# esp32-freedv2400b

A small, dependency-free, header-only FreeDV 2400B raw modem for Arduino and
PlatformIO. It converts between 48 kHz signed 16-bit discriminator/baseband PCM
and a 52-bit Codec2 payload packed MSB-first into seven bytes.

This library implements the FreeDV 2400B modem and Type-A framing layer only.
**It does not implement Codec2 speech encoding or decoding.**

## 48 kHz sample-clock requirement

FreeDV 2400B modem PCM is nominally 48,000 samples/s. The Codec2-compatible
timing loop consumes 1915, 1920, or 1925 samples for each 40 ms frame, providing
only about ±2600 ppm of long-term sample-clock correction.

Some legacy ESP32 built-in ADC configurations can be much farther from nominal.
On tested ESP32-WROOM boards, a 48,000 sample/s request produced roughly 47,600
sample/s—about -8300 ppm and outside the modem tracking range. This can look
like an RF or BER problem: synchronization slowly walks, frames shift, sync is
lost, and then it is reacquired.

Calibrate the ADC clock or resample the input close to 48 kHz instead of
widening the Codec2 timing loop blindly. For KV4P hardware, the recommended
diagnostic is
[`adc_auto_calibrate`](https://github.com/dkaukov/esp32-freedv2400b/tree/main/extras/kv4p/diagnostics/adc_auto_calibrate).
Measured requests such as 48188 or 48400 are board- and configuration-specific,
not universal settings.

`clockOffsetPpm` is the modem timing loop's estimate. Do not treat it as a
reliable clock measurement when the input clock is already outside the loop's
tracking range.

## Installation

For PlatformIO, add the Git repository directly:

```ini
lib_deps =
    https://github.com/dkaukov/esp32-freedv2400b.git
```

To pin the current published release:

```ini
lib_deps =
    https://github.com/dkaukov/esp32-freedv2400b.git#v1.0.2
```

For Arduino IDE, download the repository release as a ZIP, then select
**Sketch > Include Library > Add .ZIP Library**. This README does not assume the
library is listed in PlatformIO Registry or Arduino Library Manager.

## Receive

`processSamples()` accepts arbitrary chunk sizes. The demodulator internally
handles the decoder's changing 1915/1920/1925-sample requirement.

```cpp
#include <FreeDv2400b.h>

using namespace freedv2400b;

static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result) {
  if (result.frameType == FreeDv2400bFrameType::VOICE &&
      length == PAYLOAD_BYTES) {
    // Forward the seven-byte raw Codec2 payload.
  }
}

FreeDv2400bDemodulator demodulator(onFrame);

// From an audio/I2S task:
// demodulator.processSamples(samples, count);
```

VOICE callbacks contain seven bytes. DATA callbacks contain zero bytes because
this raw modem reports DATA detection but does not decode its contents. Callback
pointers are borrowed and valid only for the duration of the callback.

`uniqueWordErrors` is the error count for the 16-bit Type-A unique word.
`uniqueWordBerEma` is Codec2's smoothed estimate derived from that unique word;
it is not payload BER. The old `bitErrorRate` name remains as a deprecated
source-compatibility alias to the same stored value.

The receiver preserves Codec2 Type-A behavior, including normal/inverted
polarity acquisition and continued scheduled extraction after missed unique
words until the miss tolerance is exceeded.

## Transmit

TX fills caller-owned scratch space and emits exactly 1920 samples per frame.
The default levels are sample-exact with Codec2 at -16383/+16383.

```cpp
#include <FreeDv2400b.h>

using namespace freedv2400b;

static void onSamples(const int16_t *samples, size_t count) {
  // Write borrowed samples to I2S/DAC/radio audio.
}

FreeDv2400bModulator modulator(onSamples);
int16_t scratch[256];
uint8_t payload[PAYLOAD_BYTES] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x70
};

modulator.modulate(payload, sizeof(payload), scratch, 256);
```

The low nibble of payload byte 6 is ignored. `setMagnitude()` can select an
integer TX magnitude from 0 through 32767 without changing the default waveform.

## Memory and platforms

Modem RX/TX processing performs no heap allocation. The decoder is approximately
8 KiB. `FreeDv2400bDemodulator` is 12068 bytes on the tested 32-bit ESP32 ABI,
including its 3850-byte streaming FIFO, so instantiate it globally or statically.

CI builds the core library for ESP32, ESP32-S3, and ESP32-C3 with Arduino. The
core headers do not call ESP32-specific APIs.

## Examples and diagnostics

- [`examples/basic`](examples/basic/basic.ino): minimal RX and TX API usage.
- [`examples/kv4p_adc_rx`](examples/kv4p_adc_rx/README.md): KV4P/SA818 ADC receiver.
- [`extras/kv4p`](https://github.com/dkaukov/esp32-freedv2400b/tree/main/extras/kv4p): optional hardware calibration, capture,
  clock, and explicitly enabled RF diagnostics.
- [`test/hil/serial_recording_test`](https://github.com/dkaukov/esp32-freedv2400b/tree/main/test/hil/serial_recording_test): ESP32
  hardware-in-the-loop decoder firmware.

## Tests

```bash
pio test -e native
pio run -e esp32dev
pio run -e esp32s3
pio run -e esp32c3
```

The native suite covers sample-exact Codec2 TX, Type-A framing, inverted
polarity, arbitrary streaming chunks, reset, DATA handling, degraded channels,
and ±200 ppm resampling. It also decodes all 2,810 expected voice frames from
the real `ve9qrp_2400b.wav` recording.

With an ESP32 connected, the full serial HIL run is:

```bash
python tools/run_serial_recording_test.py --port /dev/cu.usbserial-0001
```

The large fixtures remain in GitHub for interoperability regression testing but
are excluded from PlatformIO library package exports.

## Compatibility, provenance, and licence

This is a direct C++ port of `freedv2400b-java`, itself based on Codec2 revision
`96e8a19c2487fd83bd981ce570f257aef42618f9`. The underlying Codec2/FMFSK and VHF
Type-A work is associated with David Rowe, with Brady O'Brien identified by
Codec2 sources where applicable. See [NOTICE](NOTICE) and the
[fixture provenance](https://github.com/dkaukov/esp32-freedv2400b/blob/main/test/fixtures/README.md)
for details.

The repository is licensed GPL-3.0-only.
