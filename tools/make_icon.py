"""KaiVideo's icon (icon.jpg, 256x256) is no longer drawn here: all the Kai apps' icons
come from one script, switch2homebrew-submit/tools/make_app_icons.py, which writes
switch2homebrew-submit/icons/kaivideo.jpg. This copies that file to icon.jpg.

Usage: python tools/make_icon.py [OUT]
       (run make_app_icons.py first after changing the symbol or its colors)
"""
import shutil
import sys
from pathlib import Path

SUBMIT = Path(__file__).resolve().parents[2] / "switch2homebrew-submit"
SOURCE = SUBMIT / "icons" / "kaivideo.jpg"


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.jpg"
    if not SOURCE.is_file():
        print(f"{SOURCE} not found: run python {SUBMIT / 'tools' / 'make_app_icons.py'} first")
        return 1
    shutil.copyfile(SOURCE, out)
    print(f"{SOURCE} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
