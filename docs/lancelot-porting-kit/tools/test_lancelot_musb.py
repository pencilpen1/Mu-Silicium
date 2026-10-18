#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile
from read_lancelot_log import parse_reply

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="musb-test-", dir="/tmp/opencode") as tmp:
    exe = Path(tmp) / "test"
    includes = [".", "ws/edk2/MdePkg/Include", "ws/edk2/MdePkg/Include/AArch64", "ws/edk2/ArmPkg/Include", "ws/MT6768Pkg/Include"]
    subprocess.run(["gcc", "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-strict-aliasing",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                    *("-I" + str(root / p) for p in includes),
                    str(root / "tools/test_lancelot_musb.c"), "-o", str(exe)], check=True)
    transcript = Path(tmp) / "fastboot.txt"
    subprocess.run([str(exe), str(transcript)], check=True, timeout=30)
    reply = parse_reply(transcript.read_bytes())
    assert reply.boot_id == 0x1122334455667788 and reply.start == 0 and reply.end == 4096
    assert reply.payload == bytes(range(256)) * 16
    print("PASS: PC decoder consumes actual firmware-generated INFO packets byte-for-byte")
