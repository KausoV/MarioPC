"""Actual SDL EXE validation: two native actors descend a real authored platform.

The C++ helper supplies explicitly assisted actor positions on World 1-1 terrain
that the production native parser built. Every recorded movement/collision after
loading is ordinary production gameplay; this is not an unassisted playthrough.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument('--exe', required=True)
ap.add_argument('--baseline-exe', required=True)
ap.add_argument('--fixture', required=True)
ap.add_argument('--output', required=True)
ap.add_argument('--phase', choices=['before', 'after', 'all'], default='all')
a = ap.parse_args()
exe, baseline, fixture, out = [Path(p).resolve() for p in [a.exe,a.baseline_exe,a.fixture,a.output]]
out.mkdir(parents=True, exist_ok=True)
meta = json.loads((fixture.parent/'fixture-source.json').read_text(encoding='utf-8-sig'))
settings = out/'enhanced.ini'
settings.write_text('width=1280\nheight=720\nscreen_mode=0\nscaling=2\nfilter=0\nvsync=0\nno_timer=1\nsmart_enemies=1\n', encoding='utf-8')
quiet = out/'no-input.txt'
quiet.write_text('0 1 S\n', encoding='utf-8')  # fixture's native frame is already > 1
checks, samples = [], []
def check(name, ok, detail=''):
    checks.append({'name':name,'passed':bool(ok),'detail':str(detail)})
    print(('PASS ' if ok else 'FAIL ')+name, flush=True)
def read_saved(path):
    raw = path.read_bytes()
    ram = raw[108:108+0x10000]
    return {'actors':[{'slot':s,'flag':ram[0xf+s],'id':ram[0x16+s], 'state':ram[0x1e+s],
             'x':ram[0x6e+s]*256+ram[0x87+s], 'y':ram[0xcf+s], 'y_high':ram[0xb6+s],
             'vx':struct.unpack('b',bytes([ram[0x58+s]]))[0],
             'vy':struct.unpack('b',bytes([ram[0xa0+s]]))[0],
             'direction':ram[0x46+s]} for s in range(2)],
            'player_x':ram[0x6d]*256+ram[0x86], 'player_y':ram[0xce],
            'camera':ram[0x71a]*256+ram[0x71c], 'op_mode':ram[0x770], 'engine':ram[0xe],
            'lives':ram[0x75a], 'timer_digits':list(ram[0x7f8:0x7fb]),
            'terrain_ram_sha256':hashlib.sha256(ram[0x500:0x6a0]).hexdigest()}
def run(label, frames, program):
    saved, png, report = [out/(label+ext) for ext in ['.mario-save','.png','.json']]
    cmd=[str(program),'--smoke','--fast','--frames',str(frames),'--settings-file',str(settings),
         '--game-save-file',str(out/'unused-user-save.dat'),'--input-script',str(quiet),
         '--load-game',str(fixture),'--save-game',str(saved),'--capture-graphics',str(png),'--report',str(report)]
    env=os.environ.copy(); env['PATH']=str(exe.parent)+os.pathsep+env.get('PATH','')
    proc=subprocess.run(cmd,cwd=out,capture_output=True,timeout=60,env=env)
    check(label+' actual EXE exits successfully',proc.returncode==0,
          proc.stderr.decode('utf-8','replace')[-1000:] if proc.returncode else '')
    if proc.returncode:raise RuntimeError(label+' failed: '+str(proc.returncode))
    state=read_saved(saved)
    state.update(label=label,frames=frames,png=str(png),program=str(program),
                 report=json.loads(report.read_text(encoding='utf-8-sig')))
    samples.append(state)
    return state
initial=read_saved(fixture)
check('fixture initializes two native Goombas on authored high platform',
      all(e['flag']==1 and e['id']==6 and e['y']+24==meta['platform_top'] and e['vx']==-8 for e in initial['actors']))
check('fixture declares no terrain modifications',meta['terrain_modified'] is False)
if a.phase in ['before','all']:
    old=[run('before-'+str(n).zfill(3),n,baseline) for n in [1,96,192,240,288]]
    check('old enhanced AI reproduces both actors staying upstairs',
          all(e['flag']==1 and e['y']==meta['actor_y'] for e in old[-1]['actors']))
if a.phase in ['after','all']:
    new=[run('after-'+str(n).zfill(3),n,exe) for n in [2,32,64,96,128,160,192,224,240]]
    for slot in range(2):
        timeline=[s['actors'][slot] for s in new]
        check(f'actor {slot+1} naturally leaves the original high platform',any(e['y']>meta['actor_y'] for e in timeline))
        check(f'actor {slot+1} completes a native landing on lower ground',
              any(e['flag']==1 and e['y']==184 and e['y_high']==1 and e['vy']==0 for e in timeline))
        check(f'actor {slot+1} preserves native horizontal speed magnitude',all(abs(e['vx'])==8 for e in timeline if e['flag']))
        check(f'actor {slot+1} remains alive during the descent sequence',all(e['flag']==1 and e['id']==6 and e['state']<0x20 for e in timeline))
    check('player stays under ordinary controls without forced movement after loading',
          all(s['player_x']==meta['player_x'] and s['player_y']==meta['player_y'] for s in new))
    check('actual EXE applies enhanced AI and unlimited timer',all(s['report']['smart_enemies'] and s['report']['no_timer'] for s in new))
# Every panel is a production --capture-graphics image; only the labels are added.
selected=[s for s in samples if s['label'].startswith('after-') and s['frames'] in [2,64,96,160,192,224,240]]
if selected:
    thumbs=[]
    for s in selected:
        im=Image.open(s['png']).convert('RGB'); im.thumbnail((854,480))
        tile=Image.new('RGB',(854,520),'#15202b');tile.paste(im,(0,40))
        d=ImageDraw.Draw(tile);d.text((12,10),f"Actual EXE +{s['frames']} frames | "+' / '.join(f"actor{e['slot']+1}:({e['x']},{e['y']})" for e in s['actors']),fill='white')
        thumbs.append(tile)
    sheet=Image.new('RGB',(1708,520*((len(thumbs)+1)//2)),'#15202b')
    for i,im in enumerate(thumbs):sheet.paste(im,((i%2)*854,(i//2)*520))
    sheet.save(out/'actual-exe-descent-contact-sheet.png')
result={'scope':meta['scope'],'fixture_metadata':meta,'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),
        'baseline_executable_sha256':hashlib.sha256(baseline.read_bytes()).hexdigest(),
        'current_executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'phase':a.phase,'checks':len(checks),'failures':sum(not c['passed'] for c in checks),'cases':checks,'samples':samples}
(out/('frontend-'+a.phase+'-verification.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(f"RESULT {result['checks']} checks, {result['failures']} failures",flush=True)
raise SystemExit(bool(result['failures']))



