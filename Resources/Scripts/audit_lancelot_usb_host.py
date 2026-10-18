#!/usr/bin/env python3
"""Check the built Host FV and boot image against current build artifacts."""
import gzip
from pathlib import Path
import re
import struct
import uuid

ROOT = Path(__file__).resolve().parents[2]


def main():
    base = ROOT / "Build/lancelotPkg/RELEASE_CLANGPDB/FV"
    fv = (base / "FVMAIN.Fv").read_bytes()
    assert fv[40:44] == b"_FVH"
    offset = struct.unpack_from("<H", fv, 48)[0]
    files = []
    while True:
        offset = (offset + 7) & ~7
        if offset + 24 > len(fv) or fv[offset:offset + 24] == b"\xff" * 24:
            break
        size = int.from_bytes(fv[offset + 20:offset + 23], "little")
        if size == 0xFFFFFF:
            size = struct.unpack_from("<Q", fv, offset + 24)[0]
        assert size >= 24 and offset + size <= len(fv)
        files.append(str(uuid.UUID(bytes_le=fv[offset:offset + 16])))
        offset += size
    modules = {
        "Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/MusbHostDxe.inf": 1,
        "Silicon/MediaTek/MediaTekPkg/Drivers/MusbDiagnosticDxe/MusbDiagnosticDxe.inf": 0,
        "Mu_Basecore/MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf": 1,
        "Mu_Basecore/MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf": 1,
        "Mu_Basecore/MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf": 1,
    }
    for path, count in modules.items():
        guid = re.search(r"FILE_GUID\s*=\s*(\S+)", (ROOT / path).read_text()).group(1).lower()
        assert files.count(guid) == count, path
        print(Path(path).stem, "FFS count:", count)
    image = (ROOT / "Mu-lancelot.img").read_bytes()
    size = struct.unpack_from("<I", image, 8)[0]
    page = struct.unpack_from("<I", image, 36)[0]
    assert image[:8] == b"ANDROID!" and struct.unpack_from("<I", image, 40)[0] == 2
    assert page == 2048 and page + size <= len(image)
    assert gzip.decompress(image[page:page + size]) == (base / "SILICIUM_UEFI.fd").read_bytes()
    print("PASS: Host-only FV, USB bus/keyboard/storage and boot-v2 gzip(FD) payload")


if __name__ == "__main__":
    main()
