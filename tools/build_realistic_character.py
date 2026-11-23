#!/usr/bin/env python3
"""Embed the user's walk, run and jump footage as calibrated presentation art.

Rebuild needs Python, Pillow and NumPy; the game itself needs none of them.
Original footage and supplied keyed PNGs are copied unchanged on first import.
The old cartoon artwork, gameplay core and input configuration are untouched.
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

WIDTH, HEIGHT = 64, 128
VARIANTS = ("big", "small", "crouch", "growth")
JUMP_SOURCES = tuple(i for i in range(30) if i not in (18, 19, 20, 21))
GROUPS = (("walk", tuple(range(14))), ("run", tuple(range(21))), ("jump", JUMP_SOURCES))
FRAMES = 61
DEFAULT_IMPORT = (Path(__file__).resolve().parents[1] / 'assets/realistic-character/source-alpha')


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_exact(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.resolve() != target.resolve():
        shutil.copyfile(source, target)
    if sha256(source) != sha256(target):
        raise ValueError(f"Copy did not preserve {source}")


def hsv(rgba: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    rgb = rgba[:, :, :3].astype(np.float32) / 255
    high, low = rgb.max(axis=2), rgb.min(axis=2)
    delta = high - low
    denominator = np.where(delta > 0, delta, 1)
    hue = np.zeros(high.shape, dtype=np.float32)
    candidates = (((rgb[:, :, 1] - rgb[:, :, 2]) / denominator) % 6,
                  (rgb[:, :, 2] - rgb[:, :, 0]) / denominator + 2,
                  (rgb[:, :, 0] - rgb[:, :, 1]) / denominator + 4)
    for channel, candidate in enumerate(candidates):
        selected = (delta > 0) & (rgb[:, :, channel] == high)
        hue[selected] = candidate[selected] / 6
    return hue, delta / np.maximum(high, 1e-9), high


def analyze(path: Path, action: str, source_index: int) -> dict:
    with Image.open(path) as image:
        rgba = np.asarray(image.convert("RGBA")).copy()
    opaque = Image.fromarray(np.where(rgba[:, :, 3] >= 96, 255, 0).astype(np.uint8))
    bounds = opaque.getbbox()
    if bounds is None:
        raise ValueError(f"No opaque character in {path}")
    # The GIF's green dithering left low-alpha speckles near the image edge.
    # Retain antialiasing only near the real silhouette, without eroding it.
    x0, y0, x1, y1 = bounds
    outer = (max(0, x0 - 2), max(0, y0 - 2), min(rgba.shape[1], x1 + 2), min(rgba.shape[0], y1 + 2))
    keep = np.zeros(rgba.shape[:2], dtype=bool)
    keep[outer[1]:outer[3], outer[0]:outer[2]] = True
    rgba[~keep] = 0
    rgba[rgba[:, :, 3] == 0] = 0
    image = Image.fromarray(rgba)
    bounds = image.getchannel("A").getbbox()
    assert bounds is not None
    hue, saturation, value = hsv(rgba)
    yy = np.arange(rgba.shape[0])[:, None]
    visible = rgba[:, :, 3] >= 96
    red = visible & (saturation > .30) & (value > .18) & ((hue < .035) | (hue > .95))
    cap = red & (yy < y0 + (y1 - y0) * .32)
    cy, cx = np.nonzero(cap)
    if len(cx) < 10:
        raise ValueError(f"Cannot measure cap in {path}")
    cap_bounds = (int(cx.min()), int(cy.min()), int(cx.max() + 1), int(cy.max() + 1))
    cap_width = cap_bounds[2] - cap_bounds[0]
    face_end = cap_bounds[1] + cap_width * 1.10
    blue = visible & (saturation > .25) & (hue > .52) & (hue < .76) & (yy > face_end)
    by, bx = np.nonzero(blue)
    torso = blue & (yy < cap_bounds[1] + cap_width * 1.90)
    ty, tx = np.nonzero(torso)
    if len(tx) < 10 or len(bx) < 10:
        raise ValueError(f"Cannot measure torso in {path}")
    # Upper quantile of the overall component tracks the torso rather than
    # the shoes. A frame-specific jump pivot removes recorded world movement.
    pivot_x = float(np.median(tx))
    pivot_y = float(np.quantile(by, .35))
    material = np.zeros(rgba.shape[:2], dtype=np.uint8)
    # Follow the actual red cloth instead of an estimated face-height band.
    # The shoulders rise beside the jaw in running/jumping footage; excluding
    # them by face_end left a moving red strip on the fire outfit.
    # The supplied jump GIF uses coarse color-table dithering. Work on a
    # small spatial average only for material detection; original RGBA art
    # and its placement are unchanged. Otherwise red cloth's purple/orange
    # dither samples look like protected anatomy to a pointwise hue test.
    material_image = image.convert("RGBa").filter(ImageFilter.GaussianBlur(
        max(.6, cap_width * .02))).convert("RGBA")
    material_hue, material_saturation, material_value = hsv(np.asarray(material_image))
    cloth_hue = (material_hue < .040) | (material_hue > .945)
    red_cloth = visible & cloth_hue & (material_saturation > .32) & (material_value > .09)
    brim_end = cap_bounds[3] + cap_width * .025
    material[red_cloth & (yy <= brim_end)] = 1
    # Cloth below the brim is red sleeves/collar. Warm skin, brown hair/boots,
    # the white cap badge, gloves and gold buttons have different source hues.
    material[red_cloth & (yy > brim_end) & (yy < pivot_y + cap_width * .30)] = 2
    cloth_blue = visible & (material_saturation > .25) & (material_hue > .52) & (material_hue < .76) & (yy > face_end)
    material[cloth_blue] = 3
    return {"action": action, "source_index": source_index, "path": path,
            "image": image, "material": Image.fromarray(material), "bounds": bounds,
            "cap_bounds": cap_bounds, "cap_width": cap_width,
            "pivot": (pivot_x, pivot_y), "face_end": face_end}


def rle(data: bytes, channels: int) -> bytes:
    packed = bytearray()
    position = 0
    while position < len(data):
        pixel = data[position:position + channels]
        count = 1
        while count < 65535 and position + (count + 1) * channels <= len(data) and data[position + count * channels:position + (count + 1) * channels] == pixel:
            count += 1
        packed.extend(struct.pack("<H", count))
        packed.extend(pixel)
        position += count * channels
    restored = bytearray()
    for i in range(0, len(packed), channels + 2):
        count = struct.unpack_from("<H", packed, i)[0]
        restored.extend(packed[i + 2:i + 2 + channels] * count)
    if bytes(restored) != data:
        raise ValueError("RLE round-trip failed")
    return bytes(packed)


def write_array(lines: list[str], name: str, values: list[int] | bytes, dtype: str = "unsigned char", stride: int = 36) -> None:
    lines.append(f"inline constexpr {dtype} {name}[{len(values)}] = {{")
    for i in range(0, len(values), stride):
        lines.append("    " + ",".join(str(v) for v in values[i:i + stride]) + ",")
    lines.append("};")


def contact(frames: list[Image.Image], title: str, sources: list[int], target: Path, columns: int = 7) -> None:
    cell_w, cell_h = 144, 286
    sheet = Image.new("RGB", (columns * cell_w, 30 + ((len(frames) + columns - 1) // columns) * cell_h), (30, 40, 54))
    draw = ImageDraw.Draw(sheet)
    draw.text((9, 9), title + " | 64x128 atlas at 2x", fill=(237, 242, 250))
    for i, (frame, source) in enumerate(zip(frames, sources)):
        x, y = (i % columns) * cell_w, 30 + (i // columns) * cell_h
        draw.text((x + 6, y + 5), f"frame {i} | source {source}", fill=(218, 230, 241))
        backdrop = Image.new("RGBA", (WIDTH, HEIGHT), (46, 60, 77, 255))
        backdrop.alpha_composite(frame)
        sheet.paste(backdrop.convert("RGB").resize((128, 256), Image.Resampling.NEAREST), (x + 7, y + 24))
    sheet.save(target)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source-root", type=Path, default=DEFAULT_IMPORT)
    args = parser.parse_args()
    project = args.project.resolve()
    assets = project / "assets" / "realistic-character"
    docs = project / "docs" / "realistic-character"
    docs.mkdir(parents=True, exist_ok=True)
    (project / "generated").mkdir(parents=True, exist_ok=True)
    records, copied = [], []
    originals = (("walk", assets / "original/Gemini_Generated_Gif_4eq0134eq0134eq0.gif.mp4"),
                 ("run", assets / "original/gemini_generated_video_b9d0d9ab.mp4"),
                 ("jump", assets / "original/Gemini_Generated_Gif_hmmxehhmmxehhmmx.gif"))
    for action, original in originals:
        destination = assets / "original" / original.name
        if not destination.is_file():
            copy_exact(original, destination)
        copied.append({"action": action, "path": destination.relative_to(project).as_posix(), "sha256": sha256(destination), "bytes": destination.stat().st_size})
    for action, indices in GROUPS:
        folder = assets / "source-alpha" / action
        import_folder = args.source_root / f"mario-video-{action}" / f"mario-{action}-frames" / "source-alpha"
        for index in (range(30) if action == "jump" else indices):
            destination = folder / f"{action}-{index:02}.png"
            if not destination.is_file():
                copy_exact(import_folder / destination.name, destination)
            if index in indices:
                records.append(analyze(destination, action, index))
    if len(records) != FRAMES:
        raise ValueError("Expected 61 retained source frames")
    calibration = {}
    for action, _ in GROUPS:
        samples = [r for r in records if r["action"] == action]
        calibration[action] = {"median_cap_width": float(np.median([r["cap_width"] for r in samples])),
                               "ground_pivot_y": float(np.median([r["pivot"][1] for r in samples]))}
    reference = calibration["walk"]["median_cap_width"]
    rects = []
    for record in records:
        action = record["action"]
        norm = reference / calibration[action]["median_cap_width"]
        calibration[action]["relative_scale"] = norm
        px, py = record["pivot"]
        if action != "jump":
            py = calibration[action]["ground_pivot_y"]
        record["placement_pivot"] = (px, py)
        record["relative_scale"] = norm
        b = record["bounds"]
        rects.append(((b[0] - px) * norm, (b[1] - py) * norm, (b[2] - px) * norm, (b[3] - py) * norm))
    left, top = min(b[0] for b in rects), min(b[1] for b in rects)
    right, bottom = max(b[2] for b in rects), max(b[3] for b in rects)
    envelope_w, envelope_h = right - left, bottom - top
    common = min(60 / envelope_w, 122 / envelope_h)
    # Small Mario has a 64px envelope independently of the large form's wide
    # arms. Restore the familiar ~54px small stature without per-action scale.
    ratios = (1.0, min(.675, 62 / (envelope_h * common)), min(1.0, 94 / (envelope_h * common)), .85)
    packed, materials = bytearray(), bytearray()
    offsets, material_offsets = [0], [0]
    details, groups, pivots_x, pivots_y, foot_y, frame_shifts = [], {}, [], [], [], []
    for variant_index, (variant, ratio) in enumerate(zip(VARIANTS, ratios)):
        scale = common * ratio
        pivot_x = (WIDTH - envelope_w * scale) / 2 - left * scale
        pivot_y = 127 - bottom * scale
        pivots_x.append(int(round(pivot_x)))
        pivots_y.append(int(round(pivot_y)))
        by_action = {action: [] for action, _ in GROUPS}
        raw_frames = []
        for frame_index, record in enumerate(records):
            b = record["bounds"]
            factor = record["relative_scale"] * scale
            crop = record["image"].crop(b)
            size = (max(1, round(crop.width * factor)), max(1, round(crop.height * factor)))
            resized = crop.convert("RGBa").resize(size, Image.Resampling.LANCZOS).convert("RGBA")
            source_labels = np.asarray(record["material"].crop(b))
            source_alpha = np.asarray(crop.getchannel("A"), dtype=np.float32) / 255
            # GIF dithering contains isolated keyed/protected source pixels.
            # Nearest-neighbor reduction picked them at random and produced
            # red/white and blue/red speckles in the smaller jump artwork.
            # Reduce all four material coverages with the same alpha support,
            # then choose the dominant material rather than a point sample.
            coverage = np.stack([np.asarray(Image.fromarray(
                np.where(source_labels == label, source_alpha, 0).astype(np.float32)
            ).resize(size, Image.Resampling.BILINEAR)) for label in range(4)])
            rgba = np.asarray(resized).copy()
            mask = coverage.argmax(axis=0).astype(np.uint8)
            hue, saturation, _ = hsv(rgba)
            neutral = rgba[:, :, :3].max(axis=2).astype(np.int16) - rgba[:, :, :3].min(axis=2).astype(np.int16) < 16
            # Preserve near-black facial features as well as bright white
            # gloves/badge. Relative saturation alone mislabels dark neutral
            # moustache and mouth-edge pixels as red dye.
            mask[(rgba[:, :, 3] < 48) | (saturation < .18) | neutral] = 0
            # Resampling at material borders must not transfer cloth labels
            # onto white gloves, the cap badge or warm skin and boot pixels.
            red_cloth = ((hue < .040) | (hue > .945)) & (saturation >= .32)
            mask[((mask == 1) | (mask == 2)) & ~red_cloth] = 0
            mask[(mask == 3) & ~((hue > .50) & (hue < .78))] = 0
            # A one-pixel cap/ear antialias mixture can have a red hue but
            # still be warm skin. Preserve those edge pixels explicitly.
            source_y = b[1] + (np.arange(size[1])[:, None] + .5) / factor
            head_band = (source_y > record["cap_bounds"][1] + record["cap_width"] * .45) & (
                source_y < record["face_end"])
            r, g, blue_channel = (rgba[:, :, i].astype(np.float32) for i in range(3))
            warm_skin = (r > 20) & (r >= g + 6) & (g >= blue_channel + 2) & (r < g * 2)
            mask[head_band & warm_skin] = 0
            resized, material = Image.fromarray(rgba), Image.fromarray(mask)
            px, py = record["placement_pivot"]
            x = round(pivot_x + (b[0] - px) * factor)
            y = round(pivot_y + (b[1] - py) * factor)
            if x < 1 or x + size[0] > WIDTH - 1 or y < 1 or y + size[1] > HEIGHT:
                raise ValueError(f"Clipped {variant}/{record['action']}/{record['source_index']}: {size} at {(x,y)}")
            canvas = Image.new("RGBA", (WIDTH, HEIGHT))
            canvas.alpha_composite(resized, (x, y))
            mask_canvas = Image.new("L", (WIDTH, HEIGHT))
            mask_canvas.paste(material, (x, y))
            raw_frames.append((record, canvas, mask_canvas, factor))
        grounded_bottoms = {action: max(canvas.getchannel("A").getbbox()[3]
                            for record, canvas, _, _ in raw_frames if record["action"] == action)
                            for action in ("walk", "run")}
        for frame_index, (record, canvas, mask_canvas, factor) in enumerate(raw_frames):
            action, source_index = record["action"], record["source_index"]
            initial_bounds = canvas.getchannel("A").getbbox()
            assert initial_bounds is not None
            if action in grounded_bottoms:
                ground_shift = 127 - grounded_bottoms[action]
            elif source_index <= 9 or source_index == 29:
                ground_shift = 127 - initial_bounds[3]
            else:
                ground_shift = 0
            if ground_shift:
                placed = Image.new("RGBA", (WIDTH, HEIGHT))
                placed.alpha_composite(canvas, (0, ground_shift))
                shifted_material = Image.new("L", (WIDTH, HEIGHT))
                shifted_material.paste(mask_canvas, (0, ground_shift))
                canvas, mask_canvas = placed, shifted_material
            frame_shifts.append(ground_shift)
            bbox = canvas.getchannel("A").getbbox()
            assert bbox is not None
            if variant == "small" and bbox[1] < 64:
                raise ValueError("Small art exceeds the lower 64 rows")
            if variant in ("crouch", "growth") and bbox[1] < 32:
                raise ValueError("Short form exceeds the lower 96 rows")
            data, material_data = canvas.tobytes(), mask_canvas.tobytes()
            packed.extend(rle(data, 4)); offsets.append(len(packed))
            materials.extend(rle(material_data, 1)); material_offsets.append(len(materials))
            b = record["bounds"]
            destination = assets / "prepared" / variant / action / f"{action}-{source_index:02}.png"
            destination.parent.mkdir(parents=True, exist_ok=True)
            canvas.save(destination, optimize=True)
            mask_canvas.save(destination.with_name(destination.stem + "-materials.png"), optimize=True)
            by_action[action].append(canvas)
            details.append({"variant": variant, "variant_index": variant_index, "frame_index": frame_index,
                            "action": action, "source_index": source_index,
                            "original_movie_frame": source_index + 21 if action == "run" else source_index,
                            "source_path": record["path"].relative_to(project).as_posix(), "source_sha256": sha256(record["path"]),
                            "source_bounds": list(b), "source_cap_bounds": list(record["cap_bounds"]),
                            "source_pivot": list(record["pivot"]), "placement_pivot": list(record["placement_pivot"]),
                            "fixed_source_scale": factor, "ground_shift_y": ground_shift,
                            "prepared_bounds": list(bbox),
                            "rgba_sha256": hashlib.sha256(data).hexdigest(),
                            "material_sha256": hashlib.sha256(material_data).hexdigest(),
                            "material_pixel_counts": [int(np.sum(np.asarray(mask_canvas) == label)) for label in range(4)]})
        foot_y.append(max(f.getchannel("A").getbbox()[3] for f in by_action["walk"]))
        groups[variant] = by_action
        for action, indices in GROUPS:
            if variant in ("big", "small"):
                contact(by_action[action], f"{action.title()} | {variant}", list(indices), docs / f"{action}-{variant}.png")
    header = project / "generated" / "realistic_character.h"
    lines = ["// Generated by tools/build_realistic_character.py. Do not edit.",
             "// One head calibration/envelope factor across walk, run and jump.",
             "// Pixel RLE: countLE16,R,G,B,A. Material RLE: countLE16,label.",
             "// Material labels: 0 protect, 1 cap, 2 shirt, 3 overalls.",
             "// Frames are variant-major, then walk14, run21, jump26.", "#pragma once", "#include <cstdint>", "", "namespace mario::realistic_data {",
             "inline constexpr unsigned Frames = 61;", "inline constexpr unsigned Variants = 4;",
             "inline constexpr unsigned WalkFrames = 14;", "inline constexpr unsigned RunFrames = 21;",
             "inline constexpr unsigned JumpFrames = 26;", "inline constexpr unsigned Width = 64;",
             "inline constexpr unsigned Height = 128;"]
    write_array(lines, "WalkSourceIndices", list(range(14)), "unsigned")
    write_array(lines, "RunSourceIndices", list(range(21, 42)), "unsigned")
    write_array(lines, "JumpSourceIndices", list(JUMP_SOURCES), "unsigned")
    write_array(lines, "TorsoPivotX", pivots_x, "unsigned")
    write_array(lines, "TorsoPivotY", pivots_y, "unsigned")
    write_array(lines, "GroundFootY", foot_y, "unsigned")
    write_array(lines, "FrameYOffset", frame_shifts, "unsigned")
    write_array(lines, "Packed", packed)
    write_array(lines, "Offsets", offsets, "std::uint32_t", 10)
    write_array(lines, "MaterialPacked", materials)
    write_array(lines, "MaterialOffsets", material_offsets, "std::uint32_t", 10)
    lines.extend(["} // namespace mario::realistic_data", ""])
    header.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    manifest = {"schema": 1, "generator": "tools/build_realistic_character.py", "pillow_version": PILLOW_VERSION,
                "frames": FRAMES, "variants": list(VARIANTS), "prepared_frames": len(details), "canvas": [WIDTH, HEIGHT],
                "frame_order": "variant-major; 14 walk, 21 run, 26 jump", "jump_source_indices": list(JUMP_SOURCES),
                "excluded_jump_source_indices": [18, 19, 20, 21], "originals": copied,
                "calibration": calibration, "reference_cap_width": reference,
                "normalized_envelope": [left, top, right, bottom], "common_fit_scale": common,
                "variant_ratios": dict(zip(VARIANTS, ratios)), "torso_pivot_x": pivots_x, "torso_pivot_y": pivots_y,
                "ground_foot_y_exclusive": foot_y,
                "alignment": "Fixed torso X; walk/run preserve bounce and land on row126; jump0..9/29 foot anchored; airborne jump torso Y removes recorded world arc",
                "materials": {"0": "protect", "1": "cap", "2": "shirt", "3": "overalls"},
                "scope": "Artwork only; source footage and old cartoon art retained; no gameplay/input changes",
                "limitations": "Jump footage ends at first contact; source 18..21 have clipped cap and are excluded; no synthetic landing",
                "rle_roundtrip_verified": True, "rgba_packed_bytes": len(packed), "material_packed_bytes": len(materials),
                "generated_header": {"path": header.relative_to(project).as_posix(), "sha256": sha256(header), "bytes": header.stat().st_size},
                "frames_detail": details}
    (docs / "asset-manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"frames": FRAMES, "prepared": len(details), "head_reference": reference, "common_scale": common,
                      "calibration": calibration, "packed_bytes": len(packed), "material_bytes": len(materials),
                      "header_sha256": sha256(header), "header": str(header)}, indent=2))


if __name__ == "__main__":
    main()
