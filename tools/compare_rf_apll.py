#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare FreeDV 2400B RF reception with ESP32 ADC APLL on and off."""

import argparse
import pathlib
import re
import shutil
import subprocess
import time

import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples/kv4p_adc_rx"
FRAME_RE = re.compile(
    r"^(VOICE|DATA) sync=(\d) uw_errors=(\d+).*?clock=([-+0-9.]+) ppm")
PAYLOAD_ERRORS_RE = re.compile(r"payload_errors=(\d+)/52")


def upload(pio, port, environment):
    subprocess.run([pio, "run", "-d", str(PROJECT), "-e", environment,
                    "-t", "upload", "--upload-port", port], check=True)


def collect(port_name, seconds):
    stats = dict(lines=0, voice=0, data=0, exact=0, payload_errors=0,
                 uw_errors=0, synchronized=0, clock_sum=0.0, clock_count=0)
    with serial.Serial(port_name, 115200, timeout=0.25) as port:
        port.dtr = False
        port.rts = False
        port.reset_input_buffer()
        # Exclude radio initialization and ADC/DC-estimator startup.
        warmup_deadline = time.monotonic() + 4.0
        while time.monotonic() < warmup_deadline:
            port.readline()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("ascii", "replace").strip()
            match = FRAME_RE.match(line)
            if not match:
                continue
            kind, sync, uw, clock = match.groups()
            stats["lines"] += 1
            stats["uw_errors"] += int(uw)
            stats["synchronized"] += int(sync)
            stats["clock_sum"] += float(clock)
            stats["clock_count"] += 1
            if kind == "VOICE":
                stats["voice"] += 1
                payload_match = PAYLOAD_ERRORS_RE.search(line)
                errors = int(payload_match.group(1)) if payload_match else 52
                stats["payload_errors"] += errors
                if errors == 0:
                    stats["exact"] += 1
            else:
                stats["data"] += 1
    return stats


def report(label, stats, seconds):
    voices = stats["voice"]
    payload_bits = voices * 52
    print(
        f"RF_APLL mode={label} seconds={seconds:.1f} frames={stats['lines']} "
        f"voice={voices} exact={stats['exact']} data={stats['data']} "
        f"exact_rate={(stats['exact'] / voices if voices else 0):.6f} "
        f"payload_errors={stats['payload_errors']}/{payload_bits} "
        f"payload_ber={(stats['payload_errors'] / payload_bits if payload_bits else 0):.9f} "
        f"mean_uw_errors={(stats['uw_errors'] / stats['lines'] if stats['lines'] else 0):.3f} "
        f"sync_rate={(stats['synchronized'] / stats['lines'] if stats['lines'] else 0):.6f} "
        f"mean_clock_ppm={(stats['clock_sum'] / stats['clock_count'] if stats['clock_count'] else 0):.2f}",
        flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rx-port", required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--pio", default=shutil.which("pio") or "pio")
    args = parser.parse_args()
    for label, environment in (("apll", "esp32dev"),
                               ("no_apll", "esp32dev_no_apll")):
        upload(args.pio, args.rx_port, environment)
        result = collect(args.rx_port, args.seconds)
        report(label, result, args.seconds)


if __name__ == "__main__":
    main()
