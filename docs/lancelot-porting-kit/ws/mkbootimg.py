#!/usr/bin/env python3
"""Minimal Android boot image v2 packer for lancelot.

No external deps. Inverse of tools/dt_parse.py's boot parser.

Usage: python3 mkbootimg.py --kernel k --ramdisk r --dtb d --base ... -o out
"""
import os
import struct
import sys

PAGE = 0x800


def pageceil(n, page=PAGE):
    return ((n + page - 1) // page) * page


def read_padded(path):
    with open(path, "rb") as f:
        d = f.read()
    return d + b"\x00" * (pageceil(len(d)) - len(d))


def main():
    args = {}
    files = {}
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a in ("--cmdline", "--kernel", "--ramdisk", "--second", "--dtb",
                 "--base", "--kernel_offset", "--ramdisk_offset",
                 "--second_offset", "--tags_offset", "--dtb_offset",
                 "--header_version", "-o"):
            v = sys.argv[i + 1]
            if a.startswith("--") and a not in ("--kernel", "--ramdisk",
                                                 "--second", "--dtb", "--cmdline"):
                v = int(v, 0)
            if a == "--kernel":
                files["kernel"] = v
            elif a == "--ramdisk":
                files["ramdisk"] = v
            elif a == "--second":
                files["second"] = v
            elif a == "--dtb":
                files["dtb"] = v
            elif a == "--cmdline":
                args["cmdline"] = v
            elif a == "-o":
                args["out"] = v
            else:
                args[a[2:]] = v
            i += 2
        else:
            i += 1

    base = args["base"]
    hdr_ver = args.get("header_version", 2)

    kdata = read_padded(files["kernel"])
    rdata = read_padded(files["ramdisk"])
    sdata = read_padded(files["second"]) if "second" in files else b""
    ddata = read_padded(files["dtb"]) if "dtb" in files else b""

    ksize = os.path.getsize(files["kernel"])
    rsize = os.path.getsize(files["ramdisk"])
    dsize = os.path.getsize(files["dtb"]) if "dtb" in files else 0

    cmdline = args.get("cmdline", "").encode() + b"\x00"
    cmdline = cmdline[:512].ljust(512, b"\x00")

    h = bytearray()
    h += b"ANDROID!"
    h += struct.pack("<I", ksize)
    h += struct.pack("<I", base + args["kernel_offset"])
    h += struct.pack("<I", rsize)
    h += struct.pack("<I", base + args["ramdisk_offset"])
    h += struct.pack("<I", 0)                              # second_size
    h += struct.pack("<I", base + args.get("second_offset", 0))
    h += struct.pack("<I", base + args["tags_offset"])
    h += struct.pack("<I", PAGE)
    h += struct.pack("<I", hdr_ver)
    h += struct.pack("<I", 0)                              # os_version
    h += b"\x00" * 16                                      # name
    h += cmdline                                           # 512
    h += struct.pack("<8I", *([0] * 8))                    # id
    h += b"\x00" * 1024                                    # extra_cmdline
    # v1/v2 trailing fields
    h += struct.pack("<I", 0)                              # recovery_dtbo_size
    h += struct.pack("<Q", 0)                              # recovery_dtbo_offset
    h += struct.pack("<I", 1660)                           # header_size
    h += struct.pack("<I", dsize)                          # dtb_size
    h += struct.pack("<Q", base + args.get("dtb_offset", 0))  # dtb_addr
    h = bytes(h)
    h = h + b"\x00" * (pageceil(len(h)) - len(h))

    out = h + kdata + rdata + sdata + ddata
    with open(args["out"], "wb") as f:
        f.write(out)
    print("wrote %s (%d bytes)" % (args["out"], len(out)))


if __name__ == "__main__":
    main()
