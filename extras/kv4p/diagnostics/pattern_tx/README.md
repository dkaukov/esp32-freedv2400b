# KV4P fixed-pattern transmitter

This ESP32-WROOM/KV4P-HT diagnostic can configure the SA818 for 446.0000 MHz,
25 kHz bandwidth, no CTCSS, squelch zero, and all audio filters disabled. It
transmits repeated FreeDV 2400B voice frames containing the raw 52-bit Codec2
payload `11 22 33 44 55 66 70`, with a short DMA-drain/PTT transition between
125-frame bursts.

TX audio uses ESP32 internal DAC channel 1 on the KV4P-HT GPIO25 audio path;
PTT is active-low on GPIO18. Audio Tools converts signed 16-bit modem samples
to the DAC's unsigned 8-bit representation.
The example emits 125 consecutive 1,920-sample frames during each keyed
interval, exactly 240,000 samples. No Codec2 speech encoding is performed.

RF transmission is disabled in an untouched checkout. To opt in, build with
the compile-time definition `ENABLE_RF_TX=1`. The user is responsible for
selecting a frequency and operating parameters they are permitted to use.

```bash
pio run -d extras/kv4p/diagnostics/pattern_tx -e esp32dev
pio run -d extras/kv4p/diagnostics/pattern_tx -e esp32dev \
  -O "build_flags=-DENABLE_RF_TX=1" -t upload \
  --upload-port /dev/cu.usbserial-0001
```
