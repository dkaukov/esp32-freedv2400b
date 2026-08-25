#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture KV4P Audio Tools ADC PCM, save WAV, and measure a known tone."""

import argparse
import pathlib
import shutil
import struct
import subprocess
import time
import wave

import numpy as np
import serial

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT = ROOT / "extras/kv4p/diagnostics/adc_capture"
NOMINAL_RATE = 48000


def read_capture(port):
    time.sleep(9.0)  # Firmware discards uploader residue for eight seconds.
    port.reset_input_buffer()
    port.write(b"GO!\n")
    framing = bytearray()
    deadline = time.monotonic() + 5.0
    while b"ADC8" not in framing and time.monotonic() < deadline:
        framing.extend(port.read(4096))
        if len(framing) > 65536:
            del framing[:-8]
    pos = framing.find(b"ADC8")
    if pos < 0:
        raise RuntimeError("ADC8 capture header not received")
    while len(framing) < pos + 8:
        framing.extend(port.read(pos + 8 - len(framing)))
    count = struct.unpack_from("<I", framing, pos + 4)[0]
    pcm = bytearray(framing[pos + 8:])
    deadline = time.monotonic() + max(30.0, count / 20000.0)
    while len(pcm) < count and time.monotonic() < deadline:
        pcm.extend(port.read(min(16384, count - len(pcm))))
    if len(pcm) != count:
        if len(pcm) < count * 9 // 10:
            raise RuntimeError(f"capture truncated: {len(pcm)}/{count} samples")
        print(f"WARNING capture_short={len(pcm)}/{count}; analyzing received PCM")
    return bytes(pcm)


def measure_tone(pcm, expected_hz):
    samples = np.frombuffer(pcm, dtype=np.int8).astype(np.float64)
    trim = NOMINAL_RATE
    if len(samples) <= 2 * trim:
        raise RuntimeError("capture is too short")
    samples = samples[trim:-trim]
    samples -= np.mean(samples)
    samples *= np.hanning(len(samples))
    spectrum = np.abs(np.fft.rfft(samples))
    frequencies = np.fft.rfftfreq(len(samples), 1.0 / NOMINAL_RATE)
    wanted = np.flatnonzero((frequencies >= expected_hz - 100.0) &
                            (frequencies <= expected_hz + 100.0))
    peak = wanted[np.argmax(spectrum[wanted])]
    a, b, c = np.log(spectrum[peak - 1:peak + 2] + 1e-30)
    offset = 0.5 * (a - c) / (a - 2.0 * b + c)
    measured_hz = (peak + offset) * NOMINAL_RATE / len(samples)
    # A known analog tone appears high when the ADC sample clock is slow.
    effective_rate = NOMINAL_RATE * expected_hz / measured_hz
    clock_ppm = (effective_rate / NOMINAL_RATE - 1.0) * 1.0e6
    rms = float(np.sqrt(np.mean(samples * samples)))
    return measured_hz, effective_rate, clock_ppm, rms


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("adc_1200hz.wav"))
    parser.add_argument("--tone-hz", type=float, default=1200.0)
    parser.add_argument("--pio", default=shutil.which("pio") or "pio")
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--inject-tone", action="store_true",
                        help="replace ADC values on-device with nominal 1200 Hz PCM")
    parser.add_argument("--no-apll", action="store_true",
                        help="use the no-APLL injected-tone environment")
    args = parser.parse_args()
    if not args.skip_upload:
        if args.inject_tone and args.no_apll:
            environment = "injected_tone_no_apll"
        else:
            environment = "injected_tone" if args.inject_tone else "esp32dev"
        subprocess.run([args.pio, "run", "-d", str(PROJECT), "-e", environment,
                        "-t", "upload", "--upload-port", args.port], check=True)
    with serial.Serial(args.port, 921600, timeout=1, write_timeout=2) as port:
        port.dtr = False
        port.rts = False
        pcm = read_capture(port)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.output), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(1)
        output.setframerate(NOMINAL_RATE)
        output.writeframes(bytes((value + 128) & 0xFF for value in pcm))
    measured, effective, ppm, rms = measure_tone(pcm, args.tone_hz)
    print(f"WAV={args.output} samples={len(pcm)} nominal_rate={NOMINAL_RATE}")
    print(f"TONE expected={args.tone_hz:.6f}_Hz measured={measured:.6f}_Hz rms={rms:.3f}")
    if args.inject_tone:
        print("MODE=injected_tone result validates PCM ordering/capture, not ADC clock")
        print(f"PCM nominal_rate={effective:.3f}_sps apparent_error={ppm:+.1f}_ppm")
    else:
        print(f"ADC effective_rate={effective:.3f}_sps clock_error={ppm:+.1f}_ppm")


if __name__ == "__main__":
    main()
