#!/usr/bin/env python3
"""
serial_watch.py - print a device's serial output and stop when a line
contains the expected text (exit 0) or the timeout passes (exit 1).

    python tools/serial_watch.py COM5 --expect "SELFTEST PASSED" --timeout 20
"""
import argparse
import sys
import time

import serial  # pyserial (desktop-app/requirements.txt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--expect", default="")
    ap.add_argument("--fail", default="", help="stop with exit 2 when a line contains this")
    ap.add_argument("--timeout", type=float, default=20)
    args = ap.parse_args()

    deadline = time.time() + args.timeout
    with serial.Serial(args.port, args.baud, timeout=0.5) as port:
        while time.time() < deadline:
            line = port.readline().decode("utf-8", "replace").rstrip()
            if not line:
                continue
            print(line, flush=True)
            if args.fail and args.fail in line:
                sys.exit(2)
            if args.expect and args.expect in line:
                sys.exit(0)
    print(f"-- timed out after {args.timeout}s without '{args.expect}'")
    sys.exit(1)


if __name__ == "__main__":
    main()
