#!/usr/bin/env python3
"""Prepare the supplied red/green mushroom GIFs for offline game presentation.

Pillow and NumPy are build tools only. Each animation retains its original
frame order/timing and one fixed crop/scale. Native item movement is untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, __version__ as PILLOW_VERSION

DEFAULT_SOURCE = (Path(__file__).resolve().parents[1] / 'assets/mushrooms/original')
WIDTH = HEIGHT = 64
ATLAS_CONTENT = 60


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def connected(mask: np.ndarray, seed: tuple[int, int]) -> np.ndarray:
    """Four-connected scanline flood fill, avoiding a per-pixel Python queue."""
    height, width = mask.shape
    seen = np.zeros(mask.shape, dtype=bool)
    pending = [seed]
    while pending:
        x, y = pending.pop()
        if not mask[y, x] or seen[y, x]:
            continue
        row = mask[y] & ~seen[y]
        left_stop = np.flatnonzero(~row[:x])
        right_stop = np.flatnonzero(~row[x + 1:])
        left = int(left_stop[-1] + 1) if left_stop.size else 0
        right = int(x + 1 + right_stop[0]) if right_stop.size else width
        seen[y, left:right] = True
        for next_y in (y - 1, y + 1):
            if not 0 <= next_y < height:
                continue
            candidates = mask[next_y, left:right] & ~seen[next_y, left:right]
            starts = candidates & ~np.concatenate(([False], candidates[:-1]))
            pending.extend((left + int(i), next_y) for i in np.flatnonzero(starts))
    return seen


def remove_magenta(image: Image.Image, kind: str) -> Image.Image:
    rgb = np.asarray(image.convert("RGB")).astype(np.float32)
    if image.size != (640, 360):
        raise ValueError("The supplied GIF framing changed; recalibrate its matte ROI")
    red, green, blue = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    # Both GIFs use several dithered magenta palette entries. White spots,
    # green cloth and warm red material remain on the foreground side.
    foreground = (np.minimum(red, blue) - green < 35) | (blue < red * .58)
    if kind == "red":
        # The red source includes a purple cast shadow below/right of its stem.
        # In this fixed lower ROI the real cream/brown stem has G >= B while
        # shadow and matte have B > G. The cap stays outside this constraint.
        foreground[245:] &= (green - blue)[245:] >= -6
    binary = np.asarray(Image.fromarray(foreground.astype(np.uint8) * 255)
                        .filter(ImageFilter.MedianFilter(3))) != 0
    # Select an interior cap seed nearest its fixed center. A few quantized
    # pixels at that exact center alternate between palette entries; selecting
    # another interior pixel affects segmentation only, never image alignment.
    ys, xs = np.where(binary[90:155, 290:350])
    if not len(xs):
        raise ValueError("The foreground cap ROI is empty")
    closest = int(np.argmin((xs + 290 - 320) ** 2 + (ys + 90 - 130) ** 2))
    silhouette = connected(binary, (int(xs[closest] + 290), int(ys[closest] + 90)))
    if silhouette.sum() < 20000:
        raise ValueError("Foreground seed did not recover the mushroom silhouette")
    # Opaque mushrooms have no interior holes. This also eliminates keyed GIF
    # dither speckles without changing the cap spots, eyes or source RGB detail.
    silhouette = ~connected(~silhouette, (0, 0))
    matte = Image.fromarray(silhouette.astype(np.uint8) * 255)
    alpha = np.minimum(np.asarray(matte), np.asarray(matte.filter(ImageFilter.GaussianBlur(.5))))
    inner = np.asarray(matte.filter(ImageFilter.MinFilter(3))).astype(np.float32) / 255
    # Decontaminate the inner contour only. Interior red, green, cream, black
    # eyes and white spots retain the original colors and lighting.
    blue_spill = np.maximum(0, rgb[:, :, 2] - np.maximum(rgb[:, :, 0] * .30, rgb[:, :, 1]))
    rgb[:, :, 2] -= blue_spill * (1 - inner) * .8
    rgba = np.dstack((np.clip(rgb, 0, 255).astype(np.uint8), alpha))
    rgba[alpha == 0] = 0
    return Image.fromarray(rgba)


def prepare(source: Image.Image, crop: tuple[int, int, int, int], kind: str) -> Image.Image:
    scaled = source.crop(crop).convert("RGBa").resize((ATLAS_CONTENT, ATLAS_CONTENT), Image.Resampling.LANCZOS).convert("RGBA")
    output = Image.new("RGBA", (WIDTH, HEIGHT))
    # One constant row compensates the red source's rounding so both supplied
    # stems end on solid atlas row 61 throughout their breathing animations.
    output.alpha_composite(scaled, (2, 3 if kind == "red" else 2))
    rgba = np.asarray(output).copy()
    rgba[rgba[:, :, 3] < 2] = 0
    # Premultiplied downsampling can average a few reflected-purple source
    # contour pixels into one opaque atlas pixel. Neutralize that spill only
    # inside a three-pixel contour band; cap/stem interiors retain source hues.
    eroded = np.asarray(output.getchannel("A").filter(ImageFilter.MinFilter(7)))
    rgb = rgba[:, :, :3].astype(np.int16)
    chroma = np.minimum(rgb[:, :, 0], rgb[:, :, 2]) - rgb[:, :, 1]
    purple = (eroded < 240) & (chroma > 25) & (rgb[:, :, 2] > rgb[:, :, 0] * .65)
    rgb[:, :, 0][purple] -= chroma[purple]
    rgb[:, :, 2][purple] -= chroma[purple]
    red_reflection = ((eroded < 240) & (rgb[:, :, 0] > rgb[:, :, 1] * 1.7) &
                      (rgb[:, :, 2] > rgb[:, :, 1] * 1.7) & (rgb[:, :, 2] > 60) & (rgb[:, :, 0] > 90))
    rgb[:, :, 2][red_reflection] = rgb[:, :, 1][red_reflection]
    rgba[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    output = Image.fromarray(rgba)
    alpha = rgba[:, :, 3]
    if alpha[0].any() or alpha[-1].any() or alpha[:, 0].any() or alpha[:, -1].any():
        raise ValueError("Mushroom silhouette touches an atlas border")
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
        packed.extend(struct.pack("<H", count)); packed.extend(color)
        position += count * 4
    decoded = bytearray()
    for offset in range(0, len(packed), 6):
        count = struct.unpack_from("<H", packed, offset)[0]
        decoded.extend(packed[offset + 2:offset + 6] * count)
    if bytes(decoded) != raw:
        raise ValueError("RGBA RLE round-trip failed")
    return bytes(packed)


def write_array(lines: list[str], name: str, values: list[int] | bytes, dtype: str) -> None:
    lines.append(f"inline constexpr {dtype} {name}[{len(values)}] = {{")
    for start in range(0, len(values), 36):
        lines.append("    " + ",".join(str(v) for v in values[start:start + 36]) + ",")
    lines.append("};")


def contact(frames: list[Image.Image], destination: Path, kind: str, durations: list[int]) -> None:
    columns, cell_w, cell_h = 6, 204, 234
    output = Image.new("RGB", (columns * cell_w, 30 + ((len(frames) + columns - 1) // columns) * cell_h), (28, 39, 54))
    draw = ImageDraw.Draw(output)
    draw.text((8, 9), f"{kind.title()} mushroom | 64x64 RGBA | supplied order and timing", fill=(242, 246, 252))
    for i, frame in enumerate(frames):
        x, y = i % columns * cell_w, 30 + i // columns * cell_h
        draw.text((x + 6, y + 6), f"{i:02} | {durations[i]} ms", fill=(228, 236, 246))
        stage = Image.new("RGBA", frame.size, (53, 71, 91, 255) if i % 2 == 0 else (223, 233, 214, 255))
        stage.alpha_composite(frame)
        output.paste(stage.convert("RGB").resize((192, 192), Image.Resampling.NEAREST), (x + 6, y + 27))
    output.save(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source-dir", type=Path, help="Optional first-import directory; rebuilds use project originals")
    args = parser.parse_args()
    project = args.project.resolve()
    assets, docs = project / "assets" / "mushrooms", project / "docs" / "mushrooms"
    docs.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/build_mushroom_assets.py. Do not edit.",
             "// RLE packets: countLE16,red,green,blue,alpha.",
             "// Supplied GIF order/timing; one fixed crop/scale per kind, no floor shadow.",
             "#pragma once", "#include <cstdint>", "", "namespace mario::mushroom_data {",
             "inline constexpr unsigned Width = 64;", "inline constexpr unsigned Height = 64;",
             "inline constexpr unsigned FPS = 10;"]
    manifest = {"schema": 1, "generator": "tools/build_mushroom_assets.py", "pillow": PILLOW_VERSION,
                "atlas_size": [WIDTH, HEIGHT], "native_sprite_size": [16, 16], "items": {}}
    comparison = Image.new("RGB", (800, 432), (28, 39, 54))
    comparison_draw = ImageDraw.Draw(comparison)
    for index, kind in enumerate(("red", "green")):
        original = assets / "original" / f"{kind}grib.gif"
        original.parent.mkdir(parents=True, exist_ok=True)
        selected = ((args.source_dir / original.name) if args.source_dir else
                    (original if original.is_file() else DEFAULT_SOURCE / original.name)).resolve()
        if selected != original.resolve():
            shutil.copyfile(selected, original)
        if sha256(selected) != sha256(original):
            raise ValueError("Original GIF copy changed bytes")
        frames, durations = [], []
        with Image.open(original) as gif:
            source_size, source_loop = gif.size, gif.info.get("loop")
            for i in range(gif.n_frames):
                gif.seek(i)
                frames.append(remove_magenta(gif.convert("RGBA"), kind))
                duration = int(gif.info.get("duration", 100))
                if duration <= 0:
                    raise ValueError("GIF contains a non-positive frame duration")
                durations.append(duration)
        if any(duration != 100 for duration in durations):
            raise ValueError("FPS metadata must be recalibrated for altered timing")
        bounds = [frame.getchannel("A").getbbox() for frame in frames]
        union = (min(b[0] for b in bounds), min(b[1] for b in bounds),
                 max(b[2] for b in bounds), max(b[3] for b in bounds))
        side = max(union[2] - union[0], union[3] - union[1]) + 8
        crop_x, crop_y = round(320 - side / 2), union[3] + 2 - side
        crop = (crop_x, crop_y, crop_x + side, crop_y + side)
        prepared = [prepare(frame, crop, kind) for frame in frames]
        packed, offsets, detail = bytearray(), [0], []
        for i, (source, atlas) in enumerate(zip(frames, prepared)):
            for folder, image in (("source-alpha", source), ("prepared", atlas)):
                path = assets / folder / kind / f"{kind}-{i:02}.png"
                path.parent.mkdir(parents=True, exist_ok=True); image.save(path, optimize=True)
            packed.extend(encode(atlas)); offsets.append(len(packed))
            a = np.asarray(atlas)[:, :, 3]
            ys, xs = np.where(a >= 128)
            detail.append({"frame": i, "start_ms": sum(durations[:i]), "duration_ms": durations[i],
                           "source_alpha_bounds": list(bounds[i]), "atlas_bounds": list(atlas.getchannel("A").getbbox()),
                           "solid_bounds": [int(xs.min()), int(ys.min()), int(xs.max() + 1), int(ys.max() + 1)],
                           "rgba_sha256": hashlib.sha256(atlas.tobytes()).hexdigest()})
        name = kind.title()
        lines.extend((f"inline constexpr unsigned {name}Frames = {len(frames)};",
                      f"inline constexpr unsigned {name}CycleMs = {sum(durations)};"))
        write_array(lines, name + "DurationsMs", durations, "unsigned")
        write_array(lines, name + "Offsets", offsets, "std::uint32_t")
        write_array(lines, name + "Packed", packed, "unsigned char")
        contact(prepared, docs / f"{kind}-frames.png", kind, durations)
        stages = []
        for atlas in prepared:
            stage = Image.new("RGBA", atlas.size, (53, 71, 91, 255)); stage.alpha_composite(atlas)
            stages.append(stage.convert("RGB").resize((256, 256), Image.Resampling.NEAREST))
        stages[0].save(docs / f"{kind}-loop.gif", save_all=True, append_images=stages[1:], duration=durations, loop=0, disposal=2)
        for background_index, background in enumerate(((53, 71, 91, 255), (223, 233, 214, 255))):
            x = index * 400 + background_index * 200
            stage = Image.new("RGBA", (64, 64), background); stage.alpha_composite(prepared[0])
            comparison.paste(stage.convert("RGB").resize((192, 192), Image.Resampling.NEAREST), (x + 4, 24))
            comparison_draw.text((x + 4, 4), f"{kind} | {'dark' if background_index == 0 else 'light'}", fill="white")
            stage = Image.new("RGBA", (64, 64), background); stage.alpha_composite(prepared[15])
            comparison.paste(stage.convert("RGB").resize((192, 192), Image.Resampling.NEAREST), (x + 4, 232))
            comparison_draw.text((x + 4, 216), "frame 15 / same fixed transform", fill="white")
        manifest["items"][kind] = {"original": str(original.relative_to(project)).replace("\\", "/"),
                                  "original_sha256": sha256(original), "original_bytes": original.stat().st_size,
                                  "source_size": list(source_size), "source_loop": source_loop,
                                  "frames": len(frames), "durations_ms": durations, "cycle_ms": sum(durations),
                                  "source_alpha_union": list(union), "fixed_crop": list(crop),
                                  "fixed_content_size": ATLAS_CONTENT, "fixed_content_offset": [2, 3 if kind == "red" else 2],
                                  "packed_bytes": len(packed), "frame_details": detail}
    lines.extend(("}", ""))
    header = project / "generated" / "mushroom_asset_data.h"
    header.parent.mkdir(parents=True, exist_ok=True); header.write_text("\n".join(lines), encoding="utf-8")
    manifest["header_sha256"] = sha256(header)
    comparison.save(docs / "mushroom-comparison.png")
    (docs / "asset-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"header_sha256": manifest["header_sha256"],
                      "items": {k: {"frames": v["frames"], "cycle_ms": v["cycle_ms"], "union": v["source_alpha_union"],
                                     "crop": v["fixed_crop"], "packed_bytes": v["packed_bytes"]} for k, v in manifest["items"].items()}}, indent=2))


if __name__ == "__main__":
    main()
