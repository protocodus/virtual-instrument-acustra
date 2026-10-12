"""Characteristic/pulse oracle for a prescribed subsonic stop, fixed c.
Independent of Acustra's delay reader and its tuning/loss filters.
Length is expressed as one-way flight time ell=L/c, seconds.
"""
import json,math
from pathlib import Path

def zero(fun, lo, hi):
    assert fun(lo)<=0<=fun(hi)
    for _ in range(64):
        mid=(lo+hi)*.5
        if fun(mid)>0: hi=mid
        else: lo=mid
    return (lo+hi)*.5

results=[]
for fret, interval in [(2,-2),(2,2),(5,12),(17,-12)]:
    f=440*2**((40+fret-69)/12)
    ell0=1/(2*f); ell1=ell0*2**(-interval/12); tau=.006
    def ell(t): return ell0 if t<0 else ell1+(ell0-ell1)*math.exp(-t/tau)
    def speed(t): return 0 if t<0 else (ell1-ell0)/tau*math.exp(-t/tau)
    rows=[]
    for launch in [-.02,-.01,-.005,0,.005,.01,.02]:
        reflection=zero(lambda t:t-ell(t)-launch,launch,launch+.1)
        arrival=reflection+ell(reflection)
        naive=zero(lambda t:t-2*ell(t)-launch,launch,launch+.1)
        rows.append({'launch_s':launch,'reflection_s':reflection,'arrival_s':arrival,
                     'instant_length_arrival_s':naive,'arrival_error_s':naive-arrival})
    max_doppler_cents=0
    for n in range(1,4801):
        t=ell0+n/48000
        reflection=zero(lambda x:x+ell(x)-t,t-.1,t)
        exact=(1-speed(reflection))/(1+speed(reflection))
        naive=1-2*speed(t)
        max_doppler_cents=max(max_doppler_cents,abs(1200*math.log2(naive/exact)))
    results.append({'fret':fret,'slide_semitones':interval,
                    'first_boundary_arrival_s':ell0,
                    'maximum_initial_speed_over_c':speed(0),
                    'max_echo_instantaneous_frequency_error_cents':max_doppler_cents,
                    'maximum_abs_pulse_arrival_error_s':max(abs(x['arrival_error_s']) for x in rows),
                    'pulses':rows})

# Independently check the root solver against the closed form for constant
# velocity ell(t)=ell0+beta*t: launch=(1-beta)*reflection-ell0.
worst=0
for beta in [-.4,-.1,0,.1,.4]:
    ell0=.005
    for launch in [0,.005,.01]:
        reflection=zero(lambda t:t-(ell0+beta*t)-launch,launch,launch+.1)
        exact_reflection=(launch+ell0)/(1-beta)
        worst=max(worst,abs(reflection-exact_reflection))
assert worst<1e-15
out={'scope':'Fixed-wave-speed ideal transverse string with externally prescribed rigid moving stop; no claim of dispersive/two-plane/contact/full instrument qualification.',
     'method':'Retarded reflection t=reflection+L(reflection)/c; pulse launch=reflection-L(reflection)/c. Same authored 6ms exponential stop used for magnitude examples.',
     'constant_speed_closed_form_max_abs_error_s':worst,'cases':results}
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps([{k:v for k,v in x.items() if k!='pulses'} for x in results],indent=2))
