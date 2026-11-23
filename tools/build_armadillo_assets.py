#!/usr/bin/env python3
"""Extract one supplied armadillo walk for the native green Koopa presentation.

Offline build tools only: Python/Pillow/NumPy and local FFmpeg/FFprobe. The
runtime embeds this RGBA atlas; gameplay, RAM, movement and collisions stay native.
"""
from __future__ import annotations
import argparse,hashlib,json,shutil,struct,subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFilter,__version__ as PILLOW_VERSION

DEFAULT_SOURCE=(Path(__file__).resolve().parents[1] / 'assets/armadillo-enemy/original/Gemini_Generated_Gif_g6hprug6hprug6hp.gif.mp4')
DEFAULT_FFMPEG=Path(shutil.which('ffmpeg') or 'ffmpeg.exe')
WIDTH,HEIGHT=64,96
SOURCE_FPS=10
PLAYBACK_FPS=57
NATIVE_HZ=60.0988138974405
NATIVE_MICRO_HZ=60098814
SOURCE_FRAMES=tuple(range(17,28))
CROP=(60,-56,284,168)
CONTENT=62
OFFSET=(1,32)
# This polygon follows the actual source-17 segmented shell's lower rim.
# It excludes the supplied head, legs and tail rather than inventing a new shell.
SHELL_POLYGON=((118,71),(128,63),(139,57),(155,54),(177,54),(193,58),
 (207,66),(218,79),(226,99),(225,108),(214,114),(200,117),(183,115),
 (175,113),(161,115),(142,114),(127,112),(120,104))
SHELL_CROP=(116,5,228,117)
SHELL_CONTENT=60
SHELL_OFFSET=(2,34)

def sha256(path:Path)->str:return hashlib.sha256(path.read_bytes()).hexdigest()

def connected(mask:np.ndarray,seed:tuple[int,int])->np.ndarray:
 height,width=mask.shape;seen=np.zeros(mask.shape,dtype=bool);pending=[seed]
 while pending:
  x,y=pending.pop()
  if not mask[y,x] or seen[y,x]:continue
  row=mask[y]&~seen[y];ls=np.flatnonzero(~row[:x]);rs=np.flatnonzero(~row[x+1:])
  left=int(ls[-1]+1) if ls.size else 0;right=int(x+1+rs[0]) if rs.size else width
  seen[y,left:right]=True
  for ny in (y-1,y+1):
   if not 0<=ny<height:continue
   candidates=mask[ny,left:right]&~seen[ny,left:right]
   starts=candidates&~np.concatenate(([False],candidates[:-1]))
   pending.extend((left+int(i),ny) for i in np.flatnonzero(starts))
 return seen

def key_magenta(frame:np.ndarray)->Image.Image:
 rgb=frame.astype(np.float32);r,g,b=rgb.transpose(2,0,1);chroma=np.minimum(r,b)-g
 # Both brown skin and emerald armour differ from the supplied pink floor.
 # This ratio also removes the pink cast shadow while preserving dark pupils.
 candidate=chroma<10
 candidate[:40]=False;candidate[166:]=False
 candidate[:,:56]=False;candidate[:,290:]=False
 mask=connected(candidate,(166,95))
 if int(mask.sum())<9000:raise ValueError('Shell seed lost the supplied creature')
 matte=Image.fromarray(mask.astype(np.uint8)*255)
 alpha=np.minimum(mask.astype(np.float32),np.asarray(matte.filter(ImageFilter.GaussianBlur(.35)))/255)
 interior=np.asarray(matte.filter(ImageFilter.MinFilter(3)))!=0
 excess=np.maximum(0,np.minimum(r,b)-g)
 # Only a one-pixel contour loses reflected magenta; interior source colours stay.
 rgb[:,:,0]-=excess*(~interior);rgb[:,:,2]-=excess*(~interior)
 out=np.dstack((np.clip(rgb,0,255).astype(np.uint8),(alpha*255).astype(np.uint8)))
 out[out[:,:,3]<2]=0
 return Image.fromarray(out)

def registered(image:Image.Image,crop:tuple[int,...],content:int,offset:tuple[int,int])->Image.Image:
 fg=image.crop(crop).convert('RGBa').resize((content,content),Image.Resampling.LANCZOS).convert('RGBA')
 out=Image.new('RGBA',(WIDTH,HEIGHT));out.alpha_composite(fg,offset)
 rgba=np.asarray(out).copy();rgba[94:]=0;rgba[rgba[:,:,3]<2]=0
 if np.any(rgba[:32,:,3]):raise ValueError('Art escaped the native top blank rows')
 return Image.fromarray(rgba)

def extract_shell(image:Image.Image)->Image.Image:
 mask=Image.new('L',image.size);ImageDraw.Draw(mask).polygon(SHELL_POLYGON,fill=255)
 data=np.asarray(image).copy();armour=data[:,:,1].astype(np.int16)>=data[:,:,0].astype(np.int16)-3
 data[:,:,3]=np.minimum(data[:,:,3],np.asarray(mask))*armour
 data[data[:,:,3]==0]=0
 return Image.fromarray(data)

def encode(image:Image.Image)->bytes:
 raw=image.tobytes();packed=bytearray();last=raw[:4];count=0
 for at in range(0,len(raw),4):
  pixel=raw[at:at+4]
  if pixel==last and count<65535:count+=1
  else:packed.extend(struct.pack('<H',count)+last);last=pixel;count=1
 packed.extend(struct.pack('<H',count)+last)
 decoded=bytearray()
 for at in range(0,len(packed),6):decoded.extend(packed[at+2:at+6]*struct.unpack_from('<H',packed,at)[0])
 if bytes(decoded)!=raw:raise ValueError('RLE round trip changed RGBA')
 return bytes(packed)

def array(lines:list[str],name:str,values:list[int]|bytes,dtype:str)->None:
 lines.append(f'inline constexpr {dtype} {name}[{len(values)}] = {{')
 for at in range(0,len(values),36):lines.append('    '+','.join(str(v) for v in values[at:at+36])+',')
 lines.append('};')

def stage(image:Image.Image,color:tuple[int,...]=(47,64,78,255),size:tuple[int,int]=(256,384))->Image.Image:
 out=Image.new('RGBA',image.size,color);out.alpha_composite(image)
 return out.convert('RGB').resize(size,Image.Resampling.NEAREST)

def difference(a:Image.Image,b:Image.Image,legs_only:bool=False)->float:
 aa=np.asarray(a).astype(np.float32)/255;bb=np.asarray(b).astype(np.float32)/255
 aa[:,:,:3]*=aa[:,:,3:4];bb[:,:,:3]*=bb[:,:,3:4]
 if legs_only:aa,bb=aa[80:],bb[80:]
 return float(np.abs(aa-bb).mean())

def main()->None:
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[1])
 parser.add_argument('--source',type=Path);parser.add_argument('--ffmpeg',type=Path,default=DEFAULT_FFMPEG)
 args=parser.parse_args();project=args.project.resolve()
 assets=project/'assets'/'armadillo-enemy';docs=project/'docs'/'armadillo-enemy'
 docs.mkdir(parents=True,exist_ok=True)
 original=assets/'original'/'Gemini_Generated_Gif_g6hprug6hprug6hp.gif.mp4'
 original.parent.mkdir(parents=True,exist_ok=True)
 source=(args.source or (original if original.exists() else DEFAULT_SOURCE)).resolve()
 if source!=original.resolve():shutil.copyfile(source,original)
 if sha256(source)!=sha256(original):raise ValueError('Original video copy changed bytes')
 ffmpeg=args.ffmpeg.resolve();ffprobe=ffmpeg.with_name('ffprobe.exe' if ffmpeg.suffix=='.exe' else 'ffprobe')
 probe=json.loads(subprocess.run([str(ffprobe),'-v','error','-select_streams','v:0','-show_entries',
  'stream=width,height,r_frame_rate,avg_frame_rate,nb_frames,duration','-of','json',str(original)],capture_output=True,text=True,check=True).stdout)['streams'][0]
 if (probe['width'],probe['height'],probe['avg_frame_rate'],int(probe['nb_frames']))!=(320,180,'10/1',30):
  raise ValueError('Supplied clip changed framing or cadence; recalibrate')
 raw=subprocess.run([str(ffmpeg),'-v','error','-i',str(original),'-vsync','0','-f','rawvideo','-pix_fmt','rgb24','-'],capture_output=True,check=True).stdout
 frames=np.frombuffer(raw,dtype=np.uint8).reshape(-1,180,320,3)
 if len(frames)!=30:raise ValueError('Source count changed')
 keyed=[key_magenta(frames[i]) for i in SOURCE_FRAMES]
 prepared=[registered(frame,CROP,CONTENT,OFFSET) for frame in keyed]
 after=registered(key_magenta(frames[28]),CROP,CONTENT,OFFSET)
 source_shell=extract_shell(keyed[0]);shell=registered(source_shell,SHELL_CROP,SHELL_CONTENT,SHELL_OFFSET)
 packed=bytearray();offsets=[0];details=[]
 for i,(src,atlas,source_index) in enumerate(zip(keyed,prepared,SOURCE_FRAMES)):
  for folder,picture in (('source-alpha',src),('prepared',atlas)):
   target=assets/folder/f'walk-{i:02}.png';target.parent.mkdir(parents=True,exist_ok=True);picture.save(target,optimize=True)
  packed.extend(encode(atlas));offsets.append(len(packed))
  data=np.asarray(atlas);y,x=np.where(data[:,:,3]>=128)
  details.append({'frame':i,'source_frame':source_index,'source_time_seconds':source_index/SOURCE_FPS,
   'source_bounds':list(src.getchannel('A').getbbox()),'atlas_bounds':list(atlas.getchannel('A').getbbox()),
   'solid_bounds':[int(x.min()),int(y.min()),int(x.max()+1),int(y.max()+1)],
   'rgba_sha256':hashlib.sha256(atlas.tobytes()).hexdigest()})
 shell_offset=len(packed);shell_encoded=encode(shell);packed.extend(shell_encoded)
 source_shell.save(assets/'source-alpha'/'shell.png',optimize=True);shell.save(assets/'prepared'/'shell.png',optimize=True)
 lines=['// Generated by tools/build_armadillo_assets.py. Do not edit.',
  '// Cosmetic green walking Koopa ID0 replacement only. RLE countLE16,R,G,B,A.',
  '// Chronological source frames17..27; fixed isotropic crop; source LEFT.',
  '// Static shell is the same supplied creature upper armour; native states drive it.',
  '#pragma once','#include <cstdint>','','namespace mario::armadillo_data {',
  f'inline constexpr unsigned Width = {WIDTH};',f'inline constexpr unsigned Height = {HEIGHT};',
  f'inline constexpr unsigned WalkFrames = {len(prepared)};',f'inline constexpr unsigned SourceFrameRate = {SOURCE_FPS};',
  f'inline constexpr unsigned FrameRate = {PLAYBACK_FPS};',f'inline constexpr unsigned ShellOffset = {shell_offset};',
  f'inline constexpr unsigned ShellBytes = {len(shell_encoded)};']
 array(lines,'WalkOffsets',offsets,'std::uint32_t');array(lines,'Packed',packed,'unsigned char');lines.extend(('}',''))
 header=project/'generated'/'armadillo_enemy_asset_data.h';header.parent.mkdir(parents=True,exist_ok=True);header.write_text('\n'.join(lines),encoding='utf-8')
 contact=Image.new('RGB',(6*192,2*310),(28,39,54));draw=ImageDraw.Draw(contact)
 for i,frame in enumerate(prepared+[shell]):
  x,y=i%6*192,i//6*310;draw.text((x+3,y+3),f'{i:02} / source {SOURCE_FRAMES[i]}' if i<len(prepared) else 'Same armour: shell',fill='white')
  contact.paste(stage(frame,size=(192,288)),(x,y+18))
 contact.save(docs/'armadillo-frames.png')
 source_contact=Image.new('RGB',(5*320,6*202),(28,39,54));draw=ImageDraw.Draw(source_contact)
 for i,frame in enumerate(frames):
  x,y=i%5*320,i//5*202;draw.text((x+3,y+3),f'source {i:02}',fill='white');source_contact.paste(Image.fromarray(frame),(x,y+20))
 source_contact.save(docs/'armadillo-source-frames.png')
 loop=[stage(frame) for frame in prepared];durations=[100]*len(loop)
 loop[0].save(docs/'armadillo-loop.gif',save_all=True,append_images=loop[1:],duration=durations,loop=0,disposal=2)
 runtime_ticks=116
 runtime_indices=[(tick*PLAYBACK_FPS*1000000//NATIVE_MICRO_HZ)%len(prepared) for tick in range(runtime_ticks)]
 runtime=[loop[index] for index in runtime_indices]
 runtime_durations=[int(round((tick+1)*1000/NATIVE_HZ/10)-round(tick*1000/NATIVE_HZ/10))*10 for tick in range(runtime_ticks)]
 runtime[0].save(docs/'armadillo-runtime-loop.gif',save_all=True,append_images=runtime[1:],duration=runtime_durations,loop=0,disposal=2)
 large=[]
 for frame in keyed:
  fg=frame.crop(CROP).convert('RGBa').resize((512,512),Image.Resampling.LANCZOS).convert('RGBA')
  bg=Image.new('RGBA',fg.size,(47,64,78,255));bg.alpha_composite(fg);large.append(bg.convert('RGB'))
 large[0].save(docs/'armadillo-keyed-loop.gif',save_all=True,append_images=large[1:],duration=durations,loop=0,disposal=2)
 seam=Image.new('RGB',(5*256,412),(28,39,54));draw=ImageDraw.Draw(seam)
 for n,i in enumerate((9,10,0,1,2)):
  draw.text((n*256+4,4),f'frame {i:02} / source {SOURCE_FRAMES[i]}',fill='white');seam.paste(stage(prepared[i]),(n*256,24))
 seam.save(docs/'armadillo-loop-seam.png')
 comparison=Image.new('RGB',(4*256,2*408),(28,39,54));draw=ImageDraw.Draw(comparison)
 for row,frame in enumerate((prepared[0],shell)):
  for col,color in enumerate(((47,64,78,255),(214,232,197,255),(113,190,235,255),(19,18,17,255))):
   draw.text((col*256+4,row*408+4),'Walk' if row==0 else 'Same source: shell',fill='white');comparison.paste(stage(frame,color),(col*256,row*408+20))
 comparison.save(docs/'armadillo-comparison.png')
 hoof=[]
 for source_index in range(24,30):
  alpha=np.asarray(key_magenta(frames[source_index]))[:,:,3]>=128
  ys,xs=np.where(alpha[162:166,80:180])
  if len(xs)<40:raise ValueError('Measured planted near-front paw lost contact')
  hoof.append({'source_frame':source_index,'ground_contact_pixels':len(xs),'source_x':float(xs.mean()+80)})
 t=np.array([v['source_frame'] for v in hoof]);x=np.array([v['source_x'] for v in hoof]);slope,intercept=np.polyfit(t,x,1)
 prediction=slope*t+intercept;r2=float(1-((x-prediction)**2).sum()/((x-x.mean())**2).sum())
 native_hoof_dx=float(slope*CONTENT/(CROP[2]-CROP[0])/4);body_speed=.5*NATIVE_HZ
 cadence={'method':'Ground-contact centroid regression, planted near-front paw source frames24..29',
  'ground_contact_source_y':[162,165],'samples':hoof,'source_pixels_per_source_frame':float(slope),
  'regression_r_squared':r2,'native_pixels_per_source_frame':native_hoof_dx,'native_body_pixels_per_tick':.5,
  'native_tick_rate':NATIVE_HZ,'native_body_pixels_per_second':body_speed,
  'foot_compensation_at_source_10fps_native_pixels_per_second':native_hoof_dx*SOURCE_FPS,
  'matching_cosmetic_frame_rate':body_speed/native_hoof_dx,'chosen_cosmetic_frame_rate':PLAYBACK_FPS,
  'chosen_foot_compensation_native_pixels_per_second':native_hoof_dx*PLAYBACK_FPS,
  'remaining_measured_slip_percent':abs(1-native_hoof_dx*PLAYBACK_FPS/body_speed)*100,
  'changes':'Only cosmetic playback clock; source, native speed, collision and behavior unchanged'}
 adjacent=[difference(prepared[i],prepared[i+1]) for i in range(len(prepared)-1)]
 legdiffs=[difference(prepared[i],prepared[i+1],True) for i in range(len(prepared)-1)]
 seam_diff=difference(prepared[-1],prepared[0]);seam_legs=difference(prepared[-1],prepared[0],True)
 silhouette0=np.asarray(prepared[0])[:,:,3]>=128;silhouette1=np.asarray(after)[:,:,3]>=128
 manifest={'schema':1,'generator':'tools/build_armadillo_assets.py','pillow':PILLOW_VERSION,
  'original':str(original.relative_to(project)).replace('\\','/'),'original_sha256':sha256(original),
  'original_bytes':original.stat().st_size,'source_probe':probe,'source_decoded_frames':len(frames),
  'source_frame_indices':list(SOURCE_FRAMES),'source_direction':'left','walk_frames':len(prepared),
  'source_frame_rate':SOURCE_FPS,'source_cycle_ms':len(prepared)*1000/SOURCE_FPS,
  'playback_frame_rate':PLAYBACK_FPS,'playback_cycle_ms':len(prepared)*1000/PLAYBACK_FPS,'cadence_measurement':cadence,
  'runtime_preview':{'native_ticks':runtime_ticks,'source_frame_indices':runtime_indices,'gif_durations_ms':runtime_durations,
   'gif_duration_ms':sum(runtime_durations),'native_duration_ms':runtime_ticks*1000/NATIVE_HZ},
  'atlas_size':[WIDTH,HEIGHT],'native_canvas_size':[16,24],'fixed_crop':list(CROP),
  'fixed_content_size':[CONTENT,CONTENT],'fixed_content_offset':list(OFFSET),
  'registration':'One fixed isotropic camera transform; no independent fits, frame reordering, pingpong or crossfade',
  'shadow_removed':True,'packed_bytes':len(packed),'header_sha256':sha256(header),
  'shell_offset':shell_offset,'shell_bytes':len(shell_encoded),'shell_source_frame':SOURCE_FRAMES[0],
  'shell_extraction_polygon':SHELL_POLYGON,'shell_fixed_crop':SHELL_CROP,'shell_content_size':SHELL_CONTENT,
  'shell_content_offset':SHELL_OFFSET,'shell_type':'Static source armour only; native core drives shell states',
  'frame_details':details,'seam':{'normalized_premultiplied_rgba_difference':seam_diff,
   'adjacent_mean':float(np.mean(adjacent)),'adjacent_max':float(max(adjacent)),'legs_difference':seam_legs,
   'legs_adjacent_max':float(max(legdiffs)),
   'first_vs_next_cycle_silhouette_iou':float((silhouette0&silhouette1).sum()/(silhouette0|silhouette1).sum()),
   'first_vs_next_cycle_rgba_difference':difference(prepared[0],after)}}
 (docs/'asset-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 (docs/'cadence-checks.json').write_text(json.dumps(cadence,indent=2)+'\n',encoding='utf-8')
 magenta=[]
 for image in prepared+[shell]:
  data=np.asarray(image).astype(np.int16);r,g,b=data[:,:,:3].transpose(2,0,1)
  magenta.append(int(((data[:,:,3]>=128)&(np.minimum(r,b)>g+20)).sum()))
 validation={'schema':1,'source_frame_count':len(frames),'chronological_source_frames':list(SOURCE_FRAMES),
  'decoded_rgba_frames':len(prepared)+1,'all_top_eight_native_rows_blank':all(not np.any(np.asarray(i)[:32,:,3]) for i in prepared+[shell]),
  'all_rle_frames_roundtrip':True,'max_magenta_spill_opaque_pixels':max(magenta),
  'walk_contact_alpha_bottom_rows':sorted(set(v['atlas_bounds'][3]-1 for v in details)),
  'walk_contact_solid_bottom_rows':sorted(set(v['solid_bounds'][3]-1 for v in details)),
  'shell_alpha_bounds':list(shell.getchannel('A').getbbox()),
  'seam_within_ordinary_adjacent_frame_max':seam_diff<=max(adjacent),
  'leg_seam_within_ordinary_adjacent_frame_max':seam_legs<=max(legdiffs)}
 if validation['max_magenta_spill_opaque_pixels']:raise ValueError('Opaque magenta floor contamination survived')
 (docs/'asset-checks.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8')
 (assets/'README.md').write_text('''# Supplied armadillo: green walking Koopa presentation

The original 320×180,10-fps supplied video is preserved byte for byte. Source
frames17 through27 form one chronological 11-frame walk facing left. A fixed
isotropic224-square crop and62-square resize preserve proportions, bottom
registered at atlas row93 in a64×96 canvas. No per-frame fit, pose interpolation,
pingpong, crossfade or synthetic extra frames are used. Pink floor, shadow and
reflected magenta contour are removed; emerald armour and brown skin are kept.

The temporary shell presentation is a fixed polygon cut from the same supplied
armour in source17, without head, legs or tail. There is no new shell animation
in this supplied walk. The renderer uses this same static image for native
stationary/kicked/dead shell states and handles native +2px shell Y placement.

At native enemy speed8/16 pixel per tick, the measured planted paw requires
approximately57 cosmetic source frames/s for ground-relative foot motion. The
game still runs on the same60.0988-Hz native clock with unchanged enemy speed.
The source10-fps preview and native-tick-sampled runtime preview are separate.

Rebuild offline with Python,Pillow,NumPy and local FFmpeg/FFprobe:
`python tools/build_armadillo_assets.py --ffmpeg PATH`. The project copy in
original/ is selected automatically, so no external user download path is
needed after delivery. The game embeds generated RGBA and needs no build tools.
''',encoding='utf-8')
 print(json.dumps({k:manifest[k] for k in ('walk_frames','packed_bytes','header_sha256','seam')},indent=2))

if __name__=='__main__':main()
