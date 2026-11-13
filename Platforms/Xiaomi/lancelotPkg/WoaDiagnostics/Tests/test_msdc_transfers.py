#!/usr/bin/env python3
"""Exercise extracted production MSDC functions with a stateful FIFO model."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def extract(source, name):
    match = re.search(r"(?m)^" + name + r" \(\n", source)
    if match is None:
        raise ValueError("Missing production function: " + name)
    brace = source.index("{", match.end())
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mu", type=Path, required=True)
    ap.add_argument("--prove-regression", action="store_true")
    args = ap.parse_args()
    source = (args.mu / "Silicon/MediaTek/MediaTekPkg/Drivers/MsdcDxe/MsdcDxe.c").read_text()
    functions = {
        "MsdcDeadlineExpired": "BOOLEAN", "MsdcTransferTimeout": "EFI_STATUS",
        "MsdcIntTrackError": "EFI_STATUS", "MsdcPollInterrupts": "EFI_STATUS",
        "MsdcFifoRxBytes": "VOID", "MsdcFifoTxBytes": "VOID",
        "MsdcPioRead": "EFI_STATUS", "MsdcPioWrite": "EFI_STATUS",
        "MsdcPassThru": "EFI_STATUS",
    }
    actual = "\n\n".join(result + "\n" + extract(source, name)
                         for name, result in functions.items()) + "\n"
    harness = Path(__file__).with_name("msdc_transfer_test.c")
    with tempfile.TemporaryDirectory(prefix="woa-msdc-") as name:
        root = Path(name)
        include = root / "msdc-under-test.inc"
        include.write_text(actual)
        exe = root / "msdc-test"
        command = ["clang", "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-I" + str(root), str(harness), "-o", str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
        if args.prove_regression:
            old = actual.replace("(FifoCs >> 16) & 0xff", "FifoCs & (0xff << 16)")
            if old == actual:
                raise ValueError("TXCNT production expression changed")
            include.write_text(old)
            subprocess.run(command, check=True)
            result = subprocess.run([str(exe)], stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=10)
            if result.returncode == 0:
                raise AssertionError("Tests did not catch the original TXCNT defect")
            print("PASS: reinstating the original TXCNT mask makes the hardware-model test fail")


if __name__ == "__main__":
    main()
