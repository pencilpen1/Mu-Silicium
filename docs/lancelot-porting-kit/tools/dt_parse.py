#!/usr/bin/env python3
"""Parse Android boot image + DTBO + embedded DTBs, and dump the FDT tree.

Pure-stdlib. No dtc needed. Read-only: never executes anything.

Usage:
  python3 -I dt_parse.py boot  <boot.img>
  python3 -I dt_parse.py dtbo  <dtbo.img>
  python3 -I dt_parse.py fdt   <file>            # treat whole file as one DTB
  python3 -I dt_parse.py scan  <file>            # scan file for DTB magic, dump each
"""
import struct
import sys


def u32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def u64(b, o):
    return struct.unpack(">Q", b[o:o + 8])[0]


def le32(b, o):
    return struct.unpack("<I", b[o:o + 4])[0]


def le64(b, o):
    return struct.unpack("<Q", b[o:o + 8])[0]


# ---------- Android boot image ----------
def parse_boot(path):
    data = open(path, "rb").read()
    assert data[:8] == b"ANDROID!", "not an ANDROID boot image"
    kernel_size = le32(data, 0x08)
    kernel_addr = le32(data, 0x0C)
    ramdisk_size = le32(data, 0x10)
    ramdisk_addr = le32(data, 0x14)
    second_size = le32(data, 0x18)
    second_addr = le32(data, 0x1C)
    tags_addr = le32(data, 0x20)
    page_size = le32(data, 0x24)
    header_version = le32(data, 0x28)
    os_version = le32(data, 0x2C)
    name = data[0x30:0x40].split(b"\x00")[0].decode("ascii", "replace")
    cmdline = data[0x40:0x240].split(b"\x00")[0].decode("ascii", "replace")
    extra_cmdline = data[0x640:0xA40].split(b"\x00")[0].decode("ascii", "replace")
    # v1/v2 fields at offset 1632
    recovery_dtbo_size = le32(data, 0x660) if len(data) > 0x664 else 0
    recovery_dtbo_offset = le64(data, 0x668) if len(data) > 0x670 else 0
    header_size = le32(data, 0x670) if len(data) > 0x674 else 0
    dtb_size = le32(data, 0x674) if header_version >= 2 and len(data) > 0x678 else 0
    dtb_addr = le64(data, 0x678) if header_version >= 2 and len(data) > 0x680 else 0

    print("=== Android boot image header ===")
    print(f"  header_version = {header_version}")
    print(f"  page_size       = 0x{page_size:x}")
    print(f"  kernel: size=0x{kernel_size:x} addr=0x{kernel_addr:x}")
    print(f"  ramdisk: size=0x{ramdisk_size:x} addr=0x{ramdisk_addr:x}")
    print(f"  second: size=0x{second_size:x} addr=0x{second_addr:x}")
    print(f"  tags_addr       = 0x{tags_addr:x}")
    print(f"  name            = {name!r}")
    print(f"  cmdline         = {cmdline!r}")
    print(f"  extra_cmdline   = {extra_cmdline!r}")
    print(f"  os_version      = 0x{os_version:x}")
    print(f"  recovery_dtbo:  size=0x{recovery_dtbo_size:x} off=0x{recovery_dtbo_offset:x}")
    print(f"  header_size     = 0x{header_size:x}")
    print(f"  dtb: size=0x{dtb_size:x} addr=0x{dtb_addr:x}")

    # Page layout: header(1 page) + kernel + ramdisk + second + recovery_dtbo + dtb
    ps = page_size
    pages = lambda sz: (sz + ps - 1) // ps
    off = ps
    off += pages(kernel_size) * ps
    off += pages(ramdisk_size) * ps
    off += pages(second_size) * ps
    off += pages(recovery_dtbo_size) * ps
    dtb_off = off
    print(f"  -> appended DTB expected at offset 0x{dtb_off:x}")
    if dtb_size and data[dtb_off:dtb_off + 4] == b"\xd0\x0d\xfe\xed":
        print(f"  -> DTB magic confirmed at 0x{dtb_off:x}")
        return [("boot.img appended DTB", data[dtb_off:dtb_off + dtb_size])]
    # fallback: scan whole image for DTB magic
    return scan_for_fdts(data, label_prefix="boot.img@")


# ---------- DTBO ----------
DT_TABLE_MAGIC = 0xB98E4D64  # "DTBO" table magic per AOSP libufdt/sysdeps

def parse_dtbo(path):
    data = open(path, "rb").read()
    magic = u32(data, 0)
    print(f"=== DTBO image ===")
    print(f"  magic = 0x{magic:x}  (expect 0x{DT_TABLE_MAGIC:x})")
    if magic != DT_TABLE_MAGIC:
        print("  ! unexpected magic; raw first 32 bytes:")
        print("   " + data[:32].hex())
        # still try to scan for embedded fdts
        return scan_for_fdts(data, label_prefix="dtbo.img@")
    total_size = u32(data, 4)
    header_size = u32(data, 8)
    entry_size = le32(data, 0x0C)
    entry_count = le32(data, 0x10)
    entries_off = le32(data, 0x14)
    page_size = le32(data, 0x18)
    version = le32(data, 0x1C)
    print(f"  total_size=0x{total_size:x} header_size=0x{header_size:x}")
    print(f"  entry_size=0x{entry_size:x} entry_count={entry_count} entries_off=0x{entries_off:x}")
    print(f"  page_size=0x{page_size:x} version=0x{version:x}")
    out = []
    for i in range(entry_count):
        eo = entries_off + i * entry_size
        dt_size = u32(data, eo + 0)
        dt_offset = u32(data, eo + 4)
        eid = u32(data, eo + 8)
        rev = u32(data, eo + 0x0C)
        flags = u32(data, eo + 0x10)
        print(f"  entry[{i}]: size=0x{dt_size:x} off=0x{dt_offset:x} id=0x{eid:x} rev=0x{rev:x} flags=0x{flags:x}")
        blob = data[dt_offset:dt_offset + dt_size]
        if blob[:4] == b"\xd0\x0d\xfe\xed":
            out.append((f"dtbo entry {i}", blob))
    return out


# ---------- scan for FDT magic ----------
def scan_for_fdts(data, label_prefix=""):
    out = []
    idx = 0
    pos = 0
    while True:
        p = data.find(b"\xd0\x0d\xfe\xed", pos)
        if p < 0:
            break
        total = u32(data, p + 4)
        if 0 < total <= len(data) - p:
            out.append((f"{label_prefix}0x{p:x}#{idx}", data[p:p + total]))
            idx += 1
            pos = p + total
        else:
            pos = p + 4
    return out


# ---------- FDT tree dump ----------
FDT_BEGIN = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9


ALNUM = (b"abcdefghijklmnopqrstuvwxyz"
         b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
         b"0123456789")


def looks_like_string(val):
    """True only for genuine string/stringlist properties."""
    if not val or val[-1] != 0:
        return False
    body = val[:-1]
    segs = body.split(b"\x00")
    if any(len(s) == 0 for s in segs):
        return False  # consecutive nuls -> numeric
    for s in segs:
        if not s:
            continue
        if not all(0x20 <= c < 0x7F for c in s):
            return False
        if len(s) < 2:
            return False
        alnum = sum(1 for c in s if c in ALNUM)
        if alnum * 2 < len(s):
            return False  # mostly punctuation -> numeric in disguise
    return True


def fmt_prop(val):
    n = len(val)
    if n == 0:
        return "<empty>"
    if looks_like_string(val):
        parts = [p.decode("ascii", "replace") for p in val.split(b"\x00") if p]
        return ",".join(parts) if len(parts) > 1 else parts[0]
    if n % 4 == 0:
        words = [hex(u32(val, i)) for i in range(0, n, 4)]
        return " ".join(words) if len(words) <= 16 else " ".join(words[:16]) + f"  ...({n}B)"
    return val.hex()


def dump_fdt(label, blob, root_filter=None):
    print(f"\n########## FDT: {label}  size={len(blob)} ##########")
    if blob[:4] != b"\xd0\x0d\xfe\xed":
        print("  not an FDT")
        return
    totalsize = u32(blob, 4)
    off_struct = u32(blob, 0x08)
    off_strings = u32(blob, 0x0C)
    off_rsv = u32(blob, 0x10)
    version = u32(blob, 0x14)
    print(f"  totalsize=0x{totalsize:x} version={version}")
    # reservation map
    o = off_rsv
    print("  --- reservation map ---")
    any_rsv = False
    while True:
        a = u64(blob, o)
        s = u64(blob, o + 8)
        o += 16
        if a == 0 and s == 0:
            break
        any_rsv = True
        print(f"    rsv: addr=0x{a:x} size=0x{s:x}")
    if not any_rsv:
        print("    (none)")

    strings = blob[off_strings:]
    def getstr(off):
        end = strings.find(b"\x00", off)
        return strings[off:end].decode("ascii", "replace")

    print("  --- device tree ---")
    p = off_struct
    depth = 0
    path = []
    while True:
        tok = u32(blob, p)
        p += 4
        if tok == FDT_BEGIN:
            end = blob.find(b"\x00", p)
            nm = blob[p:end].decode("ascii", "replace")
            p = (end + 1 + 3) & ~3
            path.append(nm)
            full = "/".join(path) or "/"
            print("  " + "  " * depth + f"{nm}/" + ("" if nm else "/"))
            depth += 1
        elif tok == FDT_END_NODE:
            depth -= 1
            path.pop()
        elif tok == FDT_PROP:
            ln = u32(blob, p)
            noff = u32(blob, p + 4)
            p += 8
            val = blob[p:p + ln]
            p = (p + ln + 3) & ~3
            name = getstr(noff)
            full = "/" + "/".join(x for x in path if x)
            print("  " + "  " * depth + f"{name} = {fmt_prop(val)}")
        elif tok == FDT_NOP:
            continue
        elif tok == FDT_END:
            break
        else:
            print(f"  ! unknown token 0x{tok:x} at offset 0x{p - 4:x}")
            break


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "boot":
        blobs = parse_boot(path)
    elif cmd == "dtbo":
        blobs = parse_dtbo(path)
    elif cmd == "fdt":
        blobs = [("whole file", open(path, "rb").read())]
    elif cmd == "scan":
        blobs = scan_for_fdts(open(path, "rb").read(), label_prefix=path + "@")
    else:
        print(__doc__); return 1
    for label, b in blobs:
        dump_fdt(label, b)
    return 0


if __name__ == "__main__":
    sys.exit(main())
