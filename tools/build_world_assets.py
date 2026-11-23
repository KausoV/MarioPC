"""Prepare the supplied Telegram scenery with deterministic alpha and PNG embedding.

Rebuild-only dependencies: Pillow, numpy, ffmpeg and ffprobe. The game runs from
embedded PNG bytes through its existing SDL3 DLL; it reads no scenery files.
Original supplied files remain byte-for-byte intact in assets/world/original.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

MOVIES = [
    ("tree", "xtcr9sxtcr9sxtcr", 180),
    ("butterfly", "d9ddbcd9ddbcd9dd", 32),
    ("grass", "rekz2rekz2rekz2r", 64),
    ("cloud", "crdn93crdn93crdn", 150),
    ("shrub", "k3mmfhk3mmfhk3mm", 132),
]
STILLS = [("cloud-still-1", 1, 180, None), ("cloud-still-2", 4, 208, None),
          ("mountain-far", 2, None, 1100), ("mountain-near", 3, None, 1060)]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def white_alpha(image: Image.Image, cloud: bool) -> Image.Image:
    rgb = np.asarray(image.convert("RGB"), dtype=np.float32)
    # H264/JPEG white is slightly bluish. Median border samples identify the
    # actual matte rather than assuming a mathematically pure white background.
    corners = np.concatenate((rgb[:4, :16].reshape(-1,3), rgb[-4:, :16].reshape(-1,3),
                              rgb[:4,-16:].reshape(-1,3), rgb[-4:,-16:].reshape(-1,3)))
    matte = np.median(corners, axis=0)
    distance = np.max(np.abs(rgb - matte), axis=2)
    if cloud:
        # Flood only exterior near-white. Enclosed bright cloud bodies remain
        # opaque; cloud highlights are not removed with a luminance-only key.
        eligible = Image.fromarray(np.uint8(distance <= 10) * 255, "L").copy()
        w,h = eligible.size
        for point in ((0,0),(w-1,0),(0,h-1),(w-1,h-1)):
            if eligible.getpixel(point) == 255:
                ImageDraw.floodfill(eligible,point,128)
        body = Image.fromarray(np.uint8(np.asarray(eligible) != 128) * 255,"L")
        # Opening clears detached H264 fringe blocks without cutting highlights.
        body = body.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
        alpha = np.asarray(body.filter(ImageFilter.GaussianBlur(1.2)),dtype=np.float32)/255
        # Two pixels inside the silhouette, every highlight stays fully opaque.
        core = np.asarray(body.filter(ImageFilter.MinFilter(7))) > 0
        alpha[core] = 1
    else:
        # Leaf holes, stems and butterfly antennae need true per-pixel alpha.
        alpha = np.clip((distance-12)/98,0,1)
    alpha[alpha < 0.018] = 0
    safe = np.maximum(alpha,0.001)[...,None]
    clean = np.clip((rgb-(1-alpha[...,None])*matte)/safe,0,255)
    clean[alpha == 0] = 0
    rgba = np.dstack((np.uint8(np.rint(clean)),np.uint8(np.rint(alpha*255))))
    return Image.fromarray(rgba,"RGBA")


def grass_alpha(image: Image.Image) -> Image.Image:
    rgb = np.asarray(image.convert("RGB"),dtype=np.float32)
    high = np.max(rgb,axis=2); low = np.min(rgb,axis=2)
    # This MP4 contains a baked grey checkerboard. Its neutral pixels have no
    # vegetation chroma; green and yellow leaves remain, including dark stems.
    plant = (rgb[...,1] >= rgb[...,0]-3) & (rgb[...,1] > rgb[...,2]+7)
    alpha = np.clip((high-low-8)/20,0,1) * plant
    alpha[alpha < 0.03] = 0
    # Remove the neutral checker component from fractional leaf-edge pixels.
    grey = np.clip(low,48,75)[...,None]
    clean = np.clip((rgb-(1-alpha[...,None])*grey)/np.maximum(alpha[...,None],0.001),0,255)
    clean[alpha == 0] = 0
    return Image.fromarray(np.dstack((np.uint8(np.rint(clean)),np.uint8(np.rint(alpha*255)))),"RGBA")


def clean_movie(path: Path, name: str) -> tuple[list[Image.Image],dict]:
    metadata = json.loads(subprocess.check_output(["ffprobe","-v","quiet","-show_streams","-of","json",str(path)]))["streams"][0]
    w,h = metadata["width"],metadata["height"]
    if w*h > 4096*4096: raise ValueError("Unexpected movie dimensions")
    raw = subprocess.check_output(["ffmpeg","-v","error","-i",str(path),"-f","rawvideo","-pix_fmt","rgb24","pipe:1"])
    frame_bytes = w*h*3
    if len(raw)%frame_bytes: raise ValueError("Incomplete movie frame")
    frames=[]
    for start in range(0,len(raw),frame_bytes):
        image=Image.frombytes("RGB",(w,h),raw[start:start+frame_bytes])
        frames.append(grass_alpha(image) if name == "grass" else white_alpha(image,name == "cloud"))
    return frames,{"width":w,"height":h,"frames":len(frames),"fps":metadata["avg_frame_rate"],"duration":metadata.get("duration")}


def union_bounds(frames: list[Image.Image]) -> tuple[int,int,int,int]:
    bounds=[image.getchannel("A").getbbox() for image in frames]
    if any(b is None for b in bounds):raise ValueError("Matting removed an entire source object")
    return (max(0,min(b[0] for b in bounds)-2),max(0,min(b[1] for b in bounds)-2),
            min(frames[0].width,max(b[2] for b in bounds)+2),min(frames[0].height,max(b[3] for b in bounds)+2))


def prepare(frames: list[Image.Image], height: int|None, width: int|None, mountain: bool=False) -> tuple[list[Image.Image],tuple]:
    bounds=union_bounds(frames);cw,ch=bounds[2]-bounds[0],bounds[3]-bounds[1]
    scale=height/ch if height else width/cw
    size=(max(1,round(cw*scale)),max(1,round(ch*scale)))
    result=[]
    for image in frames:
        # Pillow resamples RGBA through premultiplied RGBa, preventing black
        # outlines around transparent leaves after shrinking the source frames.
        out=image.crop(bounds).convert("RGBa").resize(size,Image.Resampling.LANCZOS).convert("RGBA")
        pixels=np.asarray(out).copy();a=pixels[...,3].astype(np.float32)
        if mountain:
            # The supplied cutouts end at straight bottom/side crop lines.
            # Hide those edges smoothly; near terrain covers the remaining base.
            xx=np.minimum(np.arange(size[0]),np.arange(size[0])[::-1])
            yy=np.arange(size[1])[::-1]
            edge=np.clip(xx/24,0,1)[None,:]
            base=np.clip(yy/55,0,1)[:,None]
            a*=edge*base
        pixels[...,3]=np.uint8(np.rint(a));pixels[pixels[...,3] < 5]=0
        result.append(Image.fromarray(pixels,"RGBA"))
    return result,bounds


def contact(frames: list[Image.Image],name: str,path: Path) -> None:
    indices=[0] if len(frames)==1 else [0,5,10,15,20,29]
    cw=max(224,min(400,frames[0].width+24));ch=max(190,min(360,frames[0].height+38))
    canvas=Image.new("RGB",(cw*len(indices),ch),(66,130,164));draw=ImageDraw.Draw(canvas)
    for col,index in enumerate(indices):
        image=frames[index].copy();image.thumbnail((cw-12,ch-30),Image.Resampling.LANCZOS)
        tile=Image.new("RGBA",(cw,ch),(66,130,164,255));tile.alpha_composite(image,((cw-image.width)//2,ch-8-image.height))
        canvas.paste(tile.convert("RGB"),(col*cw,0));draw.text((col*cw+6,5),f"{name} source frame {index}",fill=(246,243,231))
    canvas.save(path)


def write_header(path:Path,packed:bytes,assets:list[dict],offsets:list[tuple]) -> None:
    lines=["// Generated by tools/build_world_assets.py. Supplied object cutouts only.",
           "// Every frame is a lossless PNG with genuine alpha; no runtime files.",
           "#pragma once","#include <cstdint>","","namespace mario::world_asset_data {",
           "struct Asset { unsigned short width,height,first,count; };",
           "struct Frame { std::uint32_t offset,size; };","inline constexpr Asset Assets[] = {"]
    for asset in assets:lines.append(f"    {{{asset['width']},{asset['height']},{asset['first']},{asset['frames']}}}, // {asset['name']}")
    lines.extend(["};","inline constexpr Frame Frames[] = {"])
    lines.extend(f"    {{{offset},{size}}}," for offset,size in offsets)
    lines.extend(["};","inline constexpr unsigned char Packed[] = {"])
    for at in range(0,len(packed),32):lines.append("    "+",".join(str(b) for b in packed[at:at+32])+",")
    lines.extend(["};","} // namespace mario::world_asset_data",""])
    path.write_text("\n".join(lines),encoding="utf-8",newline="\n")


def main() -> None:
    parser=argparse.ArgumentParser();parser.add_argument("--sources",type=Path,default=Path.home()/"Downloads/Telegram Desktop")
    parser.add_argument("--project",type=Path,default=Path(__file__).resolve().parents[1]);args=parser.parse_args()
    dest=args.project/"assets/world";original=dest/"original";prepared=dest/"prepared";docs=args.project/"docs/world-assets";generated=args.project/"generated"
    for folder in (original,prepared,docs,generated):folder.mkdir(parents=True,exist_ok=True)
    packed=bytearray();offsets=[];assets=[];records=[];frame_total=0
    for name,source,height,width in [(n,f"Gemini_Generated_Gif_{token}.gif.mp4",h,None) for n,token,h in MOVIES]+[(n,f"photo_{index}_2026-10-12_15-33-08.jpg",h,w) for n,index,h,w in STILLS]:
        source_path=args.sources/source;copy=original/source
        if source_path.resolve()!=copy.resolve():shutil.copyfile(source_path,copy)
        if sha(source_path)!=sha(copy):raise ValueError("Source copy changed bytes")
        if source.endswith("mp4"):
            frames,metadata=clean_movie(copy,name)
            if len(frames)!=30:raise ValueError("Expected supplied 30-frame movie")
        else:
            im=Image.open(copy).convert("RGB");frames=[white_alpha(im,name.startswith("cloud"))];metadata={"width":im.width,"height":im.height,"frames":1}
        frames,bounds=prepare(frames,height,width,name.startswith("mountain"))
        assets.append({"name":name,"width":frames[0].width,"height":frames[0].height,"first":frame_total,"frames":len(frames)})
        assetdir=prepared/name;assetdir.mkdir(exist_ok=True)
        for index,frame in enumerate(frames):
            buffer=io.BytesIO();frame.save(buffer,format="PNG",compress_level=6);png=buffer.getvalue()
            # A PNG round-trip must preserve the prepared alpha and RGB exactly.
            if Image.open(io.BytesIO(png)).convert("RGBA").tobytes()!=frame.tobytes():raise ValueError("PNG round-trip failed")
            (assetdir/f"{index:02}.png").write_bytes(png);offsets.append((len(packed),len(png)));packed.extend(png);frame_total+=1
        contact(frames,name,docs/f"{name}-contact.png")
        movie_frames=frames+frames[-2:0:-1] if len(frames)>1 else frames
        rgb=[]
        for frame in movie_frames:
            base=Image.new("RGBA",frame.size,(75,140,175,255));base.alpha_composite(frame);rgb.append(base.convert("RGB"))
        if len(rgb)>1:rgb[0].save(docs/f"{name}-loop.gif",save_all=True,append_images=rgb[1:],duration=100,loop=0)
        corner_max=max(max(frame.getpixel(p)[3] for p in ((0,0),(frame.width-1,0),(0,frame.height-1),(frame.width-1,frame.height-1))) for frame in frames)
        if corner_max:raise ValueError(f"Opaque rectangle corner remained in {name}")
        records.append({"name":name,"original":source,"sha256":sha(copy),"source":metadata,"bounds":list(bounds),"prepared":list(frames[0].size),"frames":len(frames),"loop":"pingpong 0..29..1, 58 samples at10FPS" if len(frames)>1 else "static","matte":"chroma removes baked neutral checkerboard" if name=="grass" else "exterior-connected white with opaque interior" if name.startswith("cloud") else "soft near-white key preserving leaf holes","alpha_corner_max":max(max(frame.getpixel(p)[3] for p in ((0,0),(frame.width-1,0),(0,frame.height-1),(frame.width-1,frame.height-1))) for frame in frames)})
    header=generated/"world_asset_data.h";write_header(header,bytes(packed),assets,offsets)
    manifest={"objects":len(assets),"prepared_frames":frame_total,"packed_png_bytes":len(packed),"header_bytes":header.stat().st_size,"decoded_cache_limit_bytes":8*1024*1024,"decoded_cache_limit_entries":24,"character_changed":False,"runtime_files_required":False,"source_files_byte_exact":True,"assets":records}
    (docs/"assets.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps({k:v for k,v in manifest.items() if k!='assets'},indent=2))
    print(json.dumps(assets,indent=2))

if __name__=="__main__":main()
