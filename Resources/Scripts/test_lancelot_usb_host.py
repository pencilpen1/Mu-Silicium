#!/usr/bin/env python3
"""Compile the actual Host and board driver C with scripted MMIO and sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    includes = [ROOT, ROOT / "Mu_Basecore/MdePkg/Include",
                ROOT / "Mu_Basecore/MdePkg/Include/AArch64",
                ROOT / "Mu_Basecore/ArmPkg/Include",
                ROOT / "Silicon/MediaTek/MediaTekPkg/Include",
                ROOT / "Silicon/Silicium/SiliciumPkg/Include"]
    with tempfile.TemporaryDirectory(prefix="lancelot-host-", dir="/tmp/opencode") as tmp:
        for name in ["LancelotMusbHost", "LancelotUsbBoard"]:
            exe = Path(tmp) / name
            subprocess.run([
                "gcc", "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-strict-aliasing",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                *("-I" + str(p) for p in includes),
                str(ROOT / "Resources/Tests" / (name + ".c")), "-o", str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
