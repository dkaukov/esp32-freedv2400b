// SPDX-License-Identifier: LGPL-2.1-only
#include <Arduino.h>
#include <unity.h>
#include <FreeDv2400b.h>
#include "codec2_pcm_vector.h"

static uint8_t received[7];
static bool present;
static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result) {
  if (result.frameType == FreeDv2400bFrameType::VOICE && length == 7) {
    memcpy(received, payload, 7); present = true;
  }
}
static void test_codec2_flash_vector_and_speed() {
  static FreeDv2400bDemodulator demod(onFrame);
  static int16_t pcm[1920];
  uint32_t start = micros();
  for (int frame = 0; frame < 2; ++frame) {
    int p = 0;
    for (int i = 0; i < 96; ++i) {
      bool bit = (pgm_read_byte(&CODEC2_PCM_FRAME_BITS[frame * 12 + (i >> 3)]) >> (7 - (i & 7))) & 1;
      int16_t a = bit ? 16383 : -16383;
      for (int j = 0; j < 10; ++j) pcm[p++] = a;
      for (int j = 0; j < 10; ++j) pcm[p++] = -a;
    }
    demod.processSamples(pcm, 1920);
  }
  uint32_t elapsed = micros() - start;
  const uint8_t expected[7] = {0xa3,0x15,0x6e,0x07,0xb5,0x05,0xc0};
  TEST_ASSERT_TRUE(present);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, received, 7);
  // Two 1,920-sample frames are 80 ms of audio. Require at least 2x real time.
  TEST_ASSERT_LESS_THAN(40000, elapsed);
  Serial.printf("audio_sec=0.080 decode_sec=%.6f rt_factor=%.3f cpu_pct=%.1f\n",
                elapsed / 1000000.0, elapsed / 80000.0, elapsed / 800.0);
}
void setup() { delay(1000); UNITY_BEGIN(); RUN_TEST(test_codec2_flash_vector_and_speed); UNITY_END(); }
void loop() {}
