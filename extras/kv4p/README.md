# KV4P hardware diagnostics

This directory contains ESP32-WROOM/KV4P-HT hardware diagnostics used while
integrating the modem with the legacy built-in ADC, internal DAC/PDM output,
and SA818 radio module. They are not required to install or use the library.

`diagnostics/adc_auto_calibrate` is the preferred ADC clock calibration tool.
It searches integer sample-rate requests using DMA EOF timestamps.
`diagnostics/adc_calibrate` is retained as a faster two-pass estimate; the two
are related but not identical.

Other diagnostics capture ADC PCM, inspect ADC and output clocks, and transmit
a fixed FreeDV 2400B pattern. The RF transmitter is disabled unless the user
explicitly builds it with `ENABLE_RF_TX=1`.

Values such as 48188 or 48400 are observations from particular boards and
software configurations, not universal ESP32 sample-rate settings. Calibrate
the actual hardware and configuration being used.
