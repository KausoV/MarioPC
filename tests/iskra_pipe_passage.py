"""Exercise both wooden pipe entrances with controller inputs in the actual Iskra executable."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageChops


# Walk and jump over the three solid trunks, brake on the fourth, then press Down.
FIRST_TRAIL = """50 51 S
280 329 BR
329 357 BRA
357 383 BR
383 402 BRA
402 438 BR
438 454 BRA
454 505 BR
505 524 BRA
524 557 BR
557 581 BRA
581 592 BR
592 613 BRA
613 655 BR
655 678 BRA
678 689 BR
689 699 BRA
706 746 L
746 750 LD
750 885 D
885 952 BR
952 985 BRA
985 1040 BR
1040 1050 L
1062 1700 R
"""


def run(executable, output):
    settings = output / "display.ini"
    settings.write_text("width=960\nheight=540\nscreen_mode=0\nscaling=2\n"
                        "filter=0\nvsync=0\nsmart_enemies=0\n", encoding="ascii")

    def capture(level, frame, controls):
        stem = output / f"{level}-{frame}"
        script = output / f"{level}.txt"
        script.write_text(controls, encoding="ascii")
        snapshot = stem.with_suffix(".dat")
        command = [str(executable), "--smoke", "--fast", "--level", level,
                   "--frames", str(frame), "--settings-file", str(settings),
                   "--input-script", str(script), "--game-save-file", str(output / "unused.dat"),
                   "--capture-graphics", str(stem.with_suffix(".png")), "--save-game", str(snapshot),
                   "--report", str(stem.with_suffix(".json"))]
        subprocess.run(command, cwd=executable.parent, check=True, capture_output=True, timeout=90)
        report = json.loads(stem.with_suffix(".json").read_text(encoding="utf-8-sig"))
        assert report["simulation_frame"] == frame and report["graphics_scale"] == 4
        # The v1 save envelope has a 64-byte header followed by the native snapshot
        # with its 44-byte header. Read its gameplay state without altering it.
        with snapshot.open("rb") as stream:
            assert stream.read(8) == b"MARIOSV1"
            stream.seek(108)
            ram = stream.read(0x800)
        snapshot.unlink()
        with Image.open(stem.with_suffix(".png")) as image:
            return image.convert("RGB"), ram, report["world_camera"]

    def changed(occupied, empty, world_rect):
        image, _, cam = occupied
        ref, _, other_cam = empty
        assert cam == other_cam
        left, top, right, bottom = world_rect
        box = tuple(v*4 for v in (left-cam, top, right-cam, bottom))
        diff = ImageChops.difference(image.crop(box), ref.crop(box)).convert("L")
        # Large brightness changes identify the actor, ignoring drifting motes and subtle rim-light.
        return sum(diff.histogram()[12:])

    cases = (
        ("1-1", FIRST_TRAIL, (756, 761), 798, 885, 2, (921, 145, 937, 148), (920, 154, 937, 168)),
        ("1-2", "50 51 S\n", (390, 400), 520, 680, 2, (160, 184, 178, 201), (185, 184, 191, 201)),
        ("1-1", FIRST_TRAIL, (1346, 1350), 1320, 1360, 1, (2617, 177, 2633, 180), (2616, 186, 2633, 200)),
    )
    for level, controls, ticks, hidden, arrived, area, interior, facade in cases:
        crossing = [capture(level, frame, controls) for frame in ticks]
        empty = capture(level, hidden, controls)
        _, destination, _ = capture(level, arrived, controls)
        assert destination[0x74e] == area and destination[0xe] == 8, f"{level}: transition to area {area}"
        assert all(ram[0x74e] == 1 for _, ram, _ in [*crossing, empty])
        inside = [changed(frame, empty, interior) for frame in crossing]
        outside = [changed(frame, empty, facade) for frame in crossing]
        print(f"{level} at {ticks}: pixels changing in opening {inside}, on front bark {outside}", flush=True)
        assert min(inside) > 80, f"{level}: robot disappears before entering the hollow"
        assert max(outside) < 10, f"{level}: robot shows through the trunk"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        run(args.executable.resolve(), args.output.resolve())
    else:
        with tempfile.TemporaryDirectory() as folder:
            run(args.executable.resolve(), Path(folder))
