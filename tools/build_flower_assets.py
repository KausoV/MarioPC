#!/usr/bin/env python3
"""Extract the supplied Fireflower GIF into an embedded, offline RGBA animation.

Pillow and NumPy are preparation dependencies only. Frame order and durations
are unchanged. The flower uses a single crop and scale throughout its cycle.
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

DEFAULT_SOURCE = (Path(__file__).resolve().parents[1] / 'assets/flower/original/Fireflower.gif')
WIDTH = HEIGHT = 64
CONTENT = 60


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def connected(mask: np.ndarray, seed: tuple[int, int]) -> np.ndarray:
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
            if 0 <= next_y < height:
                candidates = mask[next_y, left:right] & ~seen[next_y, left:right]
                starts = candidates & ~np.concatenate(([False], candidates[:-1]))
                pending.extend((left + int(i), next_y) for i in np.flatnonzero(starts))
    return seen


def key_magenta(image: Image.Image) -> Image.Image:
    rgb = np.asarray(image.convert("RGB")).astype(np.float32)
    if image.size != (640, 360):
        raise ValueError("Source framing changed; recalibrate the fixed matte regions")
    red, green, blue = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    chroma = np.minimum(red, blue) - green
    foreground = chroma < 55
    # The real stem ends above the magenta floor. This source-specific region
    # removes dither speckles and the long cast shadow, without editing motion.
    foreground[318:] = False
    binary = np.asarray(Image.fromarray(foreground.astype(np.uint8) * 255)
                        .filter(ImageFilter.MedianFilter(3))) != 0
    mask = connected(binary, (320, 155))
    if mask.sum() < 20000:
        raise ValueError("The flower seed did not recover its head, leaves and stem")
    # Retain detached, warm tongues of fire in the fixed head region. Connected
    # component size excludes random quantized matte pixels and floor detail.
    pending = binary & ~mask
    pending[240:] = False
    pending[:, :180] = False
    pending[:, 465:] = False
    while pending.any():
        y, x = np.argwhere(pending)[0]
        component = connected(pending, (int(x), int(y)))
        pending &= ~component
        warm = (red - blue > 50) & (green - blue > 5) & (green > 80)
        if component.sum() >= 24 and (component & warm).sum() >= component.sum() * .55:
            mask |= component
    matte = Image.fromarray(mask.astype(np.uint8) * 255)
    # Estimate the clean background per row from areas outside the object.
    # This lets antialiased flames retain fractional opacity instead of a pink
    # hard contour. Warm interiors, eyes and green leaves remain opaque.
    background = np.median(np.concatenate((rgb[:, 20:140], rgb[:, 500:620]), axis=1), axis=1)[:, None, :]
    key_delta = np.maximum(45, np.minimum(background[:, :, 0], background[:, :, 2]) - background[:, :, 1])
    alpha = np.clip((key_delta - chroma) / key_delta, 0, 1)
    interior = np.asarray(matte.filter(ImageFilter.MinFilter(3))) != 0
    alpha[interior] = 1
    contour = np.asarray(matte.filter(ImageFilter.GaussianBlur(.45))).astype(np.float32) / 255
    alpha *= np.minimum(mask.astype(np.float32), contour)
    corrected = (rgb - (1 - alpha[:, :, None]) * background) / np.maximum(alpha[:, :, None], .01)
    rgba = np.dstack((np.clip(corrected, 0, 255).astype(np.uint8), np.round(alpha * 255).astype(np.uint8)))
    rgba[rgba[:, :, 3] < 2] = 0
    return Image.fromarray(rgba)


def prepare(source: Image.Image, crop: tuple[int, int, int, int]) -> Image.Image:
    scaled = source.crop(crop).convert("RGBa").resize((CONTENT, CONTENT), Image.Resampling.LANCZOS).convert("RGBA")
    output = Image.new("RGBA", (WIDTH, HEIGHT))
    output.alpha_composite(scaled, (2, 3))
    rgba = np.asarray(output).copy()
    rgba[rgba[:, :, 3] < 2] = 0
    # The keyed GIF contains reflected magenta on its very thin leaf edges.
    # Neutralize only contour pixels; source interiors and fire colors remain.
    edge = np.asarray(output.getchannel("A").filter(ImageFilter.MinFilter(5))) < 240
    rgb = rgba[:, :, :3].astype(np.int16)
    spill = np.minimum(rgb[:, :, 0], rgb[:, :, 2]) - rgb[:, :, 1]
    pink = edge & (spill > 15)
    rgb[:, :, 2][pink] -= spill[pink]
    rgb[:, :, 0][pink] -= spill[pink]
    rgba[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    output = Image.fromarray(rgba)
    a = rgba[:, :, 3]
    if a[0].any() or a[-1].any() or a[:, 0].any() or a[:, -1].any():
        raise ValueError("Flower silhouette touches the atlas border")
    return output


def encode(image: Image.Image) -> bytes:
    raw = image.tobytes()
    packed, position = bytearray(), 0
    while position < len(raw):
        color, count = raw[position:position + 4], 1
        while count < 65535 and position + (count + 1) * 4 <= len(raw) and raw[position + count * 4:position + (count + 1) * 4] == color:
            count += 1
        packed.extend(struct.pack("<H", count)); packed.extend(color)
        position += count * 4
    decoded = bytearray()
    for offset in range(0, len(packed), 6):
        decoded.extend(packed[offset + 2:offset + 6] * struct.unpack_from("<H", packed, offset)[0])
    if bytes(decoded) != raw:
        raise ValueError("RGBA RLE round-trip failed")
    return bytes(packed)


def write_array(lines: list[str], name: str, values: list[int] | bytes, dtype: str) -> None:
    lines.append(f"inline constexpr {dtype} {name}[{len(values)}] = {{")
    for start in range(0, len(values), 36):
        lines.append("    " + ",".join(str(v) for v in values[start:start + 36]) + ",")
    lines.append("};")


def stage(frame: Image.Image, color: tuple[int, int, int, int], size: int = 192) -> Image.Image:
    output = Image.new("RGBA", frame.size, color)
    output.alpha_composite(frame)
    return output.convert("RGB").resize((size, size), Image.Resampling.NEAREST)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source", type=Path, help="Optional first import; rebuilds use the project original")
    args = parser.parse_args()
    project = args.project.resolve()
    assets, docs = project / "assets" / "flower", project / "docs" / "flower"
    docs.mkdir(parents=True, exist_ok=True)
    original = assets / "original" / "Fireflower.gif"
    original.parent.mkdir(parents=True, exist_ok=True)
    source = (args.source or (original if original.exists() else DEFAULT_SOURCE)).resolve()
    if source != original.resolve():
        shutil.copyfile(source, original)
    if sha256(source) != sha256(original):
        raise ValueError("Original GIF copy changed bytes")
    keyed, durations, source_frames = [], [], []
    with Image.open(original) as gif:
        source_size, source_loop = gif.size, gif.info.get("loop")
        for index in range(gif.n_frames):
            gif.seek(index)
            source_frames.append(gif.convert("RGB"))
            keyed.append(key_magenta(gif.convert("RGBA")))
            durations.append(int(gif.info.get("duration", 100)))
    if len(keyed) != 30 or any(duration != 100 for duration in durations):
        raise ValueError("Supplied GIF frame timing changed; revise embedded metadata")
    bounds = [frame.getchannel("A").getbbox() for frame in keyed]
    union = (min(b[0] for b in bounds), min(b[1] for b in bounds), max(b[2] for b in bounds), max(b[3] for b in bounds))
    side = max(union[2] - union[0], union[3] - union[1]) + 10
    crop_x, crop_y = round(320 - side / 2), union[3] + 3 - side
    crop = (crop_x, crop_y, crop_x + side, crop_y + side)
    prepared = [prepare(frame, crop) for frame in keyed]
    packed, offsets, detail = bytearray(), [0], []
    sheet = Image.new("RGB", (1224, 1200), (28, 39, 54))
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 9), "Fire flower | all 30 supplied frames | 64x64 RGBA | one fixed crop/scale", fill="white")
    for index, (source_alpha, atlas) in enumerate(zip(keyed, prepared)):
        for folder, picture in (("source-alpha", source_alpha), ("prepared", atlas)):
            destination = assets / folder / f"flower-{index:02}.png"
            destination.parent.mkdir(parents=True, exist_ok=True)
            picture.save(destination, optimize=True)
        packed.extend(encode(atlas)); offsets.append(len(packed))
        a = np.asarray(atlas)[:, :, 3]
        ys, xs = np.where(a >= 128)
        detail.append({"frame": index, "start_ms": sum(durations[:index]), "duration_ms": durations[index],
                       "source_alpha_bounds": list(bounds[index]), "atlas_bounds": list(atlas.getchannel("A").getbbox()),
                       "solid_bounds": [int(xs.min()), int(ys.min()), int(xs.max() + 1), int(ys.max() + 1)],
                       "rgba_sha256": hashlib.sha256(atlas.tobytes()).hexdigest()})
        x, y = index % 6 * 204, 30 + index // 6 * 234
        draw.text((x + 6, y + 6), f"{index:02} | {durations[index]} ms", fill="white")
        sheet.paste(stage(atlas, (53, 71, 91, 255) if index % 2 == 0 else (223, 233, 214, 255)), (x + 6, y + 27))
    sheet.save(docs / "flower-frames.png")
    stages = [stage(frame, (53, 71, 91, 255), 256) for frame in prepared]
    stages[0].save(docs / "flower-loop.gif", save_all=True, append_images=stages[1:], duration=durations, loop=0, disposal=2)
    comparison = Image.new("RGB", (800, 432), (28, 39, 54))
    draw = ImageDraw.Draw(comparison)
    for row, frame_index in enumerate((0, 15)):
        for col, color in enumerate(((53, 71, 91, 255), (223, 233, 214, 255), (120, 190, 228, 255), (20, 19, 16, 255))):
            draw.text((col * 200 + 4, row * 216 + 4), f"frame {frame_index} / fixed transform", fill="white")
            comparison.paste(stage(prepared[frame_index], color), (col * 200 + 4, row * 216 + 24))
    comparison.save(docs / "flower-comparison.png")
    source_contact = Image.new("RGB", (1280, 1140), (28, 39, 54))
    draw = ImageDraw.Draw(source_contact)
    for index, frame in enumerate(source_frames):
        x, y = index % 5 * 256, index // 5 * 190
        draw.text((x + 2, y + 2), f"source {index:02} / {durations[index]} ms", fill="white")
        source_contact.paste(frame.resize((256, 144), Image.Resampling.LANCZOS), (x, y + 20))
    source_contact.save(docs / "flower-source-frames.png")
    lines = ["// Generated by tools/build_flower_assets.py. Do not edit.", "// RLE packets: countLE16,red,green,blue,alpha.",
             "// Supplied frame order/timing, fixed crop/scale, no magenta floor/shadow.", "#pragma once", "#include <cstdint>", "",
             "namespace mario::flower_data {", f"inline constexpr unsigned Width = {WIDTH};", f"inline constexpr unsigned Height = {HEIGHT};",
             "inline constexpr unsigned FPS = 10;", f"inline constexpr unsigned Frames = {len(prepared)};", f"inline constexpr unsigned CycleMs = {sum(durations)};"]
    write_array(lines, "DurationsMs", durations, "unsigned")
    write_array(lines, "Offsets", offsets, "std::uint32_t")
    write_array(lines, "Packed", packed, "unsigned char")
    lines.extend(("}", ""))
    header = project / "generated" / "flower_asset_data.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text("\n".join(lines), encoding="utf-8")
    manifest = {"schema": 1, "generator": "tools/build_flower_assets.py", "pillow": PILLOW_VERSION,
                "atlas_size": [WIDTH, HEIGHT], "native_sprite_size": [16, 16], "original": str(original.relative_to(project)).replace("\\", "/"),
                "original_sha256": sha256(original), "original_bytes": original.stat().st_size, "source_size": list(source_size), "source_loop": source_loop,
                "frames": len(prepared), "durations_ms": durations, "cycle_ms": sum(durations), "source_alpha_union": list(union),
                "fixed_crop": list(crop), "fixed_content_size": CONTENT, "fixed_content_offset": [2, 3],
                "packed_bytes": len(packed), "header_sha256": sha256(header), "frame_details": detail}
    (docs / "asset-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: manifest[key] for key in ("frames", "cycle_ms", "source_alpha_union", "fixed_crop", "packed_bytes", "header_sha256")}, indent=2))


if __name__ == "__main__":
    main()
