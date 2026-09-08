from pathlib import Path
import argparse
import hashlib
import json
import math

parser = argparse.ArgumentParser(description="Fixed-state modal spatial audit; no engine linkage.")
parser.add_argument("output_directory", type=Path)
args = parser.parse_args()
OUT = args.output_directory.resolve()
if OUT.exists():
    parser.error("output directory already exists; choose a fresh directory")
OUT.mkdir(parents=True)
PROTOCOL = Path(__file__).with_name("NonlinearStringSpatialAuditProtocol.md")
L = .650
mu = 1140 * math.pi * .001**2 / 4
T = mu * (2 * L * 195.99771799)**2
EA = 2.7e9 * math.pi * .001**2 / 4
EI = 310e-6
m = mu * L / 2
p, width = .173, .012
counts = [12, 24, 48, 96, 192]

def stiffness(n):
    k = n * math.pi / L
    return L / 2 * (T*k*k + EI*k**4)

def frequency(n):
    return math.sqrt(stiffness(n) / m) / (2 * math.pi)

def coefficients(count, amplitude):
    return [2*amplitude*math.sin(math.pi*n*p)
            / (math.pi**2*n*n*p*(1-p))
            * math.exp(-.5*math.pi**2*n*n*width**2)
            for n in range(1, count+1)]

def stress(s):
    root = math.sqrt(1+s*s)
    return (EA-T) * s**3 / (root*(root+1))

def evaluate(count, amplitude, grid):
    q = coefficients(count, amplitude)
    wave = [(n+1)*math.pi/L for n in range(count)]
    linear_energy = math.fsum(.5*stiffness(n+1)*q[n]**2 for n in range(count))
    linear_force = math.fsum((T*k+EI*k**3)*x for k,x in zip(wave,q))
    slope0 = math.fsum(k*x for k,x in zip(wave,q))
    potential_terms, reaction_terms = [], []
    for i in range(grid+1):
        theta = math.pi*i/grid
        slope = math.fsum(k*x*math.cos((n+1)*theta)
                          for n,(k,x) in enumerate(zip(wave,q)))
        w = L/grid * (.5 if i in (0,grid) else 1)
        strain = slope*slope / (math.sqrt(1+slope*slope)+1)
        potential_terms.append(w*(EA-T)*strain*strain/2)
        # d' grad_q - grad_y = integral tau * Dirichlet_kernel_N / L.
        kernel = (2*count+1 if i==0 else
                  math.sin((count+.5)*theta)/math.sin(theta/2)) / L
        reaction_terms.append(w*stress(slope)*kernel)
    phi = math.fsum(potential_terms)
    weak = linear_force + math.fsum(reaction_terms)
    point = linear_force + stress(slope0)
    return dict(modes=count, intervals=grid, amplitude_m=amplitude,
                highest_physical_frequency_hz=frequency(count),
                linear_energy_j=linear_energy, nonlinear_energy_j=phi,
                total_energy_j=linear_energy+phi, endpoint_slope=slope0,
                linear_force_n=linear_force, point_force_n=point,
                weak_reaction_n=weak, weak_minus_point_n=weak-point,
                relative_weak_minus_point=(weak-point)/point)

rows, failures = [], []
for amplitude in [.001,.002,.004]:
    computed = [evaluate(n,amplitude,8*n) for n in counts]
    reference = computed[-1]
    for row in computed:
        fine = evaluate(row['modes'],amplitude,16*row['modes'])
        row['quadrature_check'] = {}
        for key, floor in [('total_energy_j',1e-14),('nonlinear_energy_j',1e-14),
                           ('weak_reaction_n',1e-10)]:
            error = abs(row[key]-fine[key])
            passed = error <= max(floor,1e-8*abs(fine[key]))
            row['quadrature_check'][key] = dict(coarse=row[key],fine=fine[key],
                                               absolute_error=error,passed=passed)
            if not passed: failures.append(f"quadrature: A={amplitude},N={row['modes']},{key}")
        row['spatial_reference_modes'] = 192
        row['excluded_linear_energy_fraction'] = 1-row['linear_energy_j']/reference['linear_energy_j']
        row['relative_total_energy_error'] = row['total_energy_j']/reference['total_energy_j']-1
        row['relative_point_force_error'] = row['point_force_n']/reference['point_force_n']-1
        row['relative_weak_reaction_error'] = row['weak_reaction_n']/reference['weak_reaction_n']-1
        slope = amplitude/(p*L)
        row['unsmoothed_triangular_comparison'] = dict(endpoint_slope=slope,
             point_force_n=T*slope+stress(slope),label='Analytic slope/point-force comparison only; unsmoothed stiff-string bending energy is singular.')
        rows.append(row)

limits=[]
for cutoff in [3000,5000,10000,15000,20000,24000,48000,96000]:
    n=0
    while frequency(n+1)<cutoff: n+=1
    limits.append(dict(strict_frequency_cutoff_hz=cutoff,modes=n,
                       highest_retained_hz=frequency(n),first_excluded_hz=frequency(n+1),
                       prototype_minimum_quadrature_intervals=2*n+1))

report = dict(protocol_sha256=hashlib.sha256(PROTOCOL.read_bytes()).hexdigest(),
              source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              physical=dict(length_m=L,density_kg_m=mu,tension_n=T,EA_n=EA,EI_n_m2=EI),
              scope='Initial fixed-end spatial states only. The 192-mode reference is not an audio-rate candidate. No moving-port protocol or acoustic gate is changed.',
              rows=rows,bandwidth_limits=limits,failures=failures)
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(cases=len(rows),failures=failures,bandwidth_limits=limits),indent=2))
print('4mm: N, top Hz, excluded energy %, weak/point gap %, point-force vs192 %, weak-force vs192 %')
for row in rows:
    if row['amplitude_m']==.004:
        print(row['modes'],row['highest_physical_frequency_hz'],
              row['excluded_linear_energy_fraction']*100,
              row['relative_weak_minus_point']*100,row['relative_point_force_error']*100,
              row['relative_weak_reaction_error']*100)
raise SystemExit(bool(failures))
