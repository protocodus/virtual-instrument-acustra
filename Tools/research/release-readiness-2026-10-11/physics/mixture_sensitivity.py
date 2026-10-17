from pathlib import Path
import json,importlib.util,hashlib
import numpy as np
R=Path.cwd();D=R/'build-audit/release-model-audit-20261011/physics'
spec=importlib.util.spec_from_file_location('probe',D/'analyze.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
s=p.s;b=p.base
files=[(None,.4),('build-audit/studio-mic-variants/measured_bridge_coefficients.csv',.2),('build-audit/studio-mic-research/runtime-candidates-v2/neck30_literal264.csv',.3),('build-audit/studio-mic-research/runtime-candidates-v2/offhole45_literal264.csv',.45),('build-audit/studio-mic-research/runtime-free-candidates-v1/free_neck30.csv',.3),('build-audit/studio-mic-variants/measured_blend_coefficients.csv',.1)]
models=[b if path is None else s.overrides(R/path,b) for path,w in files]
rows=[]
for wood,f in [(0,254.1068278513776),(1,244.69165983232566)]:
 pieces=np.array([weight*p.tf(model[(0,wood)],[f],-1.)[0] for model,(_,weight) in zip(models,files)])
 total=sum(pieces);direct=p.tf(p.cur[(0,wood)],[f],-1.)[0]
 rows.append(dict(wood=wood,hz=f,normalized_moment=-1.,components_complex=[[float(x.real),float(x.imag)] for x in pieces],relative_rounding_error=float(abs(total-direct)/abs(direct)),component_cancellation_condition=float(sum(abs(pieces))/abs(total)),spatial_fractional_error_for_equal_output_change=float(abs(total)/abs(sum(pieces[2:5]))),note='Sensitivity of this specific fixed-ratio deep zero. Does not estimate real acoustic parameter errors or listener audibility.'))
result=dict(weights=[w for _,w in files],source_files=[path for path,_ in files],notches=rows)
(D/'mixture-sensitivity.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
