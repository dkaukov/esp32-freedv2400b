// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace freedv2400b {
static constexpr int SAMPLE_RATE = 48000;
static constexpr int PAYLOAD_BITS = 52;
static constexpr int PAYLOAD_BYTES = 7;
static constexpr int FRAME_BITS = 96;
static constexpr int TX_SAMPLES = 1920;
static constexpr int MAX_RX_INPUT = 1925;

enum class FreeDv2400bFrameType : uint8_t { NONE, VOICE, DATA };

struct FreeDv2400bDecodeResult {
    bool framePresent;
    bool synchronized;
    FreeDv2400bFrameType frameType;
    uint8_t uniqueWordErrors;
    float discriminatorSnrDb;
    float clockOffsetPpm;
};
}

#include "FreeDv2400bModulator.h"
#include "FreeDv2400bDemodulator.h"

