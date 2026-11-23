"""Embed user-supplied SMB1 iNES data for the compiled native Mario port.

This tool performs no CPU emulation. The native gameplay implementation reads
graphics, music, level tables and other data from the embedded bytes.
Python is required only for rebuilding assets, never for running MarioPC.exe.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import tempfile

ROM_LENGTH = 16 + 2 * 16_384 + 8_192
PROJECT_ROOT = Path(__file__).resolve().parents[1]


def validate_rom(data: bytes) -> None:
    if len(data) < 16 or data[:4] != b"NES\x1a":
        raise ValueError("Expected an iNES file beginning with NES + 0x1A.")
    if data[7] & 0x0C == 0x08:
        raise ValueError("NES 2.0 headers are not supported; supply an iNES SMB1 ROM.")
    mapper = (data[6] >> 4) | (data[7] & 0xF0)
    if mapper != 0:
        raise ValueError(f"Unsupported mapper {mapper}; this native SMB1 port requires mapper 0.")
    if data[4] != 2 or data[5] != 1:
        raise ValueError("Expected 2 PRG banks (32 KiB) and 1 CHR bank (8 KiB).")
    if data[6] & 0x04:
        raise ValueError("ROM trainers are not supported.")
    if len(data) != ROM_LENGTH:
        raise ValueError(f"Expected exactly {ROM_LENGTH} bytes, got {len(data)}.")


def header_for(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        suffix = "," if offset + 16 < len(data) else ""
        rows.append(", ".join(f"0x{value:02x}" for value in data[offset:offset+16]) + suffix)
    return (
        "#pragma once\n"
        "#include <stddef.h>\n"
        "// User-provided graphics, levels and music data. Gameplay is compiled C.\n"
        "static const unsigned char MarioGameData[] = {\n"
        + "\n".join(rows)
        + "\n};\nstatic const size_t MarioGameDataSize = sizeof(MarioGameData);\n"
    )


def embed(rom: Path, output: Path) -> str:
    source = rom.resolve(strict=True)
    destination = output.resolve()
    if source == destination:
        raise ValueError("The generated header must be a different file from the source ROM.")
    data = source.read_bytes()
    validate_rom(data)
    text = header_for(data)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="\n",
            dir=destination.parent, prefix=destination.name + ".", suffix=".tmp", delete=False,
        ) as file:
            temporary = Path(file.name)
            file.write(text)
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description="Embed a supplied mapper-0 SMB1 iNES file into native C game data.")
    parser.add_argument("rom", type=Path, help="Path to your 40,976-byte SMB1 .nes file.")
    parser.add_argument(
        "--out", "--output", dest="output", type=Path,
        default=PROJECT_ROOT / "generated" / "game_data.h",
        help="Output C header; defaults to this project's generated/game_data.h.",
    )
    args = parser.parse_args()
    try:
        digest = embed(args.rom, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Cannot embed ROM: {error}\n")
    print(f"Embedded {ROM_LENGTH} bytes into {args.output.resolve()}")
    print(f"Source SHA256: {digest}")


if __name__ == "__main__":
    main()

