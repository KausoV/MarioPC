"""Compare playable forest and pre-reskin frontends on identical input on all 32 stages."""
import argparse
import concurrent.futures
import hashlib
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(exist_ok=True, parents=True)
exe = args.executable.resolve()
label = 'baseline' if args.baseline else 'forest'
settings = output / (label + '.ini')
settings.write_text('width=960\nheight=540\nscreen_mode=0\nscaling=2\nfilter=0\nvsync=0\nsmart_enemies=1\n',encoding='utf-8')
keys = ['ram_hash','ppu_hash','pixel_hash','world','level','lives','player_x','core_ticks','simulation_frame','timer_value']
def play(stage):
    world, level = stage
    name = f'{world}-{level}'
    stem = output / (label + '-' + name)
    save = stem.with_suffix('.dat')
    cmd = [str(exe),'--smoke','--fast','--level',name,'--frames','280','--settings-file',str(settings),
           '--game-save-file',str(output/'unused-save'),'--save-game',str(save),'--report',str(stem.with_suffix('.json'))]
    if not args.baseline:
        cmd.extend(['--capture-ui',str(stem.with_suffix('.png'))])
    subprocess.run(cmd,cwd=exe.parent,check=True,timeout=120,capture_output=True)
    report=json.loads(stem.with_suffix('.json').read_text(encoding='utf-8-sig'))
    snapshot=save.read_bytes()
    length=struct.unpack_from('<I',snapshot,32)[0]
    record={key:report[key] for key in keys}
    record['native_snapshot_sha256']=hashlib.sha256(snapshot[64:64+length]).hexdigest()
    assert (record['world'],record['level'],record['core_ticks'])==(world,level,280),(name,record)
    if world!=1 or level!=1: save.unlink()
    return name,record
stages=[(w,l) for w in range(1,9) for l in range(1,5)]
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    cases=dict(pool.map(play,stages))
(output/(label+'-states.json')).write_text(json.dumps(cases,indent=2),encoding='utf-8')
if not args.baseline:
    before=json.loads((output/'baseline-states.json').read_text(encoding='utf-8'))
    differences=[name for name in cases if cases[name]!=before[name]]
    if differences: raise SystemExit('Native simulation difference in '+', '.join(differences))
print(label, len(cases), 'stages checked')
