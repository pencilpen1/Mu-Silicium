#!/usr/bin/env python3
"""Test the actual SEC overlay selection against a guarded MMIO model."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def extract(source, name):
    match = re.search(r"(?m)^" + name + r" \([^{}]*\)\s*\{", source)
    if match is None:
        raise ValueError("Missing production display function: " + name)
    brace = source.index("{", match.start())
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mu", type=Path, required=True)
    args = ap.parse_args()
    source = (args.mu / "Silicon/MediaTek/MT6768Pkg/Library/PlatformSecLib/PlatformSec.c").read_text()
    code = "\n\n".join(kind + "\n" + extract(source, name) for kind, name in (
        ("BOOLEAN", "FixBootSurface"), ("UINT32", "FixBootEngine"), ("VOID", "EnableConstantBlending")))
    with tempfile.TemporaryDirectory(prefix="woa-display-") as name:
        temp = Path(name)
        (temp / "display-under-test.inc").write_text(code)
        executable = temp / "display-test"
        subprocess.run(["clang", "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I" + str(temp),
                        str(Path(__file__).with_name("boot_display_test.c")), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    main()
