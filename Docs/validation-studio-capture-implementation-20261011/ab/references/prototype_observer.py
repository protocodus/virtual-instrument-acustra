#!/usr/bin/env python3
"""Research only: spatial holdout of a finite plate/hole observer, fixed poles.

All geometry not explicitly supplied by Mores is a declared prior. No source
selection, modal damping, or per-microphone gain/phase alignment is optimized.
"""
from pathlib import Path
import argparse, csv, hashlib, json, re, sys
import numpy as np
import scipy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools'))
import AuditBridgeSpatialMap as spatial

MICS = ('treble', 'bass', 'upper')
XYZ = np.array([[.10, 0., .10], [-.10, 0., .10], [.10, .20, .10]])
C = 343.
BASE = dict(width=.264, length=.490, depth=.091, hole_radius=.042,
            hole_y=.14, plate_y=0., plate_ry=.164, plate_x=0.,
            outline=.72, plate_profile=2., mic_dx=0.)
TARGETS = {
    'upper_projection_30cm': [.10, .20, .30],
    'neck_joint_30cm': [.10, .326, .30],
    'off_hole_45cm': [.15, .06, .45],
    'documented_1m_axis_prior': [0., .07, 1.],
}

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def modes():
    source = (ROOT/'Source/DSP/MeasuredBodyData.h').read_text()
    rows = []
    for match in re.finditer(r'^\s*\{ ([^{}]+) \},$', source, re.M):
        fields = match[1].replace('f','').split(',')
        if len(fields) == 14:
            rows.append([float(x) for x in fields])
    table = np.array(rows)
    residues = np.empty((len(rows), 3, 2), complex)
    for axis in range(2):
        for mic in range(3):
            start = 2 + 6*axis + 2*mic
            residues[:, mic, axis] = table[:,start] + 1j*table[:,start+1]
    return table[:,:2], residues

def geometry_variants():
    result = [('literal_dimensions', dict(BASE))]
    variations = {'width': [.244,.284,.344,.364,.384],
                  'hole_y':[.10,.12,.16,.18],
                  'plate_y':[-.04,-.02,.02,.04],
                  'plate_ry':[.124,.144,.184],
                  'plate_x':[-.02,-.01,.01,.02],
                  'plate_profile':[0.,1.],
                  'outline':[.65,.80],
                  'mic_dx':[-.01,-.005,.005,.01]}
    for name, values in variations.items():
        for value in values:
            result.append((f'{name}_{value:g}', {**BASE, name:value}))
    return result

def quadrature(g, nr=24, nt=96):
    """Area-uniform radial Gauss quadrature; weights integrate volume flow.

    Lower-bout elliptical footprint and (1-r^2)^profile mode shape are priors.
    Exclude the hole from the plate; uniform velocity on the circular hole.
    Both columns represent unit volume velocity, not equal surface velocity.
    """
    u, w = np.polynomial.legendre.leggauss(nr)
    u, w = (u+1)/2, w/2
    th = (np.arange(nt)+.5)*2*np.pi/nt
    rr, tt = np.meshgrid(np.sqrt(u), th, indexing='ij')
    weights = np.repeat(w,nt)/nt
    plate = np.column_stack((g['plate_x'] + g['width']/2*rr.ravel()*np.cos(tt.ravel()),
                             g['plate_y'] + g['plate_ry']*rr.ravel()*np.sin(tt.ravel()),
                             np.zeros(nr*nt)))
    pw = weights*np.repeat((1-u)**g['plate_profile'],nt)
    pw[((plate[:,0])**2+(plate[:,1]-g['hole_y'])**2) < g['hole_radius']**2] = 0.
    hole = np.column_stack((g['hole_radius']*rr.ravel()*np.cos(tt.ravel()),
                            g['hole_y']+g['hole_radius']*rr.ravel()*np.sin(tt.ravel()),
                            np.zeros(nr*nt)))
    return [(plate, pw/pw.sum()),(hole,weights/weights.sum())]

def green(points, s, g, nr=24, nt=96):
    """Baffled Rayleigh kernel per unit Q, omitting common rho*s/(2*pi).

    Evaluated at existing complex temporal pole s_j for residue projection.
    This preserves that pole/conjugate pair; it is a local modal approximation
    to distributed-delay propagation, not a complete causal broadband kernel.
    """
    out=np.empty((len(s),len(points),2),complex)
    for col,(nodes,weights) in enumerate(quadrature(g,nr,nt)):
        for m,point in enumerate(points):
            r=np.linalg.norm(nodes-point,axis=1)
            out[:,m,col]=(np.exp(-s[:,None]*r[None,:]/C)/r[None,:])@weights
    return out

def helmholtz(g):
    area=np.pi*g['hole_radius']**2
    neck=.003+1.7*g['hole_radius']
    volume=g['outline']*g['width']*g['length']*g['depth']
    return C/(2*np.pi)*np.sqrt(area/(volume*neck))

def metric(ref,pred):
    floor=max(np.max(abs(ref)),np.max(abs(pred)))*.01
    active=(abs(ref)>floor)&(abs(pred)>floor)
    db=20*np.log10(abs(pred[active])/abs(ref[active]))
    phase=np.abs(np.angle(pred[active]*np.conj(ref[active]),deg=True))
    return dict(complex_relative_l2=float(np.linalg.norm(pred-ref)/np.linalg.norm(ref)),
                magnitude_abs_median_db=float(np.median(abs(db))) if len(db) else None,
                magnitude_bias_median_db=float(np.median(db)) if len(db) else None,
                phase_abs_median_deg=float(np.median(phase)) if len(phase) else None)

def encode(a):
    return np.stack((np.asarray(a).real,np.asarray(a).imag),axis=-1).tolist()

def analyze(out,raw):
    fq, residues = modes()
    select = fq[:,0]<300
    fq, residues = fq[select],residues[select]
    s=-np.pi*fq[:,0]/fq[:,1] + 2j*np.pi*fq[:,0]
    variants=geometry_variants()
    rows=[]; target_rows=[]; arrays={}; geometry=[]
    for name,g in variants:
        mics=XYZ.copy(); mics[0,0]+=g['mic_dx']
        G=green(mics,s,g)
        Gt=green(np.array(list(TARGETS.values())),s,g)
        h=helmholtz(g)
        ratio=(fq[:,0]/h)**2-1
        geometry.append(dict(name=name,parameters=g,helmholtz_hz=float(h),
                             phi_plate_over_phi_hole=ratio.tolist()))
        for kind in ['free_two_source','coupled_ratio']:
            for held in range(3):
                train=[m for m in range(3) if m!=held]
                predictions=np.zeros_like(residues)
                target=np.zeros((len(fq),len(TARGETS),2),complex)
                for j in range(len(fq)):
                    # Coupled ratio is supported only for the identified A0/T1.
                    if kind=='coupled_ratio' and j>=2: continue
                    A=G[j,train,:]
                    sv=np.linalg.svd(A,compute_uv=False)
                    rank=int(np.sum(sv>sv[0]*1e-10))
                    condition=float(sv[0]/sv[1]) if rank==2 else None
                    for axis in range(2):
                        r=residues[j,:,axis]
                        if kind=='free_two_source':
                            q=np.linalg.pinv(A,rcond=1e-10)@r[train]
                            pred=G[j]@q; target[j,:,axis]=Gt[j]@q
                        else:
                            d=G[j]@np.array([ratio[j],1.])
                            scalar=np.vdot(d[train],r[train])/np.vdot(d[train],d[train])
                            pred=d*scalar
                            target[j,:,axis]=(Gt[j]@np.array([ratio[j],1.]))*scalar
                            q=scalar*np.array([ratio[j],1.])
                        predictions[j,:,axis]=pred
                        rows.append(dict(geometry=name,kind=kind,mode_hz=float(fq[j,0]),
                            axis=['heave','moment'][axis],held=MICS[held],
                            rank_free=rank,condition_free=condition,
                            train_complex_relative_l2=float(np.linalg.norm(pred[train]-r[train])/np.linalg.norm(r[train])),
                            **metric(r[held:held+1],pred[held:held+1]),
                            plate_q_real=float(q[0].real),plate_q_imag=float(q[0].imag),
                            hole_q_real=float(q[1].real),hole_q_imag=float(q[1].imag)))
                arrays[f'{name}_{kind}_{MICS[held]}_target']=target
                for j in range(2 if kind=='coupled_ratio' else len(fq)):
                    for ti,tn in enumerate(TARGETS):
                        for axis in range(2):
                            target_rows.append(dict(geometry=name,kind=kind,held=MICS[held],
                                mode_hz=float(fq[j,0]),axis=['heave','moment'][axis],target=tn,
                                residue_real=float(target[j,ti,axis].real),residue_imag=float(target[j,ti,axis].imag),
                                level_vs_upper_db=float(20*np.log10(abs(target[j,ti,axis])/abs(residues[j,2,axis])))))
    # Positive numerical witness: all holdouts recover data generated by the
    # same coupled observer, including symmetric bridge-pair-only training.
    G=green(XYZ,s,BASE); ratio=(fq[:,0]/helmholtz(BASE))**2-1
    synthetic_error=0.
    for j in range(2):
        d=G[j]@np.array([ratio[j],1.]); p=d*(.0007+.0011j)
        for held in range(3):
            tr=[i for i in range(3) if i!=held]
            pred=d*np.vdot(d[tr],p[tr])/np.vdot(d[tr],d[tr])
            synthetic_error=max(synthetic_error,float(np.max(abs(pred-p))/np.max(abs(p))))
    Gfine=green(XYZ,s,BASE,48,192)
    quadrature_error=float(np.linalg.norm(G-Gfine)/np.linalg.norm(Gfine))
    # Independent H1 frequency-domain check. This diagnostic allows source
    # strength to vary freely per frequency: useful lower bound, not a model.
    values=spatial.bridge.load_matrix(raw)
    responses,quality=spatial.extract(values)
    freq=np.fft.rfftfreq(spatial.bridge.FFT_SIZE,1/spatial.bridge.SAMPLE_RATE)
    ii=np.flatnonzero((freq>=80)&(freq<300))
    raw_rows=[]
    for window,keep in [('full_record',None),('production_250ms',12000)]:
        H=np.array([[spatial.windowed(responses[21,impact,ch],keep)[ii] for ch in [4,5,3]] for impact in [0,1,2]])
        Gf=green(XYZ,2j*np.pi*freq[ii],BASE)
        for held in range(3):
            tr=[i for i in range(3) if i!=held]
            predicted=np.empty((3,len(ii)),complex)
            for k in range(len(ii)):
                q=np.linalg.pinv(Gf[k,tr,:],rcond=1e-10)@H[:,tr,k].T
                predicted[:,k]=Gf[k,held,:]@q
            for impact in range(3):
                for low,high in [(80,130),(150,195),(195,220),(220,300),(80,300)]:
                    band=(freq[ii]>=low)&(freq[ii]<high)
                    raw_rows.append(dict(window=window,held=MICS[held],impact=['bass','center','treble'][impact],
                                         low_hz=low,high_hz=high,**metric(H[impact,held,band],predicted[impact,band])))
    # Provide 2x3 linear residue maps for root's experiments. Fit all three
    # taps using one scalar per A0/T1; do not export an unconstrained unstable
    # inverse as a shipped observer. This changes only the tested two residues.
    candidates=[]
    for name,g in [variants[0],next(v for v in variants if v[0]=='width_0.364'),
                   next(v for v in variants if v[0]=='hole_y_0.1')]:
        G=green(XYZ,s[:2],g); Gt=green(np.array(list(TARGETS.values())),s[:2],g)
        ratio=(fq[:2,0]/helmholtz(g))**2-1
        coeff=[]; pred=[]; train=[]
        for j in range(2):
            d=G[j]@np.array([ratio[j],1.])
            dt=Gt[j]@np.array([ratio[j],1.])
            K=dt[:,None]*np.conj(d)[None,:]/np.vdot(d,d)
            coeff.append(K); pred.append(K@residues[j])
            rhat=d[:,None]*(np.conj(d)@residues[j])[None,:]/np.vdot(d,d)
            train.append([metric(residues[j,:,axis],rhat[:,axis]) for axis in range(2)])
        candidates.append(dict(name=name,geometry=g,mode_frequency_q=fq[:2].tolist(),
            target_positions=TARGETS,source_mic_order=MICS,
            complex_format='final array dimension is [real,imaginary]',
            mapping_target_by_source=encode(coeff),predicted_target_residues_by_axis=encode(pred),
            all_three_training_errors=train,
            warning='Only A0/T1 low-band candidate. Other modes not spatially reconstructed; no output normalization or EQ. Do not label a partial replacement as full studio placement.'))
    hashes={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'Source/DSP/MeasuredBodyData.h',ROOT/'Source/DSP/AcustraEngine.cpp',
            ROOT/'Tools/AuditBridgeSpatialMap.py',ROOT/'Tools/GenerateMeasuredBody.py',ROOT/'Tools/GenerateMeasuredBridge.py',Path(__file__)]}
    report=dict(source='https://zenodo.org/records/4604577',raw=str(raw),raw_sha256=sha(raw),source_hashes=hashes,
        versions=dict(python=sys.version.split()[0],numpy=np.__version__,scipy=scipy.__version__),
        measurement_geometry=dict(order=MICS,xyz_metres=XYZ.tolist(),description='x treble, y neckward, z above top; bridge centre origin'),
        published_dimensions=dict(width_bottom_m=.264,width_top_m=.282,width_waist_m=.239,length_m=.490,
            depth_top_waist_bottom_m=[.090,.093,.090],bridge_to_tail_m=.164,hole_diameter_m=.084),
        missing_geometry=['soundhole center coordinates','actual top outline','spatial plate eigenfunctions','mic placement tolerance','matching far/studio pressure FRFs'],
        prior=['Lower-bout ellipse centered at bridge, radii published WB/2 and bridge-to-tail; hole omitted from plate.',
               'Plate volume velocity distribution (1-r^2)^2; uniform hole velocity; finite baffled half-space, no back/edge diffraction.',
               'Hole center y=140 mm, varied100–180 mm. Effective top-area outline factor0.72, varied0.65–0.80.',
               'Width264 mm is literal published g21 value.364 mm is an explicit sensitivity prior motivated by adjacent g18, not a correction.',
               'Eigenvector source-volume ratio uses current engine law phi_p/phi_h=(f/fH)^2−1; fH calculated from declared geometry.',
               'Existing current g21 mode frequencies,Q and complex residues kept; their authored Q correction remains. No model fit or render.',
               'Greens function evaluated at complex poles for residue mapping; exact poles conserved, propagation modeled locally per mode only.',
               'Free two-source symmetric bridge pair has rank1; pseudoinverse output is labeled unidentifiable, not a successful prediction.',
               'No geometry optimized on these observations; one-at-a-time prior perturbations only.'],
        frequency_q=fq.tolist(),geometry_variants=geometry,
        checks=dict(synthetic_coupled_holdout_error=synthetic_error,quadrature_relative_l2=quadrature_error),
        modal_holdout_rows=len(rows),raw_holdout_rows=len(raw_rows),candidates=candidates,
        warnings=['Mores Method2c states taps insufficient for nearfield reconstruction.2d records1m data exists but is excluded.',
                  'Geometry fit cannot validate relocation: all measured microphones lie on the10cm plane.',
                  'Retaining poles in residue projection does not reconstruct full delayed broadband pressure.'],
        force_quality=quality)
    out.mkdir(exist_ok=False)
    for filename,data in [('modal-holdout.csv',rows),('target-sensitivity.csv',target_rows),('raw-holdout.csv',raw_rows)]:
        with (out/filename).open('w') as f:
            w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    np.savez(out/'predictions.npz',frequency_q=fq,source_residues=residues,**arrays)
    (out/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    (out/'candidates.json').write_text(json.dumps(candidates,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(output=str(out),checks=report['checks'],modal_rows=len(rows),raw_rows=len(raw_rows))))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();analyze(a.output.resolve(),a.raw.resolve())
