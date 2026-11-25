"""Verify rule settings against the actual EXE with isolated state and saves."""
import argparse, hashlib, json, subprocess
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--exe',required=True);ap.add_argument('--output',required=True);ap.add_argument('--baseline');a=ap.parse_args()
exe=Path(a.exe).resolve();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True);checks=[]
def check(name,ok,detail=''):
 checks.append({'name':name,'passed':bool(ok),'detail':str(detail)});print(('PASS ' if ok else 'FAIL ')+name,flush=True)
def settings(name,no_timer,ai):
 p=out/(name+'.ini');p.write_text(f'width=1280\nheight=720\nscreen_mode=0\nscaling=2\nfilter=0\nvsync=0\nno_timer={int(no_timer)}\nsmart_enemies={int(ai)}\n',encoding='utf-8');return p
def run(name,cfg,frames,*more,binary=exe):
 report=out/(name+'.json')
 proc=subprocess.run([str(binary),'--smoke','--fast','--frames',str(frames),'--settings-file',str(cfg),'--game-save-file',str(out/'unused-save.dat'),'--report',str(report),*map(str,more)],cwd=out,timeout=50,capture_output=True)
 check(name+' process exits successfully',proc.returncode==0,proc.returncode)
 if proc.returncode or not report.is_file():raise RuntimeError(proc.stderr.decode(errors='replace')[-1000:])
 return json.loads(report.read_text(encoding='utf-8-sig'))
standard=settings('standard',False,False);unlimited=settings('unlimited',True,False);enhanced=settings('enhanced',True,True)
s=run('standard600',standard,600,'--capture-ui',out/'standard600.png');u=run('unlimited600',unlimited,600,'--capture-ui',out/'unlimited600.png');e=run('enhanced600',enhanced,600)
check('standard preferences remain opt-in',not s['no_timer'] and not s['smart_enemies'])
check('standard timer really counts down',s['timer_value']<401)
check('unlimited timer is applied in actual gameplay',u['no_timer'] and u['timer_value']==401)
check('enhanced setting is enabled independently',e['no_timer'] and e['smart_enemies'])
check('both rule choices keep exact simulation step count',all(x['simulation_frame']==600 and x['core_ticks']==600 for x in [s,u,e]))
for cfg,label in [(standard,'standard'),(unlimited,'unlimited'),(enhanced,'enhanced')]:
 data=run(label+'-roundtrip',cfg,1,'--save-settings')
 text=cfg.read_text(encoding='utf-8-sig');check(label+' preferences persist through application settings writer',('no_timer='+str(int(data['no_timer']))) in text and ('smart_enemies='+str(int(data['smart_enemies']))) in text)
# Reloaded state follows the current persisted preferences; no save format bump.
saved=out/'enhanced220.dat';save=run('enhanced-save220',enhanced,220,'--save-game',saved)
for n in [1,9,80]:
 loaded=run('enhanced-loaded'+str(n),enhanced,n,'--load-game',saved)
 continuous=run('enhanced-continuous'+str(220+n),enhanced,220+n)
 for field in ['ram_hash','ppu_hash','pixel_hash','presentation_hash','simulation_frame']:
  check(f'enhanced +{n} replay {field}',str(loaded[field])==str(continuous[field]))
 check(f'enhanced +{n} keeps selected rules',loaded['no_timer'] and loaded['smart_enemies'])
paused=run('rules-paused',enhanced,12,'--settings','--settings-tab','4','--load-game',saved,'--capture-ui',out/'rules-paused.png')
check('rules tab opens at correct page and pauses native steps',paused['menu_visible'] and paused['menu_tab']==4 and paused['core_ticks']==0)
for field in ['ram_hash','ppu_hash','pixel_hash','presentation_hash','simulation_frame']:
 check('rules pause preserves '+field,str(paused[field])==str(save[field]))
classic=run('loaded-with-standard-rules',standard,1,'--load-game',saved)
check('loaded save uses current standard preferences',not classic['no_timer'] and not classic['smart_enemies'])
if a.baseline:
 old=run('before-armadillo600',standard,600,binary=Path(a.baseline).resolve())
 for field in ['ram_hash','ppu_hash','pixel_hash','presentation_hash','simulation_frame']:
  check('rollback standard600 equals previous native Koopa build '+field,str(s[field])==str(old[field]))
# Replay actual title handling rather than the default legacy diagnostic input.
nav_settings=out/'title-settings-input.txt';nav_settings.write_text('40 41 D\n42 43 D\n44 45 A\n',encoding='utf-8')
title=run('title-keyboard-to-rules',standard,80,'--title-controls','--input-script',nav_settings,'--capture-ui',out/'title-keyboard-to-rules.png')
check('third title choice opens the actual rules page',title['title_controls_exercised'] and title['menu_visible'] and title['menu_tab']==4)
check('title settings choice pauses without starting a game',title['op_mode']==0 and title['core_ticks']<80)
nav_players=out/'title-two-players-input.txt';nav_players.write_text('40 41 D\n42 230 A\n',encoding='utf-8')
players=run('title-two-players',standard,260,'--title-controls','--input-script',nav_players)
check('title second choice starts native two-player mode',players['title_controls_exercised'] and players['title_players']==1 and players['op_mode']==1)
check('held accept never opens settings during gameplay',not players['menu_visible'] and players['core_ticks']==260)
result={'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':len(checks),'failures':sum(not x['passed']for x in checks),'cases':checks,'real_process_checks':True,'physical_gamepad_tested':False}
(out/'gameplay-frontend-checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('RESULT',result['checks'],'checks,',result['failures'],'failures',flush=True)
raise SystemExit(1 if result['failures']else 0)
