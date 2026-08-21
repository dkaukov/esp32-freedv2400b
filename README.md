# esp32-freedv2400b

A small, dependency-free, header-only FreeDV 2400B raw modem for Arduino and
PlatformIO on ESP32, ESP32-S3, and ESP32-C3-class devices. It converts between
48 kHz signed 16-bit discriminator/baseband audio and a packed 52-bit Codec2
payload in seven bytes. **It does not implement Codec2 speech coding.**

The receiver preserves Codec2 Type-A synchronization, including continued
scheduled extraction on missed unique words until the fifth tracking miss,
normal/inverted polarity acquisition, timing correction (1915/1920/1925 input
samples), clock-offset estimation, and discriminator SNR estimation.

## Use

Include `<FreeDv2400b.h>`. `FreeDv2400bDemodulator::processSamples()` accepts
arbitrary chunk sizes. Its callback receives seven bytes for VOICE and zero
bytes for DATA. Payload and sample pointers are borrowed and remain valid only
during their callback. Keep the demodulator global/static: it owns its fixed RX
working memory and FIFO.

```cpp
static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result) {
  if (result.frameType == FreeDv2400bFrameType::VOICE) { /* use 7 bytes */ }
}
FreeDv2400bDemodulator demod(onFrame);
// demod.processSamples(samples, count);
```

TX fills caller-owned scratch space and emits exactly 1920 samples at levels
-16383/+16383. The low nibble of byte 6 is ignored.

```cpp
static void onSamples(const int16_t *p, size_t n) { /* write to I2S */ }
FreeDv2400bModulator mod(onSamples);
int16_t scratch[256];
uint8_t payload[7] = {0x11,0x22,0x33,0x44,0x55,0x66,0x70};
mod.modulate(payload, 7, scratch, 256);
```

There is no heap allocation in RX or TX processing. On a 32-bit target the
decoder is 8192 bytes on the native ABI tested. The streaming demodulator is
12064 bytes on ESP32 (12080 bytes on the native 64-bit ABI), including its
3850-byte input FIFO.

## Compatibility and attribution

This is a direct C++ port of `freedv2400b-java`, itself based on Codec2 revision
`96e8a19c2487fd83bd981ce570f257aef42618f9`. The FMFSK and VHF Type-A framing
work is from Codec2, copyright David Rowe, with Brady O'Brien identified as an
author where applicable. Golden interoperability data is Codec2-generated.

Native tests also decode Debian FreeDV 1.4.3's real `ve9qrp_2400b.wav`
recording, matching `freedv2400b-java` PR #2: acquisition on call two, one
acquisition total, continuous synchronization, and all 2,810 voice frames.

Licensed GPL-3.0-only. Run `pio test -e native`, `pio run -e esp32dev`, and
`pio run -e esp32s3`.
