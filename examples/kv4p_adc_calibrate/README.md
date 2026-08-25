# KV4P ADC sample-rate calibration

This example measures the effective rate of the ESP32 legacy-I2S built-in ADC
against the ESP32 millisecond timer. No radio signal or second board is needed.

```bash
pio run -d examples/kv4p_adc_calibrate -t upload \
  --upload-port /dev/cu.usbserial-5
pio device monitor -p /dev/cu.usbserial-5 -b 115200
```

It drains the ADC DMA queue for 100 ms, then measures a 48,000 sample/s request
for one second using 16-sample reads and microsecond timing. It calculates:

```text
recommended = requested * 48000 / measured
```

The recommendation is rounded to the nearest 10 Hz for repeatability. It then
restarts the ADC at that rate and performs a second one-second verification.
The complete procedure takes about 2.2 seconds. Copy the printed value to `ADC_REQUEST_SAMPLE_RATE` in
`kv4p_adc_rx.ino` and `kv4p_adc_capture.ino`.

The two tested ESP32-WROOM boards measured about 47,600 sample/s for a 48,000
request and recommended approximately 48,400. Recalibrate after changing the
ESP32 family, Arduino core, Audio Tools version, I2S driver, or clock settings.

This is a practical firmware-clock calibration, not a traceable laboratory
measurement: the ADC clock and `millis()` are not fully independent. For higher
confidence, verify with a precise external tone or a separately clocked board.

## DAC and PDM output calibration

DAC and PDM output cannot be calibrated against `millis()` by counting writes:
the write call can return while samples remain queued in DMA. Use a separately
clocked, already calibrated receiver (or an oscilloscope/frequency counter):

1. Configure the TX output for 48,000 sample/s.
2. Generate a continuous 1,200 Hz sine using a phase step calculated for
   48,000 sample/s.
3. Capture at least two seconds on the calibrated RX ADC.
4. Measure the tone frequency, then calculate:

```text
recommended_tx_request = 48000 * 1200 / measured_tone_hz
```

5. Repeat once with the recommended request and verify the tone is 1,200 Hz.

Do this separately for internal DAC and PDM because they can use different I2S
clock/divider paths. Also verify DMA drain time before releasing radio PTT.

In the two-board RF tests for this repository, the internal DAC at a 48,000
request was already close enough; the receiver reported only about +400 to
+575 ppm residual relative clock offset after ADC calibration. The large
~8,300 ppm error was in the legacy built-in ADC path.
