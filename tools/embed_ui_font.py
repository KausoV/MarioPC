from pathlib import Path
import hashlib
root = Path(__file__).resolve().parents[1]
font = root / "dependencies" / "NotoSans" / "NotoSans.ttf"
data = font.read_bytes()
header = root / "generated" / "ui_font.h"
with header.open("w", encoding="utf-8", newline="\n") as out:
    out.write("// Noto Sans, SIL Open Font License; see licenses/NotoSans-OFL-1.1.txt.\n#pragma once\n#include <cstddef>\nstatic const unsigned char MarioUIFont[] = {\n")
    for offset in range(0, len(data), 32):
        out.write("  " + ",".join(str(value) for value in data[offset:offset+32]) + ",\n")
    out.write("};\nstatic constexpr std::size_t MarioUIFontSize = sizeof(MarioUIFont);\n")
print(f"Embedded UI font: {len(data)} bytes; SHA256={hashlib.sha256(data).hexdigest()}")