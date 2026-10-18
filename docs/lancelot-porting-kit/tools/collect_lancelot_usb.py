#!/usr/bin/env python3
"""Read-only root-ADB USB bring-up capture; works with Windows adb.exe too.

No partition writes, raw MMIO reads, debugfs mounts or USB role changes.
Binary output is captured directly, avoiding PowerShell redirection corruption.
"""
import argparse
from datetime import datetime
import json
import shlex
from pathlib import Path
import subprocess
import sys
import zipfile


COMMANDS = {
    "identity.txt": "uname -a; cat /proc/version; getprop ro.product.device; getprop ro.build.fingerprint; getprop ro.vendor.build.fingerprint; getprop ro.boot.hardware; getprop sys.usb.config; getprop sys.usb.state; getprop persist.sys.usb.config; getprop sys.usb.controller",
    "dmesg.txt": "dmesg",
    "iomem.txt": "cat /proc/iomem",
    "interrupts.txt": "cat /proc/interrupts",
    "kernel.config.gz": "cat /proc/config.gz",
    "runtime.dtb": "cat /sys/firmware/fdt",
    "clock-summary.txt": "cat /sys/kernel/debug/clk/clk_summary",
    "regulator-summary.txt": "cat /sys/kernel/debug/regulator/regulator_summary",
    "usb-devices.txt": "cat /sys/kernel/debug/usb/devices; ls -l /sys/bus/usb/devices; ls -l /sys/class/udc; ls -l /sys/class/usb_role; ls -l /sys/class/typec; ls -l /sys/class/extcon",
    "usb-bindings.txt": "for p in /sys/bus/platform/devices/*usb* /sys/bus/platform/devices/*phy*; do [ -e \"$p\" ] || continue; echo \"=== $p\"; ls -ld \"$p/driver\" \"$p/of_node\"; cat \"$p/uevent\"; done",
    "usb-role-state.txt": "for p in /sys/class/udc/*/state /sys/class/udc/*/current_speed /sys/class/udc/*/maximum_speed /sys/class/usb_role/*/role /sys/class/typec/*/data_role /sys/class/typec/*/power_role /sys/class/typec/*/port_type /sys/class/extcon/*/name /sys/class/extcon/*/state; do [ -f \"$p\" ] || continue; echo \"=== $p\"; cat \"$p\"; done",
    "phy-inventory.txt": "ls -l /sys/class/phy; ls -l /sys/kernel/debug/phy; ls -l /sys/kernel/debug/usb; ls -l /sys/kernel/debug/musb*; ls -l /sys/bus/i2c/devices",
    "usb-gadget-config.txt": "for p in /config/usb_gadget/*/UDC /config/usb_gadget/*/idVendor /config/usb_gadget/*/idProduct; do [ -f \"$p\" ] || continue; echo \"=== $p\"; cat \"$p\"; done; ls -l /config/usb_gadget/*/configs/*",
    "modules.txt": "cat /proc/modules; ls /vendor/lib/modules; ls /vendor_dlkm/lib/modules",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="adb executable or full Windows adb.exe path")
    parser.add_argument("--serial", help="device serial when more than one device is attached")
    parser.add_argument("--out", type=Path, default=Path("lancelot-usb-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
    args = parser.parse_args()
    adb = [args.adb] + (["-s", args.serial] if args.serial else [])

    def run(command, timeout=45):
        return subprocess.run(adb + command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)

    devices = subprocess.run([args.adb, "devices", "-l"], capture_output=True, timeout=15)
    print(devices.stdout.decode(errors="replace"))
    state = run(["get-state"], 15)
    if state.returncode or state.stdout.strip() != b"device":
        sys.exit("No authorized device selected. Check adb devices, phone authorization, and --serial.")
    root = run(["exec-out", "su", "-c", "id"])
    if root.returncode or b"uid=0" not in root.stdout:
        sys.exit("Root unavailable. Authorize su on the phone, then rerun. " + root.stderr.decode(errors="replace"))

    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "adb-devices.txt").write_bytes(devices.stdout + devices.stderr)
    (args.out / "root-id.txt").write_bytes(root.stdout + root.stderr)
    manifest = {}
    for name, command in COMMANDS.items():
        try:
            # Pass a single remote command string. Passing su arguments
            # separately preserves extra quotes with some adb exec-out builds.
            result = run(["exec-out", "su -c " + shlex.quote(command)])
            (args.out / name).write_bytes(result.stdout)
            (args.out / (name + ".stderr.txt")).write_bytes(result.stderr)
            valid = None
            shell_error = any(message in result.stdout + result.stderr for message in
                              (b"inaccessible or not found", b"No such file or directory", b"Permission denied"))
            if name == "runtime.dtb":
                valid = result.stdout[:4] == bytes.fromhex("d00dfeed")
            elif name == "kernel.config.gz":
                valid = result.stdout[:2] == b"\x1f\x8b"
            manifest[name] = {"command": command, "returncode": result.returncode,
                              "bytes": len(result.stdout), "binary_magic_ok": valid,
                              "shell_error": shell_error}
            print(name, len(result.stdout), "bytes", "OK" if result.returncode == 0 and valid is not False and not shell_error else "partial/unavailable")
        except subprocess.TimeoutExpired:
            manifest[name] = {"command": command, "error": "timeout"}
            print(name, "timed out; continuing")
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    archive = args.out.with_name(args.out.name + ".zip")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(args.out.iterdir()):
            z.write(path, args.out.name + "/" + path.name)
    print("Capture:", archive.resolve())
    print("Missing debugfs/config entries are recorded; no mounts or device settings were changed.")


if __name__ == "__main__":
    main()
