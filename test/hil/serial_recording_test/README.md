# ESP32 serial recording test

This hardware-in-the-loop firmware receives the checked-in 48 kHz discriminator
PCM fixture over a 921600-baud serial connection and feeds it to the low-level
adaptive decoder. It is driven by `tools/run_serial_recording_test.py`; it is
not a normal Arduino library example.

From the repository root:

```bash
python tools/run_serial_recording_test.py --port PORT
```

The host verifies acquisition behavior and all 2,810 expected seven-byte voice
payloads from `test/fixtures/ve9qrp_2400b.wav`.
