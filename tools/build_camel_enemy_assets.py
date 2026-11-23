#!/usr/bin/env python3
"""Prepare the supplied camel/beetle walk for the Goomba presentation only.

Build dependencies: Python, Pillow, NumPy and local FFmpeg/FFprobe. The game
embeds RGBA data and has no Python, video decoder or Internet dependency.
No gameplay, collision, speed, score, timing or native enemy data is modified.
"""
from __future__ import annotations
import argparse, hashlib, json, shutil, struct, subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, __version__ as PILLOW_VERSION

DEFAULT_SOURCE=(Path(__file__).resolve().parents[1] / 'assets/camel-enemy/original/gemini_generated_video_1fc0b0f0.mp4')
DEFAULT_FFMPEG=Path(shutil.which('ffmpeg') or 'ffmpeg.exe')
WIDTH,HEIGHT=64,96
SOURCE_FPS=24
PLAYBACK_FPS=115
NATIVE_HZ=60.0988138974405
NATIVE_MICRO_HZ=60098814
SOURCE_FRAMES=tuple(range(82,110))
# One square crop and one isotropic scale for every frame, including the seam.
# Shell anchors vary by less than half an atlas pixel in this source segment;
# keeping the camera transform fixed preserves natural gait and body movement.
CROP=(276,6,970,700)
CONTENT=62
OFFSET=(1,32)

def sha256(path:Path)->str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def connected(mask:np.ndarray, seed:tuple[int,int])->np.ndarray:
    height,width=mask.shape; seen=np.zeros(mask.shape,dtype=bool); pending=[seed]
    while pending:
        x,y=pending.pop()
        if not mask[y,x] or seen[y,x]:continue
        row=mask[y]&~seen[y]
        ls=np.flatnonzero(~row[:x]);rs=np.flatnonzero(~row[x+1:])
        left=int(ls[-1]+1) if ls.size else 0
        right=int(x+1+rs[0]) if rs.size else width
        seen[y,left:right]=True
        for ny in (y-1,y+1):
            if not 0<=ny<height:continue
            candidates=mask[ny,left:right]&~seen[ny,left:right]
            starts=candidates&~np.concatenate(([False],candidates[:-1]))
            pending.extend((left+int(i),ny) for i in np.flatnonzero(starts))
    return seen

def key_green(frame:np.ndarray)->Image.Image:
    rgb=frame.astype(np.float32)
    r,g,b=rgb.transpose(2,0,1);chroma=g-np.maximum(r,b)
    # Dark neutral pupils and armor joints belong to the creature too. A
    # channel-ratio guard rejects the dark green floor shadow without cutting
    # holes in these naturally black source details.
    mask=(chroma<25)&((r>30)|(g<=np.maximum(r,b)*1.2+3))
    mask[:140]=False;mask[710:]=False;mask[:,:270]=False;mask[:,1030:]=False
    binary=np.asarray(Image.fromarray(mask.astype(np.uint8)*255).filter(ImageFilter.MedianFilter(3)))!=0
    # The creature is a single connected foreground. Its green cast shadow,
    # matte floor and random compression speckles do not touch this shell seed.
    mask=connected(binary,(700,380))
    if int(mask.sum())<120000:raise ValueError('The shell seed lost the creature')
    matte=Image.fromarray(mask.astype(np.uint8)*255)
    interior=np.asarray(matte.filter(ImageFilter.MinFilter(5)))!=0
    alpha=np.minimum(mask.astype(np.float32),np.asarray(matte.filter(ImageFilter.GaussianBlur(.4)))/255)
    # This creature has no green material. Remove reflected matte on the narrow
    # contour only; the shell, eyes, muzzle and legs retain their source colors.
    excess=np.maximum(0,g-np.maximum(r,b))
    rgb[:,:,1]-=excess*(~interior)
    out=np.dstack((np.clip(rgb,0,255).astype(np.uint8),(alpha*255).astype(np.uint8)))
    out[out[:,:,3]<2]=0
    return Image.fromarray(out)

def prepare(image:Image.Image)->Image.Image:
    scaled=image.crop(CROP).convert('RGBa').resize((CONTENT,CONTENT),Image.Resampling.LANCZOS).convert('RGBA')
    out=Image.new('RGBA',(WIDTH,HEIGHT));out.alpha_composite(scaled,OFFSET)
    data=np.asarray(out).copy();data[data[:,:,3]<2]=0
    # Native Goomba's top eight OAM rows are blank. Never draw into this space.
    if np.any(data[:32,:,3]):raise ValueError('Walk art escaped the native footprint')
    return Image.fromarray(data)

def squash(image:Image.Image)->Image.Image:
    # A single cosmetic stomp pose of the same supplied creature. One bounded
    # compression is applied to the entire image; no legs or body parts stretch
    # independently. It ends on the original foot line, in native bottom 8 rows.
    content=image.crop((0,32,64,94)).convert('RGBa').resize((64,30),Image.Resampling.LANCZOS).convert('RGBA')
    out=Image.new('RGBA',(WIDTH,HEIGHT));out.alpha_composite(content,(0,64))
    data=np.asarray(out).copy();data[data[:,:,3]<2]=0
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

def stage(image:Image.Image,color:tuple[int,int,int,int],size:tuple[int,int]=(256,384))->Image.Image:
    out=Image.new('RGBA',image.size,color);out.alpha_composite(image)
    return out.convert('RGB').resize(size,Image.Resampling.NEAREST)

def difference(a:Image.Image,b:Image.Image,legs_only:bool=False)->float:
    aa=np.asarray(a).astype(np.float32)/255;bb=np.asarray(b).astype(np.float32)/255
    aa[:,:,:3]*=aa[:,:,3:4];bb[:,:,:3]*=bb[:,:,3:4]
    if legs_only:aa,bb=aa[75:],bb[75:]
    return float(np.abs(aa-bb).mean())

def main()->None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source',type=Path)
    parser.add_argument('--ffmpeg',type=Path,default=DEFAULT_FFMPEG)
    args=parser.parse_args();project=args.project.resolve()
    assets=project/'assets'/'camel-enemy';docs=project/'docs'/'camel-enemy'
    docs.mkdir(parents=True,exist_ok=True)
    original=assets/'original'/'gemini_generated_video_1fc0b0f0.mp4'
    original.parent.mkdir(parents=True,exist_ok=True)
    source=(args.source or (original if original.exists() else DEFAULT_SOURCE)).resolve()
    if source!=original.resolve():shutil.copyfile(source,original)
    if sha256(source)!=sha256(original):raise ValueError('Original video copy changed bytes')
    ffmpeg=args.ffmpeg.resolve();ffprobe=ffmpeg.with_name('ffprobe.exe' if ffmpeg.suffix=='.exe' else 'ffprobe')
    probe=json.loads(subprocess.run([str(ffprobe),'-v','error','-select_streams','v:0','-show_entries',
        'stream=width,height,r_frame_rate,avg_frame_rate,nb_frames,duration','-of','json',str(original)],capture_output=True,text=True,check=True).stdout)['streams'][0]
    if (probe['width'],probe['height'],probe['avg_frame_rate'],int(probe['nb_frames']))!=(1280,720,'24/1',240):
        raise ValueError('Video framing or timing changed; recalibrate this source')
    # Passthrough is essential: FFmpeg's default CFR can duplicate frame zero.
    raw=subprocess.run([str(ffmpeg),'-v','error','-i',str(original),'-vsync','0','-f','rawvideo','-pix_fmt','rgb24','-'],capture_output=True,check=True).stdout
    frames=np.frombuffer(raw,dtype=np.uint8).reshape(-1,720,1280,3)
    if len(frames)!=240:raise ValueError('Source frame count changed')
    keyed=[key_green(frames[i]) for i in SOURCE_FRAMES]
    prepared=[prepare(frame) for frame in keyed]
    after=prepare(key_green(frames[SOURCE_FRAMES[-1]+1]))
    stomp=squash(prepared[0])
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
    squash_offset=len(packed);squash_encoded=encode(stomp);packed.extend(squash_encoded)
    stomp.save(assets/'prepared'/'squash.png',optimize=True)
    lines=['// Generated by tools/build_camel_enemy_assets.py. Do not edit.',
        '// Cosmetic Goomba replacement only. RLE countLE16,R,G,B,A.',
        '// Chronological source frames 82..109, fixed crop/scale, source LEFT.',
        '#pragma once','#include <cstdint>','','namespace mario::camel_data {',
        f'inline constexpr unsigned Width = {WIDTH};',f'inline constexpr unsigned Height = {HEIGHT};',
        f'inline constexpr unsigned WalkFrames = {len(prepared)};',
        f'inline constexpr unsigned SourceFrameRate = {SOURCE_FPS};',
        f'inline constexpr unsigned FrameRate = {PLAYBACK_FPS};',
        f'inline constexpr unsigned SquashOffset = {squash_offset};',f'inline constexpr unsigned SquashBytes = {len(squash_encoded)};']
    array(lines,'WalkOffsets',offsets,'std::uint32_t');array(lines,'Packed',packed,'unsigned char');lines.extend(('}',''))
    header=project/'generated'/'camel_enemy_asset_data.h';header.parent.mkdir(parents=True,exist_ok=True);header.write_text('\n'.join(lines),encoding='utf-8')
    contact=Image.new('RGB',(8*160,4*254),(28,39,54));draw=ImageDraw.Draw(contact)
    for i,frame in enumerate(prepared):
        x,y=i%8*160,i//8*254;draw.text((x+3,y+3),f'{i:02} / source {SOURCE_FRAMES[i]}',fill='white')
        contact.paste(stage(frame,(47,64,78,255),(160,240)),(x,y+14))
    contact.save(docs/'camel-frames.png')
    source_contact=Image.new('RGB',(7*256,4*230),(28,39,54));draw=ImageDraw.Draw(source_contact)
    for i,source_index in enumerate(SOURCE_FRAMES):
        x,y=i%7*256,i//7*230
        draw.text((x+3,y+3),f'source {source_index} / {source_index/24:.3f}s',fill='white')
        source_contact.paste(Image.fromarray(frames[source_index]).crop(CROP).resize((216,216),Image.Resampling.LANCZOS),(x+20,y+14))
    source_contact.save(docs/'camel-source-frames.png')
    loop=[stage(frame,(47,64,78,255)) for frame in prepared]
    # GIF durations are 10ms-quantized; alternate 40/50ms to retain source time.
    durations=[int(round((i+1)*1000/24/10)-round(i*1000/24/10))*10 for i in range(len(loop))]
    loop[0].save(docs/'camel-loop.gif',save_all=True,append_images=loop[1:],duration=durations,loop=0,disposal=2)
    # Sample the cosmetic 115-FPS source clock at actual native game ticks.
    # The 88-tick preview covers six source cycles and ends just before phase
    # zero, giving a clean loop without crossfade or reverse playback. GIF
    # timestamps are necessarily quantized to 10 ms; cumulative rounding keeps
    # the average native presentation cadence rather than making 115-FPS GIF
    # frames that a GIF player cannot represent.
    runtime_ticks=88
    runtime_indices=[(tick*PLAYBACK_FPS*1000000//NATIVE_MICRO_HZ)%len(prepared) for tick in range(runtime_ticks)]
    runtime=[loop[index] for index in runtime_indices]
    runtime_durations=[int(round((tick+1)*1000/NATIVE_HZ/10)-round(tick*1000/NATIVE_HZ/10))*10 for tick in range(runtime_ticks)]
    runtime[0].save(docs/'camel-runtime-loop.gif',save_all=True,append_images=runtime[1:],duration=runtime_durations,loop=0,disposal=2)
    large=[]
    for frame in keyed:
        fg=frame.crop(CROP).convert('RGBa').resize((512,512),Image.Resampling.LANCZOS).convert('RGBA')
        bg=Image.new('RGBA',fg.size,(47,64,78,255));bg.alpha_composite(fg);large.append(bg.convert('RGB'))
    large[0].save(docs/'camel-keyed-loop.gif',save_all=True,append_images=large[1:],duration=durations,loop=0,disposal=2)
    seam=Image.new('RGB',(5*256,412),(28,39,54));draw=ImageDraw.Draw(seam)
    for n,i in enumerate((26,27,0,1,2)):
        draw.text((n*256+4,4),f'frame {i:02} / source {SOURCE_FRAMES[i]}',fill='white')
        seam.paste(stage(prepared[i],(47,64,78,255)),(n*256,24))
    seam.save(docs/'camel-loop-seam.png')
    comparison=Image.new('RGB',(4*256,2*408),(28,39,54));draw=ImageDraw.Draw(comparison)
    for row,frame in enumerate((prepared[0],stomp)):
        for col,color in enumerate(((47,64,78,255),(214,232,197,255),(113,190,235,255),(19,18,17,255))):
            draw.text((col*256+4,row*408+4),'Walk' if row==0 else 'Same source: stomp pose',fill='white')
            comparison.paste(stage(frame,color),(col*256,row*408+20))
    comparison.save(docs/'camel-comparison.png')
    adjacent=[difference(prepared[i],prepared[i+1]) for i in range(len(prepared)-1)]
    seam_diff=difference(prepared[-1],prepared[0]);seam_legs=difference(prepared[-1],prepared[0],True)
    legdiffs=[difference(prepared[i],prepared[i+1],True) for i in range(len(prepared)-1)]
    silhouette0=np.asarray(prepared[0])[:,:,3]>=128;silhouette1=np.asarray(after)[:,:,3]>=128
    # A planted near-front hoof remains at y=683..688 in frames 86..98.
    # Measure its floor-contact centroid, excluding touchdown and liftoff. A
    # single fixed crop/scale converts source coordinates to native pixels.
    hoof=[]
    for source_index in range(86,99):
        alpha=np.asarray(keyed[source_index-SOURCE_FRAMES[0]])[:,:,3]>=128
        ys,xs=np.where(alpha[683:689,450:710])
        if len(xs)<150:raise ValueError('The measured planted hoof lost ground contact')
        hoof.append({'source_frame':source_index,'ground_contact_pixels':int(len(xs)),'source_x':float(xs.mean()+450)})
    t=np.array([v['source_frame'] for v in hoof]);x=np.array([v['source_x'] for v in hoof])
    slope,intercept=np.polyfit(t,x,1);prediction=slope*t+intercept
    r2=float(1-((x-prediction)**2).sum()/((x-x.mean())**2).sum())
    native_hoof_dx=float(slope*CONTENT/(CROP[2]-CROP[0])/4)
    body_speed=.5*NATIVE_HZ
    cadence={'method':'Ground-contact centroid regression, planted near-front hoof source frames 86..98',
        'ground_contact_source_y':[683,688],'samples':hoof,'source_pixels_per_source_frame':float(slope),
        'regression_r_squared':r2,'native_pixels_per_source_frame':native_hoof_dx,
        'native_body_pixels_per_tick':.5,'native_tick_rate':NATIVE_HZ,'native_body_pixels_per_second':body_speed,
        'foot_compensation_at_source_24fps_native_pixels_per_second':native_hoof_dx*SOURCE_FPS,
        'matching_cosmetic_frame_rate':body_speed/native_hoof_dx,
        'chosen_cosmetic_frame_rate':PLAYBACK_FPS,'chosen_foot_compensation_native_pixels_per_second':native_hoof_dx*PLAYBACK_FPS,
        'remaining_measured_slip_percent':abs(1-native_hoof_dx*PLAYBACK_FPS/body_speed)*100,
        'changes':'Only presentation playback clock; original video/RGBA, enemy speed, behavior and physics unchanged'}
    manifest={'schema':1,'generator':'tools/build_camel_enemy_assets.py','pillow':PILLOW_VERSION,
        'original':str(original.relative_to(project)).replace('\\','/'),'original_sha256':sha256(original),
        'original_bytes':original.stat().st_size,'source_probe':probe,'source_decoded_frames':len(frames),
        'source_frame_indices':list(SOURCE_FRAMES),'source_direction':'left','walk_frames':len(prepared),
        'source_frame_rate':SOURCE_FPS,'source_cycle_ms':len(prepared)*1000/SOURCE_FPS,
        'playback_frame_rate':PLAYBACK_FPS,'playback_cycle_ms':len(prepared)*1000/PLAYBACK_FPS,
        'cadence_measurement':cadence,
        'runtime_preview':{'native_tick_rate':NATIVE_HZ,'native_ticks':runtime_ticks,'source_frame_indices':runtime_indices,
                           'gif_durations_ms':runtime_durations,'gif_duration_ms':sum(runtime_durations),
                           'native_duration_ms':runtime_ticks*1000/NATIVE_HZ,'note':'Native-tick sampled, cumulative 10-ms GIF rounding'},
        'atlas_size':[WIDTH,HEIGHT],'native_canvas_size':[16,24],'native_visible_footprint':[16,16],
        'fixed_crop':list(CROP),'fixed_content_size':[CONTENT,CONTENT],'fixed_content_offset':list(OFFSET),
        'registration':'fixed camera transform; no per-frame fit, scale or pose interpolation',
        'frame_reordering':False,'ping_pong':False,'crossfade':False,'shadow_removed':True,'packed_bytes':len(packed),
        'header_sha256':sha256(header),'squash_offset':squash_offset,'squash_bytes':len(squash_encoded),
        'squash_visible_native_rows':[16,24],'frame_details':details,
        'seam':{'normalized_premultiplied_rgba_difference':seam_diff,'adjacent_mean':float(np.mean(adjacent)),
            'adjacent_max':float(max(adjacent)),'legs_difference':seam_legs,'legs_adjacent_max':float(max(legdiffs)),
            'first_vs_next_cycle_silhouette_iou':float((silhouette0&silhouette1).sum()/(silhouette0|silhouette1).sum()),
            'first_vs_next_cycle_rgba_difference':difference(prepared[0],after)}}
    (docs/'asset-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    (docs/'cadence-checks.json').write_text(json.dumps(cadence,indent=2)+'\n',encoding='utf-8')
    green_counts=[]
    for image in prepared+[stomp]:
        data=np.asarray(image).astype(np.int16)
        green_counts.append(int(((data[:,:,3]>=128)&(data[:,:,1]>data[:,:,0]+12)&(data[:,:,1]>data[:,:,2]+12)).sum()))
    validation={'schema':1,'source_frame_count':len(frames),'chronological_source_frames':list(SOURCE_FRAMES),
        'decoded_rgba_frames':len(prepared)+1,'all_walk_top_eight_native_rows_blank':all(not np.any(np.asarray(i)[:32,:,3]) for i in prepared),
        'all_walk_art_inside_native_16_by_16':all(i.size==(64,96) and not np.any(np.asarray(i)[:32,:,3]) for i in prepared),
        'squash_top_sixteen_native_rows_blank':not bool(np.any(np.asarray(stomp)[:64,:,3])),
        'max_green_spill_opaque_pixels':max(green_counts),'walk_contact_alpha_bottom_rows':sorted(set(v['atlas_bounds'][3]-1 for v in details)),
        'walk_contact_solid_bottom_rows':sorted(set(v['solid_bounds'][3]-1 for v in details)),
        'all_rle_frames_roundtrip':True,'seam_within_ordinary_adjacent_frame_max':seam_diff<=max(adjacent),
        'leg_seam_within_ordinary_adjacent_frame_max':seam_legs<=max(legdiffs)}
    if validation['max_green_spill_opaque_pixels']:raise ValueError('Opaque green matte contamination survived')
    (docs/'asset-checks.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8')
    (assets/'README.md').write_text('''# Camel/beetle Goomba presentation

The original supplied video is preserved byte for byte in `original/`.
Chronological source frames 82 through 109 form one 28-frame forward walk.
The supplied video is 24 fps. The cosmetic playback clock is 115 fps, sampled
at the game's native 60.0988-Hz presentation ticks. This preserves native enemy
movement while matching the planted hoof's backwards travel to body motion.
No ping-pong, pose interpolation or ghost crossfade is used. A fixed
square crop and isotropic scale preserve proportions. The shell stays in place
while the original legs and body move; frames are not independently fitted.

Green background, reflected contour green and cast floor shadow are removed.
Black pupils and armor joints are preserved. Walk art fits the native Goomba's
16 by 16 visible footprint inside a 16 by 24 OAM canvas. The stomp picture is
one bounded compression of this same creature. Native defeated/flip states,
movement, collisions, score and enemy timing remain the gameplay core's work.

Offline rebuilding requires local Python with Pillow and NumPy, plus FFmpeg
and FFprobe. Run `python tools/build_camel_enemy_assets.py --ffmpeg PATH` from
the project folder, where PATH points to the local FFmpeg executable; its
FFprobe companion must be alongside it. No source-download step is required.
The game embeds generated RGBA data and does not require these build tools.

Inspect `docs/camel-enemy/asset-manifest.json`, `asset-checks.json`, the complete
frame contacts, loop seam contact and loops for preparation evidence.
`cadence-checks.json` records the planted-hoof regression. The measured source
foot compensation is 6.255 native pixels/s at 24 fps, versus the unchanged
enemy's 30.049 pixels/s. Cosmetic 115-fps playback gives 29.972 pixels/s (about
0.26 percent residual measured slip). `camel-keyed-loop.gif` and `camel-loop.gif`
retain the original 24-fps reference; `camel-runtime-loop.gif` samples the
115-fps art clock at native game ticks, with cumulative GIF timestamp rounding.
''',encoding='utf-8')
    print(json.dumps({k:manifest[k] for k in ('walk_frames','source_cycle_ms','packed_bytes','header_sha256','seam')},indent=2))

if __name__=='__main__':main()
