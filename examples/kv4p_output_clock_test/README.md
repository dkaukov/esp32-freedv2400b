# KV4P output clock test

Measures the effective internal-DAC or PDM sample clock by counting
`I2S_EVENT_TX_DONE` DMA descriptor completions against `esp_timer_get_time()`.
It reports completed sample frames per second and error relative to 48 kHz every
two seconds. A dedicated producer task keeps all eight 256-frame DMA buffers
fed during the measurement.

```bash
pio run -d examples/kv4p_output_clock_test -e dac -t upload --upload-port PORT
pio run -d examples/kv4p_output_clock_test -e pdm -t upload --upload-port PORT
```

DAC uses GPIO25. PDM uses the KV4P pins GPIO25 (data) and GPIO27 (WS). This
diagnostic does not configure or key the radio module.

The measurement proves DMA/I2S pacing relative to the ESP32 high-resolution
timer. An independently calibrated frequency counter or receiver is still
needed to measure absolute error relative to an external time reference.
