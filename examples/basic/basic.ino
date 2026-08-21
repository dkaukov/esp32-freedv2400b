// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <FreeDv2400b.h>

static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result) {
  if (result.frameType == FreeDv2400bFrameType::VOICE && length == 7) {
    // Forward the borrowed 52-bit Codec2 payload before this callback returns.
    (void)payload;
  }
}

static void onTxSamples(const int16_t *samples, size_t count) {
  // Write the borrowed discriminator/baseband samples to I2S here.
  (void)samples; (void)count;
}

FreeDv2400bDemodulator demod(onFrame); // Keep the large RX object static/global.
FreeDv2400bModulator mod(onTxSamples);
int16_t scratch[256];

void setup() {
  uint8_t payload[7] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x70};
  mod.modulate(payload, sizeof(payload), scratch, 256);
}

void loop() {
  // From an audio task: demod.processSamples(i2sSamples, sampleCount);
}
