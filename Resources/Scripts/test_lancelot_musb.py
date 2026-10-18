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
                ROOT / "Silicon/Silicium/SiliciumPkg/Include",
                ROOT / "Silicon/MediaTek/MediaTekPkg/Include"]
    assert source.count("int main(int argc, char **argv)") == 1
    managed = source.replace("int main(int argc, char **argv)", "void RegressionMain(int argc, char **argv)")
    # Add board-MMIO backing for HardwareInit(), which the packet-only archive
    # fixtures did not invoke. The firmware C remains unmodified by this harness.
    managed = managed.replace("static UINT8 *Addr(UINTN A) {", """
static BOOLEAN HardwareForbidden;
static UINT8 ClockRegs[4096], InfraRegs[4096], PhyRegs[256];
static UINT32 PllEnable=1;
static UINT8 *Addr(UINTN A) {
  assert(!HardwareForbidden);
  if(A==0x1000C238) return (UINT8*)&PllEnable;
  if(A>=0x10000000 && A<0x10001000) return ClockRegs+(A-0x10000000);
  if(A>=0x10001000 && A<0x10002000) return InfraRegs+(A-0x10001000);
  if(A>=0x11CC0800 && A<0x11CC0900) return PhyRegs+(A-0x11CC0800);
""")
    managed += '\n#include "Resources/Tests/LancelotMusbDeviceLifecycle.inc"\n'
    with tempfile.TemporaryDirectory(prefix="mu-musb-", dir="/tmp/opencode") as tmp:
        tmp = Path(tmp)
        for name, content, flags in [("standalone", source, []), ("managed", managed, ["-DMUSB_MANAGED_ROLE=1"])]:
            test = tmp / (name + ".c")
            test.write_text(content)
            exe = tmp / name
            subprocess.run([
                "gcc", "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-strict-aliasing",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie", *flags,
                *("-I" + str(p) for p in includes), str(test), "-o", str(exe),
            ], check=True)
            transcript = tmp / (name + "-fastboot.txt")
            subprocess.run([str(exe), str(transcript)], check=True, timeout=30)
            reply = parse_reply(transcript.read_bytes())
            assert reply.boot_id == 0x1122334455667788 and reply.start == 0 and reply.end == 4096
            assert reply.payload == bytes(range(256)) * 16
            print("PASS:", name, "Mu MUSB + BootLog -> PC decoder (ASan/UBSan)")


if __name__ == "__main__":
    main()
