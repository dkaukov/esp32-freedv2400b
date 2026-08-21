// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Arduino.h>

// Run-length representation of two Codec2-generated 2400B PCM frames. Each
// bit expands to ten samples at (bit ? +16383 : -16383), followed by ten at
// the opposite level. This is the exact first 3840 PCM samples of the golden
// fixture, compressed from 7680 bytes to 24 bytes per frame.
static const uint8_t CODEC2_PCM_FRAME_BITS[24] PROGMEM = {
  0xa7,0xa7,0xa3,0x15,0x6e,0x67,0xad,0x07,0xb5,0x05,0xc0,0x72,
  0xa7,0xa7,0x00,0xa3,0x87,0x67,0xad,0xb1,0x9f,0x42,0x70,0x72
};
