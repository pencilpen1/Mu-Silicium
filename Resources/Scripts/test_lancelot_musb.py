#!/usr/bin/env python3
"""Run the porting-kit regression fixtures against the integrated Mu driver."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KIT = ROOT / "docs/lancelot-porting-kit/tools"
sys.path.insert(0, str(KIT))
from read_lancelot_log import parse_reply


def main():
    source = (KIT / "test_lancelot_musb.c").read_text()
    paths = {
        "ws/MT6768Pkg/Library/BootLogLib/BootLogLib.c":
            "Silicon/Silicium/SiliciumPkg/Library/BootLogLib/BootLogLib.c",
        "ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/MusbDiagnostic.c":
            "Silicon/MediaTek/MediaTekPkg/Drivers/MusbDiagnosticDxe/MusbDiagnostic.c",
    }
    for old, new in paths.items():
        assert source.count('"' + old + '"') == 1
        source = source.replace('"' + old + '"', '"' + new + '"')
    includes = [ROOT, ROOT / "Mu_Basecore/MdePkg/Include",
                ROOT / "Mu_Basecore/MdePkg/Include/AArch64",
                ROOT / "Mu_Basecore/ArmPkg/Include",
                ROOT / "Silicon/Silicium/SiliciumPkg/Include"]
    with tempfile.TemporaryDirectory(prefix="mu-musb-", dir="/tmp/opencode") as tmp:
        tmp = Path(tmp)
        test = tmp / "test.c"
        test.write_text(source)
        exe = tmp / "test"
        subprocess.run([
            "gcc", "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-strict-aliasing",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
            *("-I" + str(p) for p in includes), str(test), "-o", str(exe),
        ], check=True)
        transcript = tmp / "fastboot.txt"
        subprocess.run([str(exe), str(transcript)], check=True, timeout=30)
        reply = parse_reply(transcript.read_bytes())
        assert reply.boot_id == 0x1122334455667788 and reply.start == 0 and reply.end == 4096
        assert reply.payload == bytes(range(256)) * 16
        print("PASS: integrated Mu MUSB + BootLog -> PC decoder (ASan/UBSan)")


if __name__ == "__main__":
    main()
