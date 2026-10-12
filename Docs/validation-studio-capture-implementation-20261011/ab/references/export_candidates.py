#!/usr/bin/env python3
"""Export research-only 48k modal-observer candidates, no production mutation."""
from pathlib import Path
import csv,json,re
import numpy as np
import prototype_observer as p

ROOT=p.ROOT
OUT=Path(__file__).resolve().parent/'runtime-candidates-v2'
DUMP=ROOT/'build-audit/mic-boom-20261011/physics/coefficients.csv'
BODY=np.array([[.343,.467,.108,.1016,.72],[.381,.492,.105,.1016,.72],
               [.397,.508,.111,.1016,.75],[.432,.533,.124,.1016,.72]])

def parse_header(path):
    rows=[]
    for match in re.finditer(r'^\s*\{ ([^{}]+) \},$',path.read_text(),re.M):
        fields=match[1].replace('f','').split(',')
        if len(fields) in [14,17]: rows.append([float(x) for x in fields[:14]])
    table=np.array(rows)
    r=np.empty((len(rows),3,2),complex)
    for axis in range(2):
        for mic in range(3):
            k=2+axis*6+mic*2
            r[:,mic,axis]=table[:,k]+1j*table[:,k+1]
    return table[:,:2],r

def normalization(g,s,f):
    G=p.green(p.XYZ,np.array([s]),g)[0]
    a=(f/p.helmholtz(g))**2-1
    # Remove the declared normal-distance arrival reference. Keep path-length
    # differences across the plate/hole rather than imposing new onset timing.
    return (G@np.array([a,1.]))/(a+1.) * np.exp(s*.10/p.C)

def run():
    OUT.mkdir(exist_ok=False)
    ownfq,ownr=parse_header(ROOT/'Source/DSP/MeasuredBodyData.h')
    jointfq,jointr=parse_header(ROOT/'Source/DSP/MeasuredJointBodyData.h')
    rows=[]
    for row in csv.reader(DUMP.open()):
        if row[0]=='M':
            shape,wood,slot=map(int,row[1:4]); x=list(map(float,row[4:]))
            residues=np.array([[complex(x[2],x[3]),complex(x[6],x[7])],
                               [complex(x[4],x[5]),complex(x[8],x[9])]])
            rows.append((shape,wood,slot,complex(x[0],x[1]),residues))
    candidates=[('neck30_literal264',.30,'neck',dict(p.BASE)),
                ('offhole45_literal264',.45,'offhole',dict(p.BASE)),
                ('neck30_width364_prior',.30,'neck',{**p.BASE,'width':.364})]
    manifest=dict(input=str(DUMP),input_sha256=p.sha(DUMP),sample_rate=48000,
                  source_hashes={str(q.relative_to(ROOT)):p.sha(q) for q in [
                    ROOT/'Source/DSP/MeasuredBodyData.h',ROOT/'Source/DSP/MeasuredJointBodyData.h',
                    ROOT/'Source/DSP/AcustraEngine.cpp',Path(__file__),Path(p.__file__)]},
                  rows_per_candidate=len(rows),candidates=[])
    keys=['shape','wood','mode','leftRe','leftIm','rightRe','rightIm',
          'leftMomentRe','leftMomentIm','rightMomentRe','rightMomentIm']
    for name,distance,position,reference in candidates:
        exported=[]; diagnostics=[]; scalar_amplitudes={}
        # Only actual identified coupled modes. Joint83Hz auxiliary is not
        # assigned an invented A0 eigenvector; receives common distance only.
        selection={0:('own',0),1:('own',1),133:('joint',1),134:('joint',2)}
        for slot,(bank,index) in selection.items():
            fq,r=(ownfq,ownr) if bank=='own' else (jointfq,jointr)
            f,q=fq[index]
            s=-np.pi*f/q+2j*np.pi*f
            d=normalization(reference,s,f)
            amplitudes=(np.conj(d)@r[index])/np.vdot(d,d)
            scalar_amplitudes[slot]=amplitudes
        for shape,wood,slot,pole,current in rows:
            width,length,depth,hole,outline=BODY[shape]
            g={**reference,'width':width,'length':length,'depth':depth,'hole_radius':hole/2,
                'outline':outline,'plate_ry':.164/.490*length,'hole_y':reference['hole_y']/.490*length}
            if position=='neck':
                target=np.array([[.10,(.490-.164)/.490*length,distance],
                                 [-.10,(.490-.164)/.490*length,distance]])
            else:
                target=np.array([[.15,g['hole_y']/2,distance],[-.15,g['hole_y']/2,distance]])
            # Global geometric gain is shared by every unchanged mode/input/
            # channel. No artificial frequency-dependent gain or added pole.
            # Preserve old high-band phase: its distributed source shape is
            # unknown, and a modal phase twist is not a causal common delay.
            value=current*(.10/distance)
            if slot in selection:
                bank,index=selection[slot]
                fq,r=(ownfq,ownr) if bank=='own' else (jointfq,jointr)
                raw=r[index,[0,2],:]
                unboosted=current.copy()
                if 85.<fq[index,0]<145.: unboosted[0]/=4.
                drive=np.vdot(raw.ravel(),unboosted.ravel())/np.vdot(raw.ravel(),raw.ravel())
                reconstruction=drive*raw
                drive_error=float(np.linalg.norm(reconstruction-unboosted)/np.linalg.norm(unboosted))
                if drive_error>2e-6:
                    raise RuntimeError(f'Runtime residue scale inconsistent {shape} {wood} {slot}: {drive_error}')
                s=np.log(pole)*48000.
                f=s.imag/(2*np.pi)
                a=(f/p.helmholtz(g))**2-1
                G=p.green(target,np.array([s]),g)[0]
                d=(G@np.array([a,1.]))/(a+1.) * np.exp(s*distance/p.C)
                value=d[:,None]*scalar_amplitudes[slot][None,:]*drive
                diagnostics.append(dict(shape=shape,wood=wood,slot=slot,bank=bank,raw_index=index,
                    runtime_frequency_hz=float(f),helmholtz_hz=float(p.helmholtz(g)),
                    phi_plate_over_phi_hole=float(a),phi_sum_normalization=float(a+1),
                    target_xyz_m=target.tolist(),common_drive=[float(drive.real),float(drive.imag)],
                    drive_relative_error=drive_error,
                    native_ratio_to_current_db=(20*np.log10(abs(value)/abs(current))).tolist(),
                    ratio_after_common_distance_match_db=(20*np.log10(abs(value)/abs(current)*distance/.10)).tolist()))
            exported.append([shape,wood,slot,value[0,0].real,value[0,0].imag,
                             value[1,0].real,value[1,0].imag,value[0,1].real,value[0,1].imag,
                             value[1,1].real,value[1,1].imag])
        path=OUT/(name+'.csv')
        with path.open('w') as f:
            w=csv.writer(f); w.writerow(keys);w.writerows(exported)
        manifest['candidates'].append(dict(name=name,path=str(path),sha256=p.sha(path),
            reference_geometry=reference,distance_m=distance,nominal_common_distance_gain=.10/distance,
            changed_spatial_slots=list(selection),diagnostics=diagnostics))
    manifest['method']=[
        'No production changes. CSV replaces runtime observation residues only; poles/state/bridge/piezo preserved.',
        'Raw measured g21 complex common-pole residue vectors at all3 microphones fit one complex amplitude per input and A0/T1 mode.',
        'Normalized volume-flow observer d=(phi_plate*Gplate+phi_hole*Ghole)/(phi_plate+phi_hole), with coupled eigenvector ratio(f/fH)^2−1.',
        'Normalization removes total volume-flow participation already included by runtime construction modal weights; target geometry changes observer only.',
        'Finite plate/hole Green kernels are evaluated at existing complex poles. This is a local modal propagation approximation, not an exact delayed transient.',
        'Target construction geometry uses engine width,length,depth,hole,outline; lower-bout length and hole center scale with measuredg21 length.',
        'Same geometry perWood; actual runtimeWood pole supplies its eigenvector ratio and propagation frequency.',
        'Exactly four spatially replaced slots: ownA0/T1(0,1), jointA0/T1(133,134). Auxiliary joint83Hz retains measured spatial response.',
        'Common runtime residue scalar recovered from2audible microphones×2inputs; treble×4removed once for selected raw85–145Hz modes.',
        'Every other mode/continuation receives only common0.1/distance amplitude gain, preserving existing phase and high-band directivity.',
        'No narrow EQ, no per-note normalization, no geometry optimizer. Each candidate is partial low-band radiation, NOT complete studio XY/nearfield reconstruction.',
        'For spectral comparison use a single common1k anchor; distance gain is explicit and does not constitute the low-band shape result.',
        'Remove commonnormal-distance arrival reference: referenceGreen×exp(s_ref*0.10/c), targetGreen×exp(s_target*distance/c). Relative source-path phase remains; no common timing shift added. No validated arrival-time claim.',
        'Literal264mm reference width follows published table;364mm candidate is an explicit geometric sensitivity prior, not corrected metadata.',
    ]
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2,allow_nan=False)+'\n')
    print(OUT)
    print('Rows per candidate:',len(rows),'max scale recovery error:',max(x['drive_relative_error'] for c in manifest['candidates'] for x in c['diagnostics']))

if __name__=='__main__': run()
