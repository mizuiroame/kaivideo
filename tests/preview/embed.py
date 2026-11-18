"""Writes NAME.h and NAME.c that embed a file, like devkitPro's bin2o.

Usage: python3 embed.py INPUT OUTDIR NAME
"""
import sys
from pathlib import Path


def main() -> None:
    source, outdir, name = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    data = source.read_bytes()
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / f"{name}.h").write_text(
        f"#pragma once\n"
        f"extern const unsigned char {name}[];\n"
        f"extern const unsigned int {name}_size;\n"
    )
    rows = ",\n".join(
        ", ".join(str(b) for b in data[i:i + 32]) for i in range(0, len(data), 32)
    )
    (outdir / f"{name}.c").write_text(
        f"const unsigned char {name}[] = {{\n{rows}\n}};\n"
        f"const unsigned int {name}_size = {len(data)};\n"
    )


if __name__ == "__main__":
    main()
