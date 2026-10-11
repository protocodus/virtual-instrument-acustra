#!/usr/bin/env python3
"""Independent header-series check against the earlier scratch direct kernel."""
from pathlib import Path
import csv,hashlib,json,math,re,sys
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'build-audit/studio-mic-research'))
import prototype_observer as p
import export_candidates as old

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    header=ROOT/'Source/DSP/StudioCaptureData.h'
    text=header.read_text()
    body=re.search(r'studioGreen\[4\]\[2\]\[2\]\[113\]\s*=\s*([\s\S]*?);',text)[1]
    coefficients=np.array([float(v) for v in re.findall(r'[-+]?\d+\.\d+e[-+]\d+',body)]).reshape(4,2,2,113)
    grid=np.array([-np.pi*f/q+2j*np.pi*f for f in np.linspace(45,400,17) for q in np.geomspace(.05,150,25)])
    rows=[]
    for shape in range(4):
        width,length,depth,hole,outline=old.BODY[shape]
        g={**p.BASE,'width':width,'length':length,'depth':depth,'hole_radius':hole/2,
           'outline':outline,'plate_ry':.164/.490*length,'hole_y':.14/.490*length}
        for placement,distance in [(0,.30),(1,.45)]:
            xyz=np.array([.10,(.490-.164)/.490*length,distance]) if placement==0 else np.array([.15,g['hole_y']/2,distance])
            # This uses the original research implementation, not the new
            # generator's combined exponential or its quadrature function.
            direct=p.green(np.array([xyz,xyz*[-1,1,1]]),grid,g)*np.exp(grid[:,None,None]*distance/p.C)
            approx=np.array([np.polynomial.polynomial.polyval(grid/1000,c) for c in coefficients[shape,placement]]).T
            error=abs(approx-direct[:,0])/abs(direct[:,0])
            peak=np.unravel_index(np.argmax(error),error.shape)
            bound=[]
            for nodes,w in p.quadrature(g):
                r=np.linalg.norm(nodes-xyz,axis=1)
                u=float(abs(complex(-np.pi*400/.05,2*np.pi*400))*max(abs(r-distance))/p.C)
                bound.append(float(sum(w/r)*np.exp(u+113*np.log(u)-math.lgamma(114))))
            rows.append(dict(shape=shape,placement=placement,cases=len(grid)*2,
                max_relative_error=float(error[peak]),worst_complex_pole=[float(grid[peak[0]].real),float(grid[peak[0]].imag)],
                source=int(peak[1]),max_absolute_error=float(np.max(abs(approx-direct[:,0]))),
                max_lr_relative_error=float(np.max(abs(direct[:,0]-direct[:,1])/abs(direct[:,0]))),
                maximum_absolute_truncation_bound=max(bound)))
    assert max(r['max_relative_error'] for r in rows)<2e-13
    assert max(r['maximum_absolute_truncation_bound'] for r in rows)<1e-15
    # Check that source fit geometry has not drifted from the prior prototypes.
    free_body=re.search(r'studioFreeSource\[4\]\[2\]\[2\]\s*=\s*([\s\S]*?);',text)[1]
    free=np.array([float(v) for v in re.findall(r'[-+]?\d+\.\d+e[-+]\d+',free_body)]).reshape(4,2,2,2)
    old_fits=json.loads((ROOT/'build-audit/studio-mic-research/runtime-free-candidates-v1/manifest.json').read_text())['base_fit']
    expected=np.array([r['source_strength_plate_hole_by_input'] for r in old_fits])
    fit_error=float(np.linalg.norm(free-expected)/np.linalg.norm(expected))
    assert fit_error<1e-13
    result=dict(status='Independent finite-source formula and serialized-header check passed.',
        header_sha256=sha(header),generator_sha256=sha(ROOT/'Tools/GenerateStudioCapture.py'),
        script_sha256=sha(Path(__file__)),prototype_sha256=sha(Path(p.__file__)),
        full_domain=dict(frequency_hz=[45,400],q=[.05,150],frequencies=17,q_values=25,
            kernel_evaluations=len(grid)*4*2*2),
        maximum_relative_error=max(r['max_relative_error'] for r in rows),
        maximum_absolute_truncation_bound=max(r['maximum_absolute_truncation_bound'] for r in rows),
        source_fit_relative_error=fit_error,kernels=rows,
        limits=['Verifies the implemented declared finite-source prior, not the validity of that prior against a new studio recording.',
            'Relative kernel accuracy does not bound relative error at cancellation zeros of a complete multi-mode observer.',
            'Full conservative envelope includes all stated calibrated low-pole ranges; it is not a new measured radiation domain.'])
    (HERE/'green-series-review.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ['maximum_relative_error','maximum_absolute_truncation_bound','source_fit_relative_error']},indent=2))
if __name__=='__main__':main()
