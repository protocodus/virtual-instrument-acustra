from pathlib import Path
import numpy as np,json,hashlib,subprocess
root=Path(__file__).resolve().parent
rows=np.genfromtxt(root/'transfer.csv',delimiter=',',names=True)
shapes=['Parlor','Auditorium','Dreadnought','Jumbo'];woods=['Spruce','Mahogany','Maple'];bands=[(70,180),(180,650),(650,2000),(2000,6000)]
def db(x):return 10*np.log10(max(float(x),1e-30))
def summarize(a,b):
    rmsA=np.sqrt(np.mean(a*a));rmsB=np.sqrt(np.mean(b*b));aa=a/rmsA;bb=b/rmsB
    w=np.hanning(len(a))[:,None]
    pa=np.sum(np.abs(np.fft.rfft(a*w,axis=0))**2,axis=1);pb=np.sum(np.abs(np.fft.rfft(b*w,axis=0))**2,axis=1)
    f=np.fft.rfftfreq(len(a),1/48000); valid=(f>=70)&(f<8000)
    weight=np.sqrt(pa*pb);delta=10*np.log10(np.maximum(pb,1e-30)/np.maximum(pa,1e-30))-20*np.log10(rmsB/rmsA)
    weightedRms=np.sqrt(np.average(delta[valid]**2,weights=weight[valid]))
    centroid=lambda p:np.sum(f[valid]*p[valid])/np.sum(p[valid])
    return dict(native_rms_db=20*np.log10(rmsB/rmsA),gain_matched_residual_rms=float(np.sqrt(np.mean((bb-aa)**2))),gain_matched_correlation=float(np.mean(aa*bb)),power_weighted_spectral_distance_db=float(weightedRms),centroid_ratio=float(centroid(pb)/centroid(pa)),band_relative_db=[db(np.sum(pb[(f>=lo)&(f<hi)])/np.sum(pa[(f>=lo)&(f<hi)]))-20*np.log10(rmsB/rmsA) for lo,hi in bands])
def bodyResponse(r):
    l=r['body_left_real']+1j*r['body_left_imag'];rr=r['body_right_real']+1j*r['body_right_imag'];mid=(l+rr)*.5
    return np.abs(mid+.62*(l-mid))**2+np.abs(mid+.62*(rr-mid))**2
report={'git':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'note_protocol':'Fresh engine/reset, 48kHz, Finger, velocity0.72, MIDI40/string1,55/string4,64/string6; source defaults incl Width0.62 Body0.82 Output0.42, Room0/PiezoMix0; notes held1.5s; analysis20-800ms stereo-power FFT and RMS; no listening judgment inferred. Mono reconstructed from un-limited stereo pair and its stored trim. rack_blend reconstructs Rack Body.70/PiezoMix.20 keeping Output.42 (common scalar).','bands_hz':bands,'transfer':[],'notes':[]}
for s in range(4):
    a=rows[(rows['shape']==s)&(rows['wood']==0)];pa=bodyResponse(a);freq=a['frequency']
    for wood in (1,2):
        b=rows[(rows['shape']==s)&(rows['wood']==wood)];pb=bodyResponse(b)
        gain=20*np.log10(b['micgain'][0]/a['micgain'][0]);d=10*np.log10(np.maximum(pb,1e-30)/np.maximum(pa,1e-30));weights=np.sqrt(pa*pb)
        yA=a['bridge_real']+1j*a['bridge_imag'];yB=b['bridge_real']+1j*b['bridge_imag'];bridgeDB=20*np.log10(np.abs(yB)/np.abs(yA))
        bandData=[]
        for low,high in bands:
            m=(freq>=low)&(freq<high)
            bandData.append({'hz':[low,high],'radiation_power_ratio_db':db(np.trapz(pb[m],freq[m])/np.trapz(pa[m],freq[m])),'bridge_modulus_ratio_db_median':float(np.median(bridgeDB[m])),'bridge_modulus_abs_db_p90':float(np.percentile(np.abs(bridgeDB[m]),90))})
        report['transfer'].append({'shape':shapes[s],'wood':woods[wood],'stereo_gain_compensation_db':float(gain),'mono_gain_compensation_db':float(gain+20*np.log10(b['monotrim'][0]/a['monotrim'][0])),'piezo_gain_compensation_db':float(gain+20*np.log10(b['piezotrim'][0]/a['piezotrim'][0])),'bands':bandData})
        for note in (40,55,64):
            x=np.fromfile(root/f'{s}-0-{note}.f32',dtype='<f4').reshape(-1,3).astype(float);y=np.fromfile(root/f'{s}-{wood}-{note}.f32',dtype='<f4').reshape(-1,3).astype(float)
            assert np.max(np.abs(x))<.89125094 and np.max(np.abs(y))<.89125094
            for route in ('stereo','mono','piezo','rack_blend'):
                if route=='stereo':xx=x[:,:2];yy=y[:,:2]
                elif route=='piezo':xx=x[:,2:3];yy=y[:,2:3]
                elif route=='rack_blend':
                    xx=x[:,:2]*((.68+.72*.70)/(.68+.72*.82))+.20*x[:,2:3]
                    yy=y[:,:2]*((.68+.72*.70)/(.68+.72*.82))+.20*y[:,2:3]
                else:
                    xx=((x[:,0]+x[:,1])*.5-(x[:,0]-x[:,1])*.5/.62)[:,None]*a['monotrim'][0]
                    yy=((y[:,0]+y[:,1])*.5-(y[:,0]-y[:,1])*.5/.62)[:,None]*b['monotrim'][0]
                result=summarize(xx[960:38400],yy[960:38400])
                late=lambda v:db(np.mean(v[48000:67200]**2)/np.mean(v[4800:14400]**2))
                result['late_to_early_relative_db']=late(yy)-late(xx)
                report['notes'].append(dict(shape=shapes[s],wood=woods[wood],note=note,route=route,**result))
for p in ['Source/DSP/AcustraEngine.cpp','Source/DSP/AcustraEngine.h','Source/DSP/ConstructionLoudnessData.h','build-dsp/libAcustraDSP.a']:
    report.setdefault('sha256',{})[p]=hashlib.sha256(Path(p).read_bytes()).hexdigest()
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n')
for wood in ('Mahogany','Maple'):
    for route in ('stereo','mono','piezo','rack_blend'):
        selected=[v for v in report['notes'] if v['wood']==wood and v['route']==route]
        print(wood,route,'native dB range',np.round([min(v['native_rms_db'] for v in selected),max(v['native_rms_db'] for v in selected)],3),'spectral RMS dB median/range',np.round([np.median([v['power_weighted_spectral_distance_db'] for v in selected]),min(v['power_weighted_spectral_distance_db'] for v in selected),max(v['power_weighted_spectral_distance_db'] for v in selected)],3),'centroid ratio range',np.round([min(v['centroid_ratio'] for v in selected),max(v['centroid_ratio'] for v in selected)],3))
print('Dreadnought Mahogany details:')
for v in report['notes']:
    if v['shape']=='Dreadnought' and v['wood']=='Mahogany':
        print(v['note'],v['route'],'native',round(v['native_rms_db'],3),'normalizedbands',np.round(v['band_relative_db'],3),'late/earlydB',round(v['late_to_early_relative_db'],3),'correlation',round(v['gain_matched_correlation'],3))
print('Dreadnought transfers:',json.dumps([v for v in report['transfer'] if v['shape']=='Dreadnought'],indent=2))
