#!/usr/bin/env python3
"""Bounded local PSD bridge refinement for Mores g35/g36 candidates.

The original six-string complex least-squares objective and acceptance limits
are unchanged. Frequencies stay within 0.75 initial peak linewidths and adjacent
peak midpoints; Q remains in [2,80] and at most doubles/halves. Residue matrices
are Cholesky factors, hence positive semidefinite throughout. No center impact
is fitted. The initial generator's rejection exceptions are suspended only to
retrieve a failed initialization, never to qualify a result. The refined,
float32-rounded coefficients must pass the original gates independently.
No engine or shipping-bank mutation occurs. Robert Mores dataset: CC BY 4.0.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import scipy
import sys,json,time,hashlib
import numpy as np
from scipy.optimize import minimize
import GenerateMeasuredBridge as b

def initial(f,t,ba,c,corner,modes):
    # Retrieve rejected candidates for scientific refinement; qualification is
    # evaluated separately using the original, unchanged gate values below.
    e,m=b.MAX_COMPLEX_RELATIVE_ERROR,b.MAX_MEDIAN_MAGNITUDE_ERROR_DB
    try:
        b.MAX_COMPLEX_RELATIVE_ERROR=b.MAX_MEDIAN_MAGNITUDE_ERROR_DB=float('inf')
        return b.positive_semidefinite_fit(f,t,ba,c,corner,modes)
    finally:
        b.MAX_COMPLEX_RELATIVE_ERROR,b.MAX_MEDIAN_MAGNITUDE_ERROR_DB=e,m

def run(raw, out, g, count, maxiter):
    f,tt,bb,tb=b.extract_two_point(raw,g);corner,_=b.coherence_corner(raw,g)
    modes=b.candidate_modes(f,tt+bb,count)
    fitted,advance,_,_=initial(f,tt,bb,tb,corner,modes)
    f0=np.array([m[0] for m in modes]);q0=np.array([m[1] for m in modes]);n=len(modes)
    indices=np.flatnonzero((f>=b.MINIMUM_FREQUENCY)&(f<=b.MAXIMUM_FREQUENCY))[::3]
    ff=f[indices];s=2j*b.SAMPLE_RATE*np.tan(np.pi*ff/b.SAMPLE_RATE)
    phase=np.exp(2j*np.pi*ff*advance/b.SAMPLE_RATE)
    t,ba,c=tt[indices]*phase,bb[indices]*phase,tb[indices]*phase
    shape=np.stack([np.ones(6),2*np.array(b.STRING_LEVER_ARMS),np.array(b.STRING_LEVER_ARMS)**2],axis=1)
    below=ff<corner;mean=.5*(t+ba)
    target=np.array([np.where(below,((1+u)**2*t+(1-u)**2*ba+2*(1-u*u)*c)/4,mean) for u in b.STRING_LEVER_ARMS])
    norm2=float(np.sum(abs(target)**2))
    matrix=np.zeros((len(ff),2,2),complex)
    matrix[:,0,0]=np.where(below,(t+ba+2*c)/4,mean)
    matrix[:,0,1]=matrix[:,1,0]=np.where(below,(t-ba)/4,0)
    matrix[:,1,1]=np.where(below,(t+ba-2*c)/4,0)
    eig=np.linalg.eigvalsh(matrix.real)
    audit=dict(guitar=g,corner_hz=corner,phase_advance_samples=advance,
        negative_real_string_fraction=float(np.mean(target.real<0)),
        scalar_positive_real_complex_error_lower_bound=float(np.linalg.norm(np.minimum(target.real,0))/np.sqrt(norm2)),
        negative_real_matrix_bin_fraction=float(np.mean(eig[:,0]<0)),
        negative_real_matrix_norm_fraction=float(np.linalg.norm(np.minimum(eig,0))/np.linalg.norm(matrix)),
        candidate_count=count,frequency_bound_in_initial_linewidths=.75,q_bounds=[b.Q_MINIMUM,b.Q_MAXIMUM])
    print('AUDIT',json.dumps(audit),flush=True)
    (out/f'g{g}-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    R=np.zeros((n,3));lookup={r[0]:r[2:] for r in fitted}
    for i,freq in enumerate(f0):R[i]=lookup.get(freq,(0,0,0))
    rock=f0<corner
    scale=np.maximum(R[:,0]+R[:,2],np.max(R[:,0]+R[:,2])*1e-5)
    root=np.sqrt(scale)
    aa=np.sqrt(np.maximum(R[:,0],scale*1e-8));bb0=R[:,1]/aa
    cc=np.sqrt(np.maximum(R[:,2]-bb0*bb0,scale*1e-8))*rock
    x0=np.zeros((n,5));x0[:,2]=aa/root;x0[:,3]=bb0/root;x0[:,4]=cc/root
    fscale=f0/q0
    bounds=[]
    for i in range(n):
        flo=max(b.MINIMUM_FREQUENCY,f0[i]-.75*fscale[i],f0[i]-.45*(f0[i]-f0[i-1]) if i else 0)
        fhi=min(b.MAXIMUM_FREQUENCY,f0[i]+.75*fscale[i],f0[i]+.45*(f0[i+1]-f0[i]) if i+1<n else b.MAXIMUM_FREQUENCY)
        if rock[i]:fhi=min(fhi,corner-.001)
        else:flo=max(flo,corner+.001)
        bounds.extend([((flo-f0[i])/fscale[i],(fhi-f0[i])/fscale[i]),(max(-np.log(2),np.log(b.Q_MINIMUM/q0[i])),min(np.log(2),np.log(b.Q_MAXIMUM/q0[i]))),(0,None),(None,None) if rock[i] else (0,0),(0,None) if rock[i] else (0,0)])
    def calc(x,grad=True):
        v=x.reshape(n,5);freq=f0+v[:,0]*fscale;q=q0*np.exp(v[:,1])
        a=v[:,2]*root;bv=v[:,3]*root;cv=v[:,4]*root
        r=np.stack([a*a,a*bv,bv*bv+cv*cv],axis=1)
        omega=2*b.SAMPLE_RATE*np.tan(np.pi*freq/b.SAMPLE_RATE)
        den=s[None,:]**2+(omega/q)[:,None]*s[None,:]+omega[:,None]**2
        basis=s[None,:]/den;coefficient=shape@r.T
        model=coefficient@basis;e=model-target
        value=float(np.sum(abs(e)**2)/norm2)
        if not grad:return value,model,np.column_stack([freq,q,r])
        gr=2*np.real((shape.T@e)@np.conj(basis.T))/norm2
        gr=gr.T
        gd=coefficient.T@np.conj(e)
        domega=2*np.pi/np.cos(np.pi*freq/b.SAMPLE_RATE)**2
        dbdf=-s[None,:]*(s[None,:]/q[:,None]+2*omega[:,None])*domega[:,None]/den**2
        dbdq=s[None,:]**2*(omega/q)[:,None]/den**2
        gradient=np.empty((n,5))
        gradient[:,0]=2*np.real(np.sum(gd*dbdf,axis=1))*fscale/norm2
        gradient[:,1]=2*np.real(np.sum(gd*dbdq,axis=1))/norm2
        gradient[:,2]=root*(2*a*gr[:,0]+bv*gr[:,1])
        gradient[:,3]=root*(a*gr[:,1]+2*bv*gr[:,2])
        gradient[:,4]=root*2*cv*gr[:,2]
        return value,gradient.ravel()
    x=x0.ravel();value,gradient=calc(x)
    rng=np.random.default_rng(1707);direction=rng.normal(size=x.size)
    for i, (lo,hi) in enumerate(bounds):
        if lo==hi:direction[i]=0
    direction/=np.linalg.norm(direction);eps=1e-5
    numerical=(calc(x+eps*direction)[0]-calc(x-eps*direction)[0])/(2*eps)
    exact=float(gradient@direction)
    audit['gradient_directional_error']=abs(numerical-exact)/max(abs(exact),1e-12)
    if audit['gradient_directional_error']>1e-4:raise ValueError('analytic gradient mismatch')
    iteration=0
    def callback(x):
        nonlocal iteration
        iteration+=1
        if iteration%100==0:
            v,mod,_=calc(x,False)
            mag=np.median(abs(20*np.log10(np.maximum(abs(mod),1e-30)/np.maximum(abs(target),1e-30))),axis=1)
            print(f'g{g} iteration{iteration}: complex{np.sqrt(v):.6f}, median{max(mag):.6f}',flush=True)
    result=minimize(calc,x,jac=True,method='L-BFGS-B',bounds=bounds,callback=callback,options=dict(maxiter=maxiter,maxls=30,ftol=1e-12,gtol=1e-8,maxcor=20))
    error,model,coefficients=calc(result.x,False)
    mag=np.median(abs(20*np.log10(np.maximum(abs(model),1e-30)/np.maximum(abs(target),1e-30))),axis=1)
    audit.update(optimizer_success=bool(result.success),optimizer_message=str(result.message),iterations=int(result.nit),
        complex_relative_error=float(np.sqrt(error)),magnitude_errors=mag.tolist(),
        passed=bool(np.sqrt(error)<=b.MAX_COMPLEX_RELATIVE_ERROR and max(mag)<=b.MAX_MEDIAN_MAGNITUDE_ERROR_DB),
        coefficients=coefficients.tolist())
    # Export float32 inputs to the native recurrence. Keep a one-ULP inward
    # margin on rank-one cross residues so rounding cannot make them indefinite.
    coefficients=coefficients.astype(np.float32).astype(float)
    for row in coefficients:
        bound=np.nextafter(np.float32(np.sqrt(row[2]*row[4])),np.float32(0)) if row[2]*row[4]>0 else 0.
        row[3]=np.sign(row[3])*min(abs(row[3]),float(bound))
    def evaluate(frequency):
        use=np.flatnonzero((frequency>=b.MINIMUM_FREQUENCY)&(frequency<=b.MAXIMUM_FREQUENCY))
        freq=frequency[use]; ss=2j*b.SAMPLE_RATE*np.tan(np.pi*freq/b.SAMPLE_RATE)
        omega=2*b.SAMPLE_RATE*np.tan(np.pi*coefficients[:,0]/b.SAMPLE_RATE)
        basis=ss[None,:]/(ss[None,:]**2+(omega/coefficients[:,1])[:,None]*ss[None,:]+omega[:,None]**2)
        modeled=(shape@coefficients[:,2:].T)@basis
        rotation=np.exp(2j*np.pi*freq*advance/b.SAMPLE_RATE)
        t0,bb0,c0=tt[use]*rotation,bb[use]*rotation,tb[use]*rotation
        desired=np.array([np.where(freq<corner,((1+u)**2*t0+(1-u)**2*bb0+2*(1-u*u)*c0)/4,.5*(t0+bb0)) for u in b.STRING_LEVER_ARMS])
        error=float(np.linalg.norm(modeled-desired)/np.linalg.norm(desired))
        magnitude=np.median(abs(20*np.log10(np.maximum(abs(modeled),1e-30)/np.maximum(abs(desired),1e-30))),axis=1)
        return dict(complex_relative_error=error,magnitude_errors=magnitude.tolist(),passed=bool(error<=b.MAX_COMPLEX_RELATIVE_ERROR and max(magnitude)<=b.MAX_MEDIAN_MAGNITUDE_ERROR_DB))
    audit['dense_float32_validation']=evaluate(f)
    audit['passed']=audit['passed'] and audit['dense_float32_validation']['passed']
    audit['coefficients']=coefficients.tolist()
    audit['unresolved_open_string_modes']=[dict(frequency=float(freq),q=float(q),open_string_hz=hz)
        for freq,q,*_ in coefficients for hz in b.OPEN_STRING_HZ
        if abs(1200*np.log2(freq/hz))<25 and q>=b.Q_MAXIMUM]
    audit['passed']=audit['passed'] and not audit['unresolved_open_string_modes']
    audit['residue_minimum_eigenvalue']=float(np.min(np.linalg.eigvalsh(np.array([[[r[2],r[3]],[r[3],r[4]]] for r in coefficients]))))
    if audit['residue_minimum_eigenvalue'] < -1e-12:raise ValueError('rounded residue is indefinite')
    (out/f'g{g}-report.json').write_text(json.dumps(audit,indent=2)+'\n')
    np.savez_compressed(out/f'g{g}.npz',frequency=ff,target=target,model_before_float32_rounding=model,coefficients=coefficients)
    print('DONE',g,audit['complex_relative_error'],max(mag),'passed',audit['passed'],flush=True)
    return audit


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-mat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--guitars',nargs='+',type=int,choices=(35,36),default=[35,36])
    parser.add_argument('--pole-counts',nargs='+',type=int,default=[92,71])
    parser.add_argument('--maxiter',type=int,default=800)
    args=parser.parse_args()
    if len(args.guitars)!=len(args.pole_counts):raise ValueError('one pole count per guitar required')
    if args.output.exists():raise ValueError('output must be a new directory')
    if b.digest(args.raw_mat)!=b.RAW_MD5:raise ValueError('source checksum mismatch')
    args.output.mkdir(parents=True)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(protocol=__doc__,complete=False,started_at_utc=datetime.now(timezone.utc).isoformat(),
        selected_ids=args.guitars,selected_pole_counts=args.pole_counts,max_iterations=args.maxiter,
        source_url='https://zenodo.org/records/4604577',source_license='CC BY 4.0',
        raw_md5=b.digest(args.raw_mat),raw_sha256=digest(args.raw_mat),
        source_sha256_at_start={p.name:digest(p) for p in (Path(__file__),Path(b.__file__))},
        versions=dict(python=sys.version.split()[0],numpy=np.__version__,scipy=scipy.__version__),banks=[])
    def save():
        (args.output/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    save()
    for g,count in zip(args.guitars,args.pole_counts):
        report['banks'].append(run(args.raw_mat,args.output,g,count,args.maxiter))
        save()
    report.update(complete=True,completed_at_utc=datetime.now(timezone.utc).isoformat())
    save()


if __name__=='__main__':main()
