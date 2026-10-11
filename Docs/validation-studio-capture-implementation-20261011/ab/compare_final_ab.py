#!/usr/bin/env python3
"""Compare the same fixed A/B grid after flat construction-level matching."""
from pathlib import Path
import csv,hashlib,itertools,json
import numpy as np
from scipy.io import wavfile

HERE=Path(__file__).resolve().parent
RATE=48000
WINDOWS=('attack','early','held','late')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,indent=2,allow_nan=False)+'\n')
def balance(w):
    h=w['partial_power_dbfs']
    return h[0]-10*np.log10((10**(h[1]/10)+10**(h[2]/10))/2)
def read_audio(version,key):
    return np.fromfile(HERE/(('normalized-after' if version=='after' else version)+'-renders')/'audio'/(key+'.f32'),dtype='<f4').reshape(-1,2)

def main():
    plan=json.loads((HERE/'plan.json').read_text())
    out=HERE/'comparison-final';out.mkdir(exist_ok=False);(out/'audition').mkdir()
    reports={name:json.loads((HERE/(('normalized-after' if name=='after' else name)+'-renders')/'report.json').read_text()) for name in ['before','after']}
    rows={name:{r['key']:r for r in report['renders']} for name,report in reports.items()}
    assert rows['before'].keys()==rows['after'].keys()=={r['key'] for r in plan['cases']}
    assert all(report['plan_sha256']==sha(HERE/'plan.json') for report in reports.values())
    gains={};headroom={}
    for capture in ['stereo_mic','mono_mic']:
        energy={name:sum(r['matching_reference_energy'] for r in table.values() if r['group']=='primary' and r['capture']==capture) for name,table in rows.items()}
        gains[capture]=float(np.sqrt(energy['before']/energy['after']))
        peak=max(max(10**(r['peak_dbfs']/20)*(gains[capture] if name=='after' else 1.)
                     for r in table.values() if r['capture']==capture) for name,table in rows.items())
        headroom[capture]=float(min(1.,10**(-1/20)/peak))
    changes=[]
    for key,before in rows['before'].items():
        after=rows['after'][key]
        for window in WINDOWS:
            b,a=before['windows'][window],after['windows'][window]
            partial=np.array(a['partial_power_dbfs'])-b['partial_power_dbfs']
            fold_b,fold_a=before['stereo_fold_windows'][window],after['stereo_fold_windows'][window]
            changes.append({**{field:before[field] for field in ['key','group','shape','wood','capture','technique','velocity','midi','channel','reason_note','master_tune_cents','expected_fundamental_hz','requested_third_partial_hz']},
                'window':window,'h1_h2h3_balance_change_db':float(balance(a)-balance(b)),
                'partial_power_change_db':partial.tolist(),
                'before_partial_power_dbfs':b['partial_power_dbfs'],'after_partial_power_dbfs':a['partial_power_dbfs'],
                'globally_matched_h1_change_db':float(partial[0]+20*np.log10(gains[before['capture']])),
                'rms_change_db':a['rms_dbfs']-b['rms_dbfs'],
                'fold_h1_retention_before_db':fold_b['partial_power_dbfs'][0]-b['partial_power_dbfs'][0],
                'fold_h1_retention_after_db':fold_a['partial_power_dbfs'][0]-a['partial_power_dbfs'][0],
                'estimated_settled_h1_before_hz':before['estimated_settled_h1_hz'],
                'estimated_settled_h1_after_hz':after['estimated_settled_h1_hz']})
    columns=['key','group','shape','wood','capture','technique','velocity','midi','channel','master_tune_cents','expected_fundamental_hz','window','h1_h2h3_balance_change_db','h1_change_db','h2_change_db','h3_change_db','h4_change_db','globally_matched_h1_change_db','rms_change_db','fold_h1_retention_before_db','fold_h1_retention_after_db']
    with (out/'changes.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader()
        for c in changes:
            row={k:c[k] for k in columns if k in c}
            row.update({f'h{i+1}_change_db':v for i,v in enumerate(c['partial_power_change_db'])})
            writer.writerow(row)
    groups=[]
    for shape,midi,window in itertools.product(['parlor','auditorium','jumbo'],[40,41,45,47],WINDOWS):
        selected=[c for c in changes if c['group']=='primary' and c['shape']==shape and c['midi']==midi and c['window']==window]
        if not selected:continue
        groups.append(dict(shape=shape,midi=midi,window=window,cases=len(selected),
            balance_min=min(c['h1_h2h3_balance_change_db'] for c in selected),
            balance_median=float(np.median([c['h1_h2h3_balance_change_db'] for c in selected])),
            balance_max=max(c['h1_h2h3_balance_change_db'] for c in selected),
            raw_h1_min=min(c['partial_power_change_db'][0] for c in selected),
            raw_h1_max=max(c['partial_power_change_db'][0] for c in selected),
            matched_h1_min=min(c['globally_matched_h1_change_db'] for c in selected),
            matched_h1_max=max(c['globally_matched_h1_change_db'] for c in selected)))
    # Audition files have real native A/B waveforms; only the declared global
    # matching/headroom scalars and a final20ms tail fade are applied.
    auditions=[]
    def write_audition(name,selected):
        clips=[];segments=[];frame=0
        gap=np.zeros((round(.35*RATE),2),np.float32)
        for c in selected:
            for version in ['before','after']:
                gain=headroom[c['capture']]*(gains[c['capture']] if version=='after' else 1.)
                audio=(read_audio(version,c['key'])*gain).astype(np.float32)
                n=round(.020*RATE);audio[-n:]*=np.linspace(1.,0.,n,dtype=np.float32)[:,None]
                segments.append(dict(version=version,key=c['key'],start_seconds=frame/RATE,
                    end_seconds=(frame+len(audio))/RATE,master_tune_cents=c['master_tune_cents'],
                    expected_fundamental_hz=c['expected_fundamental_hz'],technique=c['technique'],
                    velocity=c['velocity'],gain_linear=gain))
                clips += [audio,gap];frame+=len(audio)+len(gap)
        audio=np.concatenate(clips[:-1])
        path=out/'audition'/(name+'.wav');wavfile.write(path,RATE,audio)
        auditions.append(dict(path=str(path),sha256=sha(path),segments=segments,
            peak_dbfs=float(20*np.log10(max(np.max(abs(audio)),1e-30)))))
    for shape,midi in [('parlor',47),('auditorium',45),('jumbo',40),('jumbo',41)]:
        for wood,capture in itertools.product(['spruce','mahogany'],['stereo_mic','mono_mic']):
            selected=[c for c in plan['cases'] if c['group']=='primary' and c['shape']==shape and c['midi']==midi and c['wood']==wood and c['capture']==capture and c['master_tune_cents'] in [-100,0,100]]
            selected.sort(key=lambda c:c['master_tune_cents'])
            assert len(selected)==3
            write_audition(f'{shape}-{wood}-{capture}-m{midi}-pitch-ab',selected)
    for wood,capture in itertools.product(['spruce','mahogany'],['stereo_mic','mono_mic']):
        selected=[c for c in plan['cases'] if c['group']=='partial' and c['wood']==wood and c['capture']==capture]
        selected.sort(key=lambda c:(c['technique'],c['requested_third_partial_hz']))
        write_audition(f'parlor-{wood}-{capture}-lowE-H3-245-254-ab',selected)
    extrema={}
    for group in ['primary','excitation','partial']:
        selected=[c for c in changes if c['group']==group]
        extrema[group]=dict(balance_minimum=min(selected,key=lambda c:c['h1_h2h3_balance_change_db']),
            balance_maximum=max(selected,key=lambda c:c['h1_h2h3_balance_change_db']),
            h1_minimum=min(selected,key=lambda c:c['partial_power_change_db'][0]),
            h1_maximum=max(selected,key=lambda c:c['partial_power_change_db'][0]),
            h2_minimum=min(selected,key=lambda c:c['partial_power_change_db'][1]),
            h3_minimum=min(selected,key=lambda c:c['partial_power_change_db'][2]))
    centres=[]
    for shape,midi in [('parlor',47),('auditorium',45),('jumbo',40),('jumbo',41)]:
        for wood,capture,window in itertools.product(['spruce','mahogany'],['stereo_mic','mono_mic'],WINDOWS):
            subset={c['master_tune_cents']:c for c in plan['cases'] if c['group']=='primary' and c['shape']==shape and c['midi']==midi and c['wood']==wood and c['capture']==capture}
            values={}
            for version in ['before','after']:
                mid=rows[version][subset[0]['key']]['windows'][window]
                endpoints=[rows[version][subset[c]['key']]['windows'][window]['partial_power_dbfs'][0] for c in [-100,100]]
                endpoint_mean=10*np.log10(np.mean(10**(np.array(endpoints)/10)))
                values[version]=dict(h1_balance_db=float(balance(mid)),h1_power_dbfs=mid['partial_power_dbfs'][0],
                    centre_h1_vs_endpoint_power_mean_db=float(mid['partial_power_dbfs'][0]-endpoint_mean))
            centres.append(dict(shape=shape,midi=midi,wood=wood,capture=capture,window=window,
                reason_note=subset[0]['reason_note'],nominal_frequency_hz=subset[0]['expected_fundamental_hz'],**values))
    # Four compact panels, with individual construction/capture curves. No
    # pooling or smoothing hides a route; dashed/solid show native before/after.
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    fig,axes=plt.subplots(2,2,figsize=(12,7.8),sharex=True)
    routes=[('spruce','stereo_mic','#007a78'),('spruce','mono_mic','#bf6510'),
            ('mahogany','stereo_mic','#3763a4'),('mahogany','mono_mic','#944994')]
    for ax,(shape,midi) in zip(axes.flat,[('parlor',47),('auditorium',45),('jumbo',40),('jumbo',41)]):
        for wood,capture,color in routes:
            selected=sorted([c for c in plan['cases'] if c['group']=='primary' and c['shape']==shape and c['midi']==midi and c['wood']==wood and c['capture']==capture],key=lambda c:c['master_tune_cents'])
            for version,style,alpha,width in [('before','--',.65,1.25),('after','-',1.,1.8)]:
                ax.plot([c['master_tune_cents'] for c in selected],
                    [balance(rows[version][c['key']]['windows']['early']) for c in selected],
                    linestyle=style,color=color,alpha=alpha,linewidth=width)
        ax.set_title(f"{shape.title()} {selected[0]['reason_note']} — {440*2**((midi-69)/12):.2f} Hz at 0c",fontsize=11)
        ax.axvline(0,color='#999999',linewidth=.7,alpha=.55)
        ax.grid(alpha=.18);ax.set_xlim(-100,100);ax.set_xticks([-100,-50,0,50,100])
        ax.set_xlabel('Reference tuning offset (cents)');ax.set_ylabel('H1 / mean(H2, H3) power (dB)')
    fig.suptitle('Early harmonic balance around the reported notes',fontsize=16,y=.98)
    fig.text(.5,.937,'Native output · Finger96 · 0.15–0.65s · fixed body poles · every wood/capture route shown',ha='center',fontsize=10)
    handles=[Line2D([0],[0],color=c,linewidth=2,label=f"{w.title()} / {'Stereo' if cap=='stereo_mic' else 'Mono'}") for w,cap,c in routes]
    handles += [Line2D([0],[0],color='#333333',linestyle='--',label='Before'),Line2D([0],[0],color='#333333',label='After')]
    fig.legend(handles=handles,loc='lower center',bbox_to_anchor=(.5,.01),ncol=3,frameon=False,fontsize=10)
    fig.subplots_adjust(top=.865,bottom=.17,wspace=.22,hspace=.40)
    figure=out/'fine-pitch-ab.png';fig.savefig(figure,dpi=160);plt.close(fig)
    result=dict(plan_sha256=sha(HERE/'plan.json'),comparison_script_sha256=sha(Path(__file__)),
        report_sha256={name:sha(HERE/(('normalized-after' if name=='after' else name)+'-renders')/'report.json') for name in reports},
        matched_after_gain_linear=gains,matched_after_gain_db={k:float(20*np.log10(v)) for k,v in gains.items()},
        common_headroom_gain_linear=headroom,primary_summary=groups,extrema=extrema,
        native_peaks_dbfs={name:max(r['peak_dbfs'] for r in table.values()) for name,table in rows.items()},
        native_full_scale_samples={name:sum(r['full_scale_samples'] for r in table.values()) for name,table in rows.items()},
        exact_centre_statistics=centres,changes=changes,auditions=auditions,
        figure=dict(path=str(figure),sha256=sha(figure),window='early',pooled=False),listened=False)
    save(out/'comparison.json',result)
    save(out/'audition/manifest.json',dict(policy=plan['audition_policy'],gain_linear=gains,
        gain_db=result['matched_after_gain_db'],common_headroom_gain_linear=headroom,
        format='48kHz stereo IEEE float32 WAV',tail_fade_seconds=.02,gap_seconds=.35,
        listened=False,files=auditions))
    lines=['# Targeted native capture A/B','',
        '296 cases per version, identical scores and controls. Before is frozen d54; after build/binary hashes are recorded in normalized-after-manifest.json and normalized-after-renders/report.json. The production microphone references include the fixed calibration-phrase level match; approved mixture weights are unchanged. No mixture weights were changed by this test.','',
        'The scratch fixture sets the host Master Tune before the note. It preserves open/stopped geometry and retensions all strings while body/microphone poles remain fixed. This also retunes sympathetic strings; it is not an external force-only frequency sweep. Zero-cent scratch output was proven byte-identical to the original d54 renderer.','',
        '| Target | Window | H1 balance change | Raw H1 change | H1 after global level match |',
        '|---|---|---:|---:|---:|']
    for r in groups:
        lines.append(f"| {r['shape']} MIDI{r['midi']} | {r['window']} | {r['balance_min']:+.2f} to {r['balance_max']:+.2f}dB | {r['raw_h1_min']:+.2f} to {r['raw_h1_max']:+.2f}dB | {r['matched_h1_min']:+.2f} to {r['matched_h1_max']:+.2f}dB |")
    lines += ['', 'Ranges include both woods, both microphone captures and the full−100…+100c primary sweep. H1 balance is H1 relative to mean H2/H3 power. Native raw powers, early/held/release windows, folded stereo and estimated settled H1 frequencies remain in the machine-readable reports.','',
        '## Exact centre: spectral balance versus local pitch prominence','',
        'These are different quantities. Spectral balance compares H1 with H2/H3 within one note. Local pitch prominence compares H1 power at0c with the arithmetic mean of H1 power at−100c and+100c on the same string/fret. The latter is a bounded tuning-neighbor proxy, not an ordinary chromatic-note or modal-Q measurement. Values below use the early0.15–0.65s window; all windows are in comparison.json.','',
        '| Centre / wood / capture | Spectral balance before → after | Local H1 prominence before → after |',
        '|---|---:|---:|']
    for c in centres:
        if c['window']!='early':continue
        b,a=c['before'],c['after']
        lines.append(f"| {c['shape']} {c['reason_note']} / {c['wood']} / {c['capture']} | {b['h1_balance_db']:+.2f} → {a['h1_balance_db']:+.2f}dB | {b['centre_h1_vs_endpoint_power_mean_db']:+.2f} → {a['centre_h1_vs_endpoint_power_mean_db']:+.2f}dB |")
    lines += ['', '## Partial-cancellation checks','']
    for group in ['primary','excitation','partial']:
        for harmonic in [2,3]:
            c=extrema[group][f'h{harmonic}_minimum'];i=harmonic-1
            lines.append(f"- {group}, H{harmonic}: worst power change {c['partial_power_change_db'][i]:+.2f}dB, {c['key']}, {c['window']}; baseline {c['before_partial_power_dbfs'][i]:.2f}dBFS → after {c['after_partial_power_dbfs'][i]:.2f}dBFS.")
    lines += ['',
        '## Listening files','',
        'Each pitch A/B WAV alternates before then after at−100c,0c,+100c. The partial witness WAVs alternate versions at lowE tunings whose nominal third harmonics are245/254Hz, for Finger96, Pick120 and Thumb56. Exact segment offsets and hashes are in audition/manifest.json. No listening preference is claimed.','',
        'AFTER matching gain uses one scalar per capture across the whole primary Finger96 grid, applied unchanged to all models, woods, notes and techniques. Raw numerical measurements are unnormalized.','']
    for capture in gains:lines.append(f"- {capture}: after gain {result['matched_after_gain_db'][capture]:+.3f}dB; pair headroom gain {20*np.log10(headroom[capture]):+.3f}dB.")
    lines += ['', '## Limits','', *[f'- {s}' for s in plan['limitations']], '',
        'All extrema and individual cases are retained; a reduced target ratio alone is not acceptance. Fine pitch spacing does not overcome the spectral resolution of a short attack window.','']
    (out/'REPORT.md').write_text('\n'.join(lines))
    print(out/'REPORT.md')

if __name__=='__main__':main()
