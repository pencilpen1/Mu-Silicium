#!/usr/bin/env python3
"""Capture lancelot UEFI logs over USB interface 0, EP 0x01 OUT / 0x81 IN.

Runs inside WSL/Linux using native Linux fastboot and Python's standard library.
On Ubuntu: sudo apt install fastboot. The UEFI USB device must be visible to Linux.
Start before booting UEFI; Ctrl+C stops capture. See docs/lancelot_usb_logging.md.
"""
import argparse
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
import time


SNAPSHOT_SIZE = 4096
PREFIX = re.compile(r"^\s*(?:\(bootloader\)\s*|INFO\s*)?")
HEADER = re.compile(r"LLOG1 ([0-9a-fA-F]{16}) ([0-9a-fA-F]{16}) ([0-9a-fA-F]{16})")
DATA = re.compile(r"LLOGD ((?:[0-9a-fA-F]{2}){1,24})")


class ProtocolError(RuntimeError):
    pass


class UnsupportedFirmware(RuntimeError):
    pass


@dataclass(frozen=True)
class Batch:
    boot_id: int
    start: int
    end: int
    payload: bytes

    @property
    def next_cursor(self):
        return self.start + len(self.payload)


def parse_reply(output: bytes, *, partial=False) -> Batch:
    """Validate framing/length before committing bytes or advancing a cursor.

    INFO uses hex so fastboot's line prefixes and newline conversions cannot
    alter firmware bytes. A timed-out command may contain valid partial data.
    """
    header = None
    payload = bytearray()
    if partial and not output.endswith((b"\n", b"\r")):
        # A killed fastboot may have written only part of its last INFO line.
        output = output[:output.rfind(b"\n") + 1]
    for line in output.decode("utf-8", errors="replace").splitlines():
        line = PREFIX.sub("", line, count=1).strip()
        if line.startswith("LLOG1"):
            match = HEADER.fullmatch(line)
            if header is not None or match is None:
                raise ProtocolError("Invalid or duplicate LLOG1 header")
            header = tuple(int(v, 16) for v in match.groups())
            _, start, end = header
            if end < start or end - start > SNAPSHOT_SIZE:
                raise ProtocolError("Invalid log sequence range")
        elif line.startswith("LLOGD"):
            match = DATA.fullmatch(line)
            if header is None or match is None:
                raise ProtocolError("Invalid LLOGD packet or missing header")
            payload.extend(bytes.fromhex(match[1]))
            if len(payload) > header[2] - header[1]:
                raise ProtocolError("Log payload exceeds advertised length")
    if header is None:
        raise ProtocolError("No LLOG1 header; firmware must support USB log v0.3")
    if not partial and len(payload) != header[2] - header[1]:
        raise ProtocolError("Incomplete log snapshot")
    return Batch(*header, bytes(payload))


def check_usb_access(output):
    text = output.decode("utf-8", errors="replace")
    if any(message in text.lower() for message in
           ("no permissions", "permission denied", "insufficient permissions")):
        raise PermissionError(
            "Linux cannot access the USB device. Check udev rules and your plugdev "
            "group membership; see docs/lancelot_usb_logging.md. " + text.strip()[-400:])


def fetch_batch(fastboot, serial, cursor, timeout):
    command = [fastboot, "-s", serial, "oem", "log", f"{cursor:016x}"]
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
    except FileNotFoundError as exc:
        raise FileNotFoundError(
            f"Linux fastboot not found: {fastboot}. On Ubuntu/WSL run "
            "'sudo apt install fastboot', or use --fastboot /path/to/fastboot.") from exc
    except subprocess.TimeoutExpired as exc:
        check_usb_access(exc.output or b"")
        # Preserve already-received bytes if firmware hangs halfway through a
        # reply. Never advance beyond complete, validated LLOGD packets.
        try:
            batch = parse_reply(exc.output or b"", partial=True)
        except ProtocolError:
            raise TimeoutError(
                "Waiting for UEFI USB log device / response. Check 'fastboot devices -l' "
                "in this Linux environment; WSL needs the UEFI USB device forwarded.") from exc
        if not batch.payload:
            raise TimeoutError("UEFI USB log response timed out") from exc
        return batch, "Timed out: saved partial log; waiting to reconnect/resume"

    check_usb_access(result.stdout)
    if result.returncode:
        detail = result.stdout.decode("utf-8", errors="replace").strip()[-800:]
        if "diagnostic getvar" in detail or "unknown command" in detail.lower():
            raise UnsupportedFirmware("Install the USB log v0.3 image first: " + detail)
        try:
            batch = parse_reply(result.stdout, partial=True)
        except ProtocolError:
            raise ConnectionError(detail or f"fastboot exited {result.returncode}")
        return batch, "USB command interrupted; retained validated partial log"
    return parse_reply(result.stdout), None


class Capture:
    def __init__(self, output, echo=True):
        self.output = output
        self.echo = echo
        self.boot_id = None
        self.cursor = 0

    def note(self, text):
        line = f"[capture {datetime.now().isoformat(timespec='seconds')}] {text}\n"
        print(line.rstrip(), file=sys.stderr, flush=True)
        self.output.write(("\n" + line).encode("utf-8"))
        self.output.flush()

    def consume(self, batch):
        """Return False to re-fetch from zero after detecting another boot."""
        previous_boot = self.boot_id
        if batch.boot_id != previous_boot:
            self.boot_id = batch.boot_id
            self.cursor = 0
            self.note(f"BOOT {batch.boot_id:016x}")
            if previous_boot is not None:
                # The old cursor might land inside the new boot's log; re-read
                # from zero before accepting anything from that new boot.
                return False
        if batch.start > self.cursor:
            self.note(f"LOST {batch.start - self.cursor} bytes (ring overwritten)")
            self.cursor = batch.start
        skip = max(0, self.cursor - batch.start)
        payload = batch.payload[skip:]
        if payload:
            self.output.write(payload)
            self.output.flush()
            if self.echo:
                sys.stdout.write(payload.decode("utf-8", errors="replace"))
                sys.stdout.flush()
        self.cursor = max(self.cursor, batch.next_cursor)
        return True


def positive(value):
    number = float(value)
    if not 0 < number < float("inf"):
        raise argparse.ArgumentTypeError("must be a finite positive number")
    return number


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fastboot", default="fastboot", help="Linux fastboot executable (default: fastboot from PATH)")
    parser.add_argument("--serial", default="lancelot-uefi", help="fastboot -s device selector")
    parser.add_argument("-o", "--output", type=Path,
                        default=Path(datetime.now().strftime("lancelot-boot-%Y%m%d-%H%M%S.log")),
                        help="Output file (appended, never truncated)")
    parser.add_argument("--interval", type=positive, default=0.2, help="Idle polling interval in seconds")
    parser.add_argument("--timeout", type=positive, default=15.0, help="Per-command timeout in seconds")
    parser.add_argument("--once", action="store_true", help="Drain the retained log then exit")
    parser.add_argument("--quiet", action="store_true", help="Save log without echoing it to the terminal")
    args = parser.parse_args(argv)

    try:
        with args.output.open("ab") as output:
            capture = Capture(output, echo=not args.quiet)
            capture.note(f"USB interface=0 OUT=0x01 IN=0x81 serial={args.serial}; file={args.output}")
            last_error = None
            while True:
                try:
                    batch, warning = fetch_batch(args.fastboot, args.serial, capture.cursor, args.timeout)
                    last_error = None
                    if not capture.consume(batch):
                        continue
                    if warning:
                        capture.note(warning)
                        if args.once:
                            return 1
                        time.sleep(args.interval)
                        continue
                    if len(batch.payload) < SNAPSHOT_SIZE:
                        if args.once:
                            return 0
                        time.sleep(args.interval)
                except (TimeoutError, ConnectionError, ProtocolError) as exc:
                    text = str(exc)
                    if text != last_error:
                        capture.note(text)
                        last_error = text
                    if args.once:
                        return 1
                    time.sleep(max(1.0, args.interval))
    except KeyboardInterrupt:
        print("\nCapture stopped; log saved.", file=sys.stderr)
        return 0
    except (OSError, UnsupportedFirmware) as exc:
        print(f"Capture error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
