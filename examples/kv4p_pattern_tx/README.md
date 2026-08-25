# KV4P fixed-pattern transmitter

This ESP32-WROOM/KV4P-HT example configures the SA818 for 446.0000 MHz,
25 kHz bandwidth, no CTCSS, squelch zero, and all audio filters disabled. It
transmits continuous FreeDV 2400B voice frames containing the raw 52-bit Codec2
payload `11 22 33 44 55 66 70` for five seconds, then unkeys for five seconds.

TX audio uses ESP32 internal DAC channel 1 on the KV4P-HT GPIO25 audio path;
PTT is active-low on GPIO18. Audio Tools converts signed 16-bit modem samples
to the DAC's unsigned 8-bit representation.
The example emits 125 consecutive 1,920-sample frames during each keyed
interval, exactly 240,000 samples. No Codec2 speech encoding is performed.

Be sure that transmitting on 446 MHz is permitted by your licence and local
regulations, and connect a suitable antenna or dummy load before uploading.

```bash
pio run -d examples/kv4p_pattern_tx -e esp32dev
pio run -d examples/kv4p_pattern_tx -e esp32dev -t upload \
  --upload-port /dev/cu.usbserial-0001
```
