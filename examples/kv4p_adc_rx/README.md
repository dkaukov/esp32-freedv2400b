# KV4P-HT ADC receiver example

This example follows the ESP32-WROOM RX audio path in KV4P-HT's `rxAudio.h`:

- GPIO34: ADC1 channel 6 discriminator/audio input
- GPIO26: DAC channel 2, providing a 1.75 V ADC bias
- GPIO16/17: SA818 UART RX/TX
- GPIO18: active-low PTT, held high in receive mode
- GPIO19: SA818 power-down control, driven high to enable the module
- 48 kHz, mono, signed 16-bit samples
- 12 dB ADC attenuation, DC removal, and 16× input gain without explicit clipping

The ADC is sampled continuously; this example does not use or require the
KV4P-HT hardware squelch signal.

UART0 uses an 8 KiB transmit buffer and 1 KiB receive buffer so diagnostic
frame logging does not stall the ADC/decode loop during short bursts.

At startup the example configures an SA818 UHF module for 446.0000 MHz receive
and transmit frequency, 25 kHz wide mode, no CTCSS, squelch level zero, volume
eight, and disabled pre-emphasis/high-pass/low-pass filters. PTT remains high,
so the example never keys the transmitter.

The DAC bias assumes the KV4P-HT analog coupling/bias network. Do not directly
short GPIO26 to a radio discriminator output or to GPIO34; use the KV4P-HT
circuit or an equivalent AC-coupled bias network.

Build and upload with:

```text
pio run -d examples/kv4p_adc_rx -e esp32dev -t upload
pio device monitor -b 115200
```

The serial console prints VOICE payloads as fourteen hexadecimal digits and
reports DATA detection, synchronization, unique-word errors, SNR, and estimated
BER and sample-clock offset. `uw_ber_ema` is Codec2's smoothed estimate derived
from the 16-bit Type-A unique word, not a measurement of the unknown payload bits. The
seven-byte payload is raw Codec2 data; this library
does not decode speech.
