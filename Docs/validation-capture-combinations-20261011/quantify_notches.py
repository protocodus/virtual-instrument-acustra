"""Measure connected widths of existing force/moment stress notches; no new variants."""
from pathlib import Path
import hashlib,importlib.util,json
import numpy as np
from scipy.optimize import minimize_scalar,brentq

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
source=ROOT/'build-audit/studio-mic-variants/scoring/score_variants.py'
spec=importlib.util.spec_from_file_location('score',source);score=importlib.util.module_from_spec(spec);spec.loader.exec_module(score)
basepath=ROOT/'build-audit/mic-boom-20261011/physics/coefficients.csv';base=score.read_coeff(basepath)
requests=[('balanced_all','parlor','spruce','stereo_mid',254.13204819600068),
          ('balanced_all','parlor','mahogany','stereo_mid',244.71404136056861),
          ('balanced_all','auditorium','maple','stereo_mid',220.6303643910914),
          ('measured_continuity','parlor','spruce','right_mono',252.74680437682719),
          ('measured_continuity','dreadnought','maple','stereo_mid',196.8031505857011),
          ('measured_continuity','auditorium','maple','right_mono',219.53679288457684)]
out=[];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name,shape,wood,channel,seed in requests:
    path=HERE/name/'observation.csv';candidate=score.overrides(path,base)
    key=(score.SHAPES.index(shape),score.WOODS.index(wood));ratio=-1.
    def transfer(cell,f):
        h=score.response(cell,np.asarray(f));z=h[:,:2]+ratio*h[:,2:]
        return z.mean(axis=1) if channel=='stereo_mid' else z[:,1]
    def level(cell,f):return 20*np.log10(np.maximum(abs(transfer(cell,f)),1e-20))
    def change(f):return level(candidate[key],f)-level(base[key],f)
    def scalar(f):return float(change([f])[0])
    optimum=minimize_scalar(scalar,bounds=(seed-.5,seed+.5),method='bounded',options={'xatol':1e-10})
    assert optimum.success
    center=float(optimum.x);depth=scalar(center)
    widths={}
    # Each threshold is the connected interval containing the refined loss
    # minimum. Find the nearest crossing on each side, then refine its root.
    for threshold in (-6.,-10.):
        ends=[]
        for direction,bound in [(-1.,80.),(1.,300.)]:
            walk=center+direction*np.geomspace(1e-6,abs(bound-center),2000)
            values=change(walk)-threshold
            crossings=np.flatnonzero(values>=0)
            if len(crossings):
                k=int(crossings[0]);inside=center if k==0 else float(walk[k-1]);outside=float(walk[k])
                lo,hi=sorted([inside,outside]);ends.append(float(brentq(lambda f:scalar(f)-threshold,lo,hi,xtol=1e-10)))
            else:ends.append(None)
        if all(x is not None for x in ends):
            assert ends[0]<center<ends[1]
            assert np.max(change(np.linspace(ends[0],ends[1],1001)[1:-1]))<threshold+1e-7
            widths[str(threshold)]={'low_hz':ends[0],'high_hz':ends[1],'width_hz':ends[1]-ends[0],
                'width_cents':float(1200*np.log2(ends[1]/ends[0])),'threshold_crossing_residual_db':[scalar(x)-threshold for x in ends]}
        else:widths[str(threshold)]={'low_hz':ends[0],'high_hz':ends[1],'bounded_by_sweep':True}
    around=np.array([center/2**(1/6),center,center*2**(1/6)])
    lb=level(base[key],around);lc=level(candidate[key],around)
    previous=json.loads((HERE/name/'score.json').read_text())
    old=next(r for r in previous['force_transfer']['rows'] if r['shape']==shape and r['wood']==wood and r['channel']==channel and r['moment_to_force']==ratio)
    # Independent direct complex-pole form at the deepest point/crossings;
    # confirm the refined cancellation is not rational-form roundoff.
    points=np.array([center,*[interval[k] for interval in widths.values() for k in ('low_hz','high_hz')]])
    def direct(cell):
        q=np.exp(-2j*np.pi*points/48000);h=np.zeros((len(points),4),complex)
        for m in cell['modes'].values():
            pole=m[0]+1j*m[1];res=m[2::2]+1j*m[3::2]
            h+=res/(1-q[:,None]*pole)+res.conj()/(1-q[:,None]*pole.conjugate())
        filt=np.full(len(points),cell['gain'],complex)
        for b0,b1,b2,a1,a2 in cell['filters'].values():filt*=(b0+b1*q+b2*q*q)/(1+a1*q+a2*q*q)
        h*=filt[:,None];z=h[:,:2]+ratio*h[:,2:]
        return z.mean(axis=1) if channel=='stereo_mid' else z[:,1]
    direct_loss=20*np.log10(abs(direct(candidate[key]))/abs(direct(base[key])))
    numerical_error=float(np.max(abs(direct_loss-change(points))));assert numerical_error<1e-5,numerical_error
    out.append({'candidate':name,'shape':shape,'wood':wood,'channel':channel,'normalized_moment_to_force':ratio,
        'sampled_minimum_loss':old['maximum_loss'],'refined_minimum_loss':{'hz':center,'db':depth},
        'connected_relative_loss_intervals':widths,
        'local_notch_at_refined_frequency_db':{'baseline':float(lb[1]-.5*(lb[0]+lb[2])),
          'candidate':float(lc[1]-.5*(lc[0]+lc[2])),'change':float(lc[1]-.5*(lc[0]+lc[2])-lb[1]+.5*(lb[0]+lb[2]))},
        'previous_sweep_worst_local_notch_before':old['worst_local_notch_before'],
        'previous_sweep_worst_local_notch_after':old['worst_local_notch_after'],'override_sha256':sha(path),
        'independent_complex_pole_vs_rational_max_db_error':numerical_error})
receipt={'purpose':__doc__,'rate_hz':48000,'baseline_coefficients_sha256':sha(basepath),'scorer_sha256':sha(source),
    'script_sha256':sha(Path(__file__)),
    'definitions':{
      'relative_loss':'20log10(abs(H_candidate(F=1,M=-1))/abs(H_baseline(F=1,M=-1))).',
      'width':'Connected interval containing the refined minimum with relative loss below -6 or -10dB. Root-refined crossings; no musical-tone bandwidth assumed.',
      'local_notch':'Center magnitude in dB minus mean dB at +/-2 semitones, evaluated at the same refined frequency for baseline and candidate.',
      'normalized_moment':'M = physical torque/a, a=0.0232 m. M/F=-1 corresponds a bass-side archive impact. String lever arms are -1.25, -0.75, -0.25, +0.25, +0.75, +1.25; actual coupled force/moment ratios are frequency-dependent and can be complex.',
      'capture':'stereo_mid is (raw mic L+R)/2, unchanged by Width; right_mono is the separate mono microphone transfer, not a stereo sum.',
      'limits':'A narrow notch is not necessarily inaudible: a sustained harmonic can be narrower still, and bending/tuning can cross it. Point FRF loss is not whole-note loss. The native grid supplies different, actual coupled input trajectories; its finite-band partial powers can conceal narrow subband holes.'},'notches':out}
(HERE/'notch-widths.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
