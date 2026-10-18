#!/usr/bin/env python3
"""Run the real firmware ring implementation under ASan/UBSan."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="bootlog-test-", dir="/tmp/opencode") as tmp:
    exe = Path(tmp) / "test"
    includes = [".", "ws/edk2/MdePkg/Include", "ws/edk2/MdePkg/Include/AArch64",
                "ws/edk2/ArmPkg/Include", "ws/MT6768Pkg/Include"]
    subprocess.run(["gcc", "-std=c11", "-g", "-O1", "-fshort-wchar",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                    *("-I" + str(root / p) for p in includes),
                    str(root / "tools/test_lancelot_bootlog.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
