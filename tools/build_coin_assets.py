#!/usr/bin/env python3
"""Prepare the supplied Bitcoin GIF as offline game presentation artwork.

Python/Pillow/NumPy are build tools only. Every frame shares one source crop,
scale and rotation axis. The original emblem and GIF timing are preserved.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, __version__ as PILLOW_VERSION

DEFAULT_SOURCE = (Path(__file__).resolve().parents[1] / 'assets/coins/original/Bitcoin.gif')
WIDTH, HEIGHT = 64, 64
SPRITE_WIDTH, SPRITE_HEIGHT = 32, 64


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def remove_green(image: Image.Image) -> Image.Image:
    rgb = np.asarray(image.convert("RGB")).astype(np.float32)
    border = np.concatenate((rgb[:4].reshape(-1, 3), rgb[-4:].reshape(-1, 3),
                             rgb[:, :4].reshape(-1, 3), rgb[:, -4:].reshape(-1, 3)))
    matte = np.median(border, axis=0)
    chroma = rgb[:, :, 1] - np.maximum(rgb[:, :, 0], rgb[:, :, 2])
    matte_chroma = max(float(matte[1] - max(matte[0], matte[2])), 1)
    alpha = 1 - np.clip(chroma / matte_chroma, 0, 1)
    # GIF quantization uses several green palette colors, including dark green
    # with substantial blue. An absolute chroma test removes all such matte
    # colors while the warm gold coin always has red >= green - 25.
    pure_green = chroma > 25
    alpha[pure_green] = 0
    alpha[alpha < .06] = 0
    color = np.clip((rgb - (1 - alpha[:, :, None]) * matte) /
                    np.maximum(alpha[:, :, None], .001), 0, 255)
    # Remove residual reflected matte without touching warm golden material.
    spill = np.maximum(color[:, :, 1] - np.maximum(color[:, :, 0], color[:, :, 2]), 0)
    color[:, :, 1] -= spill
    rgba = np.dstack((np.round(color).astype(np.uint8), np.round(alpha * 255).astype(np.uint8)))
    rgba[alpha == 0] = 0
    return Image.fromarray(rgba)


def prepare(source: Image.Image, crop: tuple[int, int, int, int], width: int, height: int, diameter: int) -> Image.Image:
    resized = source.crop(crop).convert("RGBa").resize((diameter, diameter), Image.Resampling.LANCZOS).convert("RGBA")
    output = Image.new("RGBA", (width, height))
    output.alpha_composite(resized, ((width - diameter) // 2, (height - diameter) // 2))
    rgba = np.asarray(output).copy()
    rgba[rgba[:, :, 3] == 0] = 0
    output = Image.fromarray(rgba)
    a = np.asarray(output)[:, :, 3]
    if a[0].any() or a[-1].any() or a[:, 0].any() or a[:, -1].any():
        raise ValueError("Coin silhouette touches an atlas border")
    return output


def encode(image: Image.Image) -> bytes:
    raw = image.tobytes()
    packed = bytearray()
    position = 0
    while position < len(raw):
        color = raw[position:position + 4]
        count = 1
        while count < 65535 and position + (count + 1) * 4 <= len(raw) and raw[position + count * 4:position + (count + 1) * 4] == color:
            count += 1
        packed.extend(struct.pack("<H", count))
        packed.extend(color)
        position += count * 4
    decoded = bytearray()
    for offset in range(0, len(packed), 6):
        count = struct.unpack_from("<H", packed, offset)[0]
        decoded.extend(packed[offset + 2:offset + 6] * count)
    if bytes(decoded) != raw:
        raise ValueError("RGBA RLE round-trip failed")
    return bytes(packed)


def write_array(lines: list[str], name: str, values: list[int] | bytes, dtype: str = "unsigned char", stride: int = 36) -> None:
    lines.append(f"inline constexpr {dtype} {name}[{len(values)}] = {{")
    for start in range(0, len(values), stride):
        lines.append("    " + ",".join(str(v) for v in values[start:start + stride]) + ",")
    lines.append("};")


def contact(frames: list[Image.Image], destination: Path, title: str, durations: list[int]) -> None:
    columns, factor = 6, 4
    cell_w, cell_h = 276, 290
    output = Image.new("RGB", (columns * cell_w, 30 + ((len(frames) + columns - 1) // columns) * cell_h), (28, 39, 54))
    draw = ImageDraw.Draw(output)
    draw.text((8, 9), title + " | original frame order", fill=(242, 246, 252))
    for i, frame in enumerate(frames):
        x, y = i % columns * cell_w, 30 + i // columns * cell_h
        draw.text((x + 6, y + 6), f"frame {i} | {durations[i]} ms", fill=(228, 236, 246))
        stage = Image.new("RGBA", frame.size, (53, 71, 91, 255))
        stage.alpha_composite(frame)
        stage = stage.convert("RGB").resize((frame.width * factor, frame.height * factor), Image.Resampling.NEAREST)
        output.paste(stage, (x + (cell_w - stage.width) // 2, y + 28))
    output.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source", type=Path, help="Optional first-import GIF; rebuilds use the project original")
    args = parser.parse_args()
    project = args.project.resolve()
    assets, docs = project / "assets" / "coins", project / "docs" / "coins"
    docs.mkdir(parents=True, exist_ok=True)
    original = assets / "original" / "Bitcoin.gif"
    original.parent.mkdir(parents=True, exist_ok=True)
    selected = (args.source or (original if original.is_file() else DEFAULT_SOURCE)).resolve()
    if selected != original.resolve():
        shutil.copyfile(selected, original)
    if sha256(selected) != sha256(original):
        raise ValueError("Original GIF copy changed bytes")
    originals, frames, durations = [], [], []
    with Image.open(original) as gif:
        source_size = gif.size
        loop = gif.info.get("loop")
        for i in range(gif.n_frames):
            gif.seek(i)
            image = gif.convert("RGBA")
            originals.append(image.copy())
            frames.append(remove_green(image))
            duration = int(gif.info.get("duration", 100))
            if duration <= 0:
                raise ValueError("GIF contains a non-positive frame duration")
            durations.append(duration)
    bounds = [frame.getchannel("A").getbbox() for frame in frames]
    if not frames or any(b is None for b in bounds):
        raise ValueError("GIF contains an empty coin frame")
    union = (min(b[0] for b in bounds), min(b[1] for b in bounds), max(b[2] for b in bounds), max(b[3] for b in bounds))
    center_x, center_y = (union[0] + union[2]) / 2, (union[1] + union[3]) / 2
    side = max(union[2] - union[0], union[3] - union[1]) + 8
    crop_x, crop_y = round(center_x - side / 2), round(center_y - side / 2)
    crop = (crop_x, crop_y, crop_x + side, crop_y + side)
    backgrounds = [prepare(frame, crop, WIDTH, HEIGHT, 62) for frame in frames]
    sprites = [prepare(frame, crop, SPRITE_WIDTH, SPRITE_HEIGHT, 30) for frame in frames]
    packed, sprite_packed = bytearray(), bytearray()
    offsets, sprite_offsets = [0], [0]
    detail = []
    for i, (source, background, sprite) in enumerate(zip(frames, backgrounds, sprites)):
        for folder, image in (("source-alpha", source), ("background", background), ("sprite", sprite)):
            path = assets / folder / f"coin-{i:02}.png"
            path.parent.mkdir(parents=True, exist_ok=True)
            image.save(path, optimize=True)
        packed.extend(encode(background)); offsets.append(len(packed))
        sprite_packed.extend(encode(sprite)); sprite_offsets.append(len(sprite_packed))
        detail.append({"frame": i, "start_ms": sum(durations[:i]), "duration_ms": durations[i],
                       "source_alpha_bounds": list(bounds[i]),
                       "background_bounds": list(background.getchannel("A").getbbox()),
                       "sprite_bounds": list(sprite.getchannel("A").getbbox()),
                       "background_rgba_sha256": hashlib.sha256(background.tobytes()).hexdigest(),
                       "sprite_rgba_sha256": hashlib.sha256(sprite.tobytes()).hexdigest()})
    contact(backgrounds, docs / "background-frames.png", "Bitcoin coin | 64x64 background atlas", durations)
    contact(sprites, docs / "sprite-frames.png", "Bitcoin coin | 32x64 sprite atlas, circular 30px content", durations)
    stages = []
    for frame in backgrounds:
        stage = Image.new("RGBA", (64, 64), (41, 57, 77, 255))
        stage.alpha_composite(frame)
        stages.append(stage.convert("RGB").resize((256, 256), Image.Resampling.NEAREST))
    stages[0].save(docs / "coin-loop.gif", save_all=True, append_images=stages[1:], duration=durations, loop=0, disposal=2)
    header = project / "generated" / "coin_asset_data.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/build_coin_assets.py. Do not edit.",
             "// Each RLE packet is countLE16,red,green,blue,alpha.",
             "// One fixed source crop and scale; original Bitcoin emblem and GIF timing.",
             "#pragma once", "#include <cstdint>", "", "namespace mario::coin_data {",
             f"inline constexpr unsigned Frames = {len(frames)};", "inline constexpr unsigned Width = 64;",
             "inline constexpr unsigned Height = 64;", "inline constexpr unsigned SpriteWidth = 32;",
             "inline constexpr unsigned SpriteHeight = 64;",
             f"inline constexpr unsigned CycleMs = {sum(durations)};",
             f"inline constexpr unsigned FPS = {round(1000 / (sum(durations) / len(durations)))};"]
    write_array(lines, "DurationsMs", durations, "unsigned")
    write_array(lines, "Packed", packed)
    write_array(lines, "Offsets", offsets, "std::uint32_t", 10)
    write_array(lines, "SpritePacked", sprite_packed)
    write_array(lines, "SpriteOffsets", sprite_offsets, "std::uint32_t", 10)
    lines.extend(["} // namespace mario::coin_data", ""])
    header.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    first, last = np.asarray(backgrounds[0]), np.asarray(backgrounds[-1])
    first_alpha, last_alpha = first[:, :, 3] >= 64, last[:, :, 3] >= 64
    overlap = float(np.sum(first_alpha & last_alpha) / np.sum(first_alpha | last_alpha))
    active = first_alpha | last_alpha
    seam_rgb_mean = float(np.mean(np.abs(first[:, :, :3].astype(float)[active] - last[:, :, :3].astype(float)[active])))
    manifest = {"schema": 1, "generator": "tools/build_coin_assets.py", "pillow_version": PILLOW_VERSION,
                "source": {"path": original.relative_to(project).as_posix(), "sha256": sha256(original),
                           "bytes": original.stat().st_size, "size": list(source_size), "frames": len(frames), "loop": loop},
                "durations_ms": durations, "cycle_ms": sum(durations), "source_frame_order": list(range(len(frames))),
                "background_canvas": [64, 64], "sprite_canvas": [32, 64], "sprite_content_square": [30, 30],
                "source_union_alpha_bounds": list(union), "fixed_source_crop": list(crop),
                "fixed_axis_source": [center_x, center_y], "background_crop_scale": 62 / side, "sprite_crop_scale": 30 / side,
                "alignment": "All frames use one square crop, scale and center; no per-frame fitting or stretching",
                "matte": "Soft green chroma removal, edge unmix, residual green despill, premultiplied Lanczos",
                "emblem": "User-provided Bitcoin emblem preserved",
                "loop": "Original forward 30-frame GIF order and timing; no pingpong or synthetic redraw",
                "loop_limitations": "Source last frame is slightly tilted versus first; minor original lighting/pose seam retained",
                "prepared_first_last_alpha_iou": overlap, "prepared_first_last_rgb_mean_absolute_difference": seam_rgb_mean,
                "rle_roundtrip_verified": True, "packed_bytes": len(packed), "sprite_packed_bytes": len(sprite_packed),
                "runtime_external_assets_required": False,
                "generated_header": {"path": header.relative_to(project).as_posix(), "sha256": sha256(header), "bytes": header.stat().st_size},
                "frames_detail": detail}
    (docs / "asset-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"frames": len(frames), "cycle_ms": sum(durations), "crop": crop,
                      "background_packed_bytes": len(packed), "sprite_packed_bytes": len(sprite_packed),
                      "header_sha256": sha256(header), "header": str(header)}, indent=2))


if __name__ == "__main__":
    main()
