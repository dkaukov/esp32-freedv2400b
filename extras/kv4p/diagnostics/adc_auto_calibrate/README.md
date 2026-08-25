# KV4P automatic ADC clock calibration (experimental)

This work-in-progress finds the integer legacy-I2S ADC request that produces the DMA
sample rate closest to 48 kHz. It measures a baseline, brackets the target,
binary-searches by measured clock error, then compares every remaining adjacent
integer request with equal 500-EOF measurements. The shorter binary-search
probes use 250 EOF intervals. A typical complete search takes about 20 seconds.
The reported rate is a least-squares fit across every EOF timestamp, rather
than a first-to-last calculation, to suppress FreeRTOS wake-up jitter.

```bash
pio run -d extras/kv4p/diagnostics/adc_auto_calibrate -e esp32dev -t upload --upload-port PORT
pio device monitor --port PORT --baud 115200
```

The default matches the KV4P receiver and keeps the ESP32 Audio PLL enabled.
For diagnostic comparison without APLL:

```bash
pio run -d extras/kv4p/diagnostics/adc_auto_calibrate -e esp32dev_no_apll -t upload --upload-port PORT
```

The final line reports the best DMA-clock candidate measured by this experiment:

```text
ADC_AUTO_CAL measured_best_request=48188 measured_rate=47999.9_sps error=-2.0_ppm overflow=0 dma_error=0 experimental=1
```

The measurement uses `I2S_EVENT_RX_DONE` descriptor completions relative to
`esp_timer_get_time()`. GPIO34 is the KV4P audio input and GPIO26 supplies the
normal 1.75 V ADC bias. The radio module is not configured.

Do not copy this value blindly into the receiver. The DMA-EOF optimum has not
yet correlated reliably with end-to-end RF decoding; a request of 48400 has
performed better on the currently tested hardware. That value is also only a
starting point, not a universal setting. The adjacent `adc_calibrate` sketch is
a faster, less exhaustive two-pass estimate; both remain available while this
discrepancy is investigated.
