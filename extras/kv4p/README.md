# KV4P hardware diagnostics

This directory contains ESP32-WROOM/KV4P-HT hardware diagnostics used while
integrating the modem with the legacy built-in ADC, internal DAC/PDM output,
and SA818 radio module. They are not required to install or use the library.

`diagnostics/adc_auto_calibrate` is an experimental work-in-progress. It
searches integer sample-rate requests using DMA EOF timestamps, but its reported
clock optimum has not yet correlated reliably with the best real RF decode
result. On the currently tested boards, a request of 48400 performs better in
practice than the automatic result. Use that only as a starting point and
validate it on the complete receive path.

`diagnostics/adc_calibrate` is a faster two-pass estimate. The tools overlap,
but neither should currently be treated as authoritative; keep both until the
measurement discrepancy is understood.

Other diagnostics capture ADC PCM, inspect ADC and output clocks, and transmit
a fixed FreeDV 2400B pattern. The RF transmitter is disabled unless the user
explicitly builds it with `ENABLE_RF_TX=1`.

Values such as 48188 or 48400 are observations from particular boards and
software configurations, not universal ESP32 sample-rate settings. Calibrate
and verify the actual hardware and complete audio/RF configuration being used.
