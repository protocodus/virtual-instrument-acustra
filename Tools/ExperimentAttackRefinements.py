#!/usr/bin/env python3
"""Reproduce the four 2026-10-15 attack experiments on frozen Acustra 90d2d8c.

Requires C++20, NumPy and SciPy. No production source is modified. Example:
  git archive 90d2d8c Source/DSP | tar -x -C /tmp/acustra-frozen
  python3 Tools/ExperimentAttackRefinements.py --source /tmp/acustra-frozen --output /tmp/attack-review

Outputs native stereo float WAVs, measurements.json and provenance.json.
The synthetic simultaneous engine chords isolate force spread; they do not
represent the Performer's timed strokes. Descriptors establish differences,
not listener preference or a recording-realism improvement.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, sys

BASELINE_HASH = 'cee45a0d3b34e01b836d5ef86aff7d16338f9bd71686230aba36c68cfdfa489b'
PROBE = r'''#include "DSP/AcustraEngine.h"
#include <fstream>
#include <memory>
#include <vector>
#include <filesystem>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>
namespace acustra { extern int attackExperiment; }
using namespace acustra;
constexpr int rate=48000,block=64;
std::string outputDirectory;
void wave(std::string path,const std::vector<float>& a){std::ofstream f(path,std::ios::binary);auto u16=[&](unsigned x){for(int i=0;i<2;i++)f.put(x>>(i*8));};auto u32=[&](unsigned x){for(int i=0;i<4;i++)f.put(x>>(i*8));};f.write("RIFF",4);u32(36+a.size()*4);f.write("WAVEfmt ",8);u32(16);u16(3);u16(2);u32(rate);u32(rate*8);u16(8);u16(32);f.write("data",4);u32(a.size()*4);f.write((char*)a.data(),a.size()*4);}
struct E{int frame,note;float velocity;};
void run(std::string name,PickingTechnique technique,int variant,std::vector<E> events,int frames,bool strum=false){auto e=std::make_unique<AcustraEngine>();attackExperiment=variant;auto c=fittedPhysicalCalibration;if(variant==1)c.pickEdgeRadiusMetres*=1.5f;if(variant==2&&technique==PickingTechnique::Pick)c.steel.transientScale*=.5f;if(variant==4)c.pickReleaseVelocityShare*=.65f;e->setPhysicalCalibration(c);EngineParameters p;p.picking=technique;p.outputGain=1;p.room=.5;p.piezoMix=.2;p.releaseNoise=.1;e->prepare(rate,block);e->setParameters(p);std::vector<float>a(frames*2);float l[64],r[64];int event=0;for(int frame=0;frame<frames;){if(event<int(events.size())&&events[event].frame==frame){if(strum)e->beginStrum(0,0,false,0,5);do{e->noteOn(events[event].note,events[event].velocity,1,0,strum);event++;}while(event<int(events.size())&&events[event].frame==frame);}int n=std::min(block,frames-frame);if(event<int(events.size()))n=std::min(n,events[event].frame-frame);e->process(l,r,n);for(int j=0;j<n;j++){a[2*(frame+j)]=l[j];a[2*(frame+j)+1]=r[j];}frame+=n;}wave(outputDirectory+"/"+std::to_string(variant)+"-"+name+".wav",a);}
int main(int argc,char**argv){if(argc!=2)return 2;outputDirectory=argv[1];std::filesystem::create_directories(outputDirectory);for(int v=0;v<=4;v++){for(int n:{40,52,64,76})for(int vel:{40,80,120})run("pick-n"+std::to_string(n)+"-v"+std::to_string(vel),PickingTechnique::Pick,v,{{4800,n,vel/127.f}},rate);std::vector<E>notes;for(int i=0;i<12;i++)for(int n:{40,47,52,56,59,64})notes.push_back({4800+i*12000,n,.76f});run("strum",PickingTechnique::Pick,v,notes,rate*4,true);std::cout<<v<<std::endl;}}
'''

ANALYSIS = r'''from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter,sosfilt
import json,sys
p=Path(sys.argv[1])
def db(x):return 10*np.log10(max(x,1e-30))
rows=[]
for f in sorted(p.glob('*.wav')):
 sr,x=wavfile.read(f);x=x.astype(float);mono=x.mean(axis=1);a=mono[4800:4800+720];b=mono[4800+2400:4800+12000]
 def band(lo,hi,signal):
  sos=butter(4,[lo,hi],fs=sr,btype='bandpass',output='sos');return np.mean(sosfilt(sos,signal)**2)
 row={'file':f.name,'peak_db':20*np.log10(np.max(np.abs(x))+1e-30),'rms_db':db(np.mean(x*x)), 'attack_hi_db':db(band(4000,16000,a)), 'attack_mid_db':db(band(1000,4000,a)), 'attack_low_db':db(band(80,1000,a)), 'sustain_hi_db':db(band(4000,16000,b)),'sustain_low_db':db(band(80,1000,b))}
 if 'strum' in f.name:
  levels=[db(np.mean(x[4800+i*12000:4800+i*12000+4800]**2)) for i in range(12)];row['stroke_level_sd']=np.std(levels);row['stroke_level_span']=np.ptp(levels)
 rows.append(row)
base={r['file'][2:]:r for r in rows if r['file'].startswith('0-')}
out={}
for v in range(1,5):
 vr=[r for r in rows if r['file'].startswith(str(v)+'-') and 'strum' not in r['file']]
 out[v]={}
 for key in ['peak_db','rms_db','attack_hi_db','attack_mid_db','attack_low_db','sustain_hi_db','sustain_low_db']:
  values=[r[key]-base[r['file'][2:]][key] for r in vr];out[v][key]={'min':min(values),'median':float(np.median(values)),'max':max(values)}
 out[v]['strum']={k:r[k] for r in rows if r['file']==str(v)+'-strum.wav' for k in ['peak_db','rms_db','stroke_level_sd','stroke_level_span']}
print(json.dumps(out,indent=2));(p.parent/'measurements.json').write_text(json.dumps({'summary':out,'rows':rows},indent=2))
'''

PROVENANCE = {'baseline_revision': '90d2d8c', 'engine_sha256': 'cee45a0d3b34e01b836d5ef86aff7d16338f9bd71686230aba36c68cfdfa489b', 'toolchain': 'GCC 14.2, -O3 -DNDEBUG -std=c++20', 'rate': 48000, 'block': 64, 'controls': 'plugin defaults: output1,room0.5,piezo0.2,releaseNoise0.1; Pick', 'note_grid': {'midi': [40, 52, 64, 76], 'velocity': [40, 80, 120], 'onset_seconds': 0.1, 'duration_seconds': 1}, 'chords': '12 same-time6-string engine contacts at250ms spacing, no Performer sweep; isolates force spread, not a full performed-stroke model', 'variants': {'0': 'baseline', '1': 'pick edge radius x1.5', '2': 'Pick-only contact roughness amplitude x0.5 (selected)', '3': 'strum force spread7.74 to5.418dB', '4': 'pick release velocity energy share x0.65'}, 'audio': 'native stereo float32, no normalization, same seeds; first15ms attack band measurements4thorder Butterworth causal filter applied to isolated attack window', 'limitations': 'Objective descriptors and isolated rendering, no listener preference established; does not compare recording realism scores.'}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path, help='frozen repository root with Source/DSP')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', default='c++')
    args = parser.parse_args()
    source = args.source / 'Source' / 'DSP'
    digest = hashlib.sha256((source / 'AcustraEngine.cpp').read_bytes()).hexdigest()
    if digest != BASELINE_HASH:
        raise SystemExit('Expected unchanged Acustra 90d2d8c source; engine SHA256 was ' + digest)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    dsp = output / 'DSP'
    shutil.copytree(source, dsp, ignore=shutil.ignore_patterns('SampleBank'), dirs_exist_ok=True)
    path = dsp / 'AcustraEngine.cpp'
    text = path.read_text()
    before = 'strumLevelGain = std::pow(10.0f, 7.74f * forceVariation / 20.0f);'
    after = 'strumLevelGain = std::pow(10.0f, (attackExperiment == 3 ? 5.418f : 7.74f) * forceVariation / 20.0f);'
    if text.count(before) != 1: raise SystemExit('Could not uniquely identify force-spread experiment point')
    text = text.replace(before, after).replace('namespace acustra\n{', 'namespace acustra\n{\nint attackExperiment = 0;', 1)
    path.write_text(text)
    (output / 'probe.cpp').write_text(PROBE)
    (output / 'analyse.py').write_text(ANALYSIS)
    command = [args.compiler, '-O3', '-DNDEBUG', '-std=c++20', '-I' + str(output), str(output / 'probe.cpp'), str(path), '-o', str(output / 'probe')]
    subprocess.run(command, check=True)
    subprocess.run([str(output / 'probe'), str(output / 'audio')], check=True)
    subprocess.run([sys.executable, str(output / 'analyse.py'), str(output / 'audio')], check=True)
    provenance = dict(PROVENANCE)
    provenance['compiler'] = subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0]
    provenance['command'] = command
    provenance['probe_sha256'] = hashlib.sha256(PROBE.encode()).hexdigest()
    provenance['analysis_sha256'] = hashlib.sha256(ANALYSIS.encode()).hexdigest()
    provenance['audio_sha256'] = {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in sorted((output / 'audio').glob('*.wav'))}
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')

if __name__ == '__main__': main()
