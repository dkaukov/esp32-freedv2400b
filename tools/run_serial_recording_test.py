#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-only
"""Stream the real 2400B WAV to an ESP32 and verify all decoded frames."""

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import time
import wave
from collections import Counter

import serial


ROOT = pathlib.Path(__file__).resolve().parents[1]
WAV = ROOT / "test/fixtures/ve9qrp_2400b.wav"
FRAMES = ROOT / "test/fixtures/ve9qrp_2400b_voice_frames.bin"
PROJECT = ROOT / "test/hil/serial_recording_test"


def payload_bit_errors(actual, expected):
    """Count errors in the six full bytes and high nibble containing 52 bits."""
    errors = sum(bin(a ^ b).count("1") for a, b in zip(actual[:6], expected[:6]))
    return errors + bin((actual[6] ^ expected[6]) & 0xF0).count("1")


def read_protocol_line(port, deadline):
    while time.monotonic() < deadline:
        raw = port.readline()
        if raw:
            line = raw.decode("ascii", "replace").strip()
            if line.startswith(("FREEDV", "NEED=", "RESULT=", "DONE=", "ERROR=")):
                return line
    raise TimeoutError("timed out waiting for device")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--environment", default="esp32dev")
    parser.add_argument("--pio", default=shutil.which("pio") or "pio")
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--report-frames", action="store_true",
                        help="print UW and known-payload BER for every voice frame")
    args = parser.parse_args()

    if not args.skip_upload:
        subprocess.run([
            args.pio, "run", "-d", str(PROJECT), "-e", args.environment,
            "-t", "upload", "--upload-port", args.port,
        ], check=True)

    with wave.open(str(WAV), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2 or source.getframerate() != 48000:
            raise RuntimeError("fixture must be 48 kHz mono signed 16-bit PCM")
        pcm = source.readframes(source.getnframes())
        sample_count = source.getnframes()
    expected = FRAMES.read_bytes()
    if len(expected) != 2810 * 7:
        raise RuntimeError("decoded-frame fixture has the wrong size")

    with serial.Serial(args.port, 921600, timeout=1, write_timeout=10) as port:
        port.dtr = False
        port.rts = False
        deadline = time.monotonic() + 15
        while read_protocol_line(port, deadline) != "FREEDV2400B_SERIAL_RX_V1":
            pass
        port.write(struct.pack("<I", sample_count))

        offset = calls = synchronized_calls = acquisitions = first_sync = voices = 0
        total_payload_errors = mismatched_frames = 0
        frame_ber_counts = Counter()
        first_mismatch = None
        previously_synchronized = False
        started = time.monotonic()
        deadline = started + 240
        while True:
            line = read_protocol_line(port, deadline)
            if line.startswith("NEED="):
                count = int(line[5:])
                block = pcm[offset * 2:(offset + count) * 2]
                if len(block) != count * 2:
                    raise AssertionError("device requested PCM beyond EOF")
                port.write(block)
                offset += count
            elif line.startswith("RESULT="):
                fields = line[7:].split(",")
                call, synchronized, present, frame_type, errors = map(int, fields[:5])
                payload = fields[5]
                calls += 1
                if call != calls:
                    raise AssertionError(f"call sequence mismatch: {call} != {calls}")
                if synchronized:
                    synchronized_calls += 1
                    if not previously_synchronized:
                        acquisitions += 1
                        if not first_sync:
                            first_sync = calls
                if present and frame_type == 1:
                    wanted_bytes = expected[voices * 7:(voices + 1) * 7]
                    actual_bytes = bytes.fromhex(payload)
                    payload_errors = payload_bit_errors(actual_bytes, wanted_bytes)
                    total_payload_errors += payload_errors
                    frame_ber_counts[(errors, payload_errors)] += 1
                    if args.report_frames:
                        print(f"FRAME={voices} uw_errors={errors}/16 "
                              f"payload_errors={payload_errors}/52 "
                              f"payload_ber={payload_errors / 52.0:.6f}")
                    if payload_errors:
                        mismatched_frames += 1
                        if first_mismatch is None:
                            first_mismatch = (voices, payload, wanted_bytes.hex())
                    voices += 1
                previously_synchronized = bool(synchronized)
            elif line.startswith("DONE="):
                break
            elif line.startswith("ERROR="):
                raise RuntimeError(line)

    assert first_sync == 2, first_sync
    assert acquisitions == 1, acquisitions
    assert synchronized_calls == calls - 1, (synchronized_calls, calls)
    assert voices == 2810, voices
    assert previously_synchronized
    elapsed = time.monotonic() - started
    for (uw_errors, payload_errors), count in sorted(frame_ber_counts.items()):
        print(f"FRAME_BER uw_errors={uw_errors}/16 payload_errors={payload_errors}/52 "
              f"payload_ber={payload_errors / 52.0:.6f} frames={count}")
    print(f"PAYLOAD_BER total_errors={total_payload_errors}/{voices * 52} "
          f"ber={total_payload_errors / (voices * 52):.9f} "
          f"mismatched_frames={mismatched_frames}")
    if first_mismatch is not None:
        frame, actual, wanted = first_mismatch
        raise AssertionError(f"voice frame {frame} differs: {actual} != {wanted}")
    print(f"PASS calls={calls} voices={voices} first_sync={first_sync} acquisitions={acquisitions}")
    print(f"pcm_bytes={len(pcm)} serial_test_sec={elapsed:.3f}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise
