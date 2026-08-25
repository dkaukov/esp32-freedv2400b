# KV4P ADC DMA clock test

Measures the ESP32 legacy I2S ADC sample rate by counting
`I2S_EVENT_RX_DONE` DMA descriptor completions against
`esp_timer_get_time()`. A dedicated consumer task continuously drains the ADC.
The report also includes RX queue overflow and DMA error counts.

```bash
pio run -d examples/kv4p_adc_dma_clock_test -e request_48000 -t upload --upload-port PORT
pio run -d examples/kv4p_adc_dma_clock_test -e request_48400 -t upload --upload-port PORT
pio run -d examples/kv4p_adc_dma_clock_test -e request_48188 -t upload --upload-port PORT
pio run -d examples/kv4p_adc_dma_clock_test -e request_48190 -t upload --upload-port PORT
```

The KV4P audio input is GPIO34. GPIO26 supplies the normal 1.75 V ADC bias.
This diagnostic does not configure the radio module or decode modem frames.

The result measures ADC/I2S pacing relative to the ESP32 high-resolution timer.
Use an external time reference when absolute crystal accuracy is required.
