// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#ifndef FREEDV2400B_PUBLIC_TYPES
#include "FreeDv2400b.h"
#endif

#include <stddef.h>
#include <stdint.h>

namespace freedv2400b {
namespace detail {

class VhfTypeAFramer {
public:
    static inline void frame(const uint8_t *payload, uint8_t *bits) {
        static const uint8_t packed[12] = {
            0xa7, 0xa7, 0x00, 0x00, 0x00, 0x67,
            0xad, 0x00, 0x00, 0x00, 0x02, 0x72
        };
        for (int i = 0; i < FRAME_BITS; ++i)
            bits[i] = (packed[i >> 3] >> (7 - (i & 7))) & 1;
        for (int i = 0; i < 24; ++i) bits[16 + i] = payloadBit(payload, i);
        for (int i = 24; i < PAYLOAD_BITS; ++i) bits[56 + i - 24] = payloadBit(payload, i);
    }
private:
    static inline uint8_t payloadBit(const uint8_t *p, int i) {
        return (p[i >> 3] >> (7 - (i & 7))) & 1;
    }
};
}

class FreeDv2400bEncoder {
public:
    FreeDv2400bEncoder() : magnitude_(16383) {}
    int outputSamples() const { return TX_SAMPLES; }
    void setMagnitude(uint16_t magnitude) {
        magnitude_ = static_cast<int16_t>(magnitude > 32767 ? 32767 : magnitude);
    }
    int16_t magnitude() const { return magnitude_; }
    bool encode(const uint8_t *payload, size_t length, int16_t *output,
                size_t capacity = TX_SAMPLES) {
        if (!payload || length < PAYLOAD_BYTES || !output || capacity < TX_SAMPLES) return false;
        uint8_t bits[FRAME_BITS];
        detail::VhfTypeAFramer::frame(payload, bits);
        size_t p = 0;
        for (int i = 0; i < FRAME_BITS; ++i) {
            const int16_t a = bits[i] ? magnitude_ : static_cast<int16_t>(-magnitude_);
            for (int j = 0; j < 10; ++j) output[p++] = a;
            for (int j = 0; j < 10; ++j) output[p++] = static_cast<int16_t>(-a);
        }
        return true;
    }
    void reset() {}
private:
    int16_t magnitude_;
};

typedef void (*FreeDv2400bSampleCallback)(const int16_t *samples, size_t count);

class FreeDv2400bModulator {
public:
    explicit FreeDv2400bModulator(FreeDv2400bSampleCallback callback = 0)
        : callback_(callback), magnitude_(16383) {}

    void setMagnitude(uint16_t magnitude) {
        magnitude_ = static_cast<int16_t>(magnitude > 32767 ? 32767 : magnitude);
    }
    int16_t magnitude() const { return magnitude_; }

    bool modulate(const uint8_t *payload, size_t length, int16_t *buffer, size_t chunkSize) {
        if (!payload || length < PAYLOAD_BYTES || !buffer || !chunkSize || !callback_) return false;
        uint8_t bits[FRAME_BITS];
        detail::VhfTypeAFramer::frame(payload, bits);
        size_t used = 0;
        for (int bit = 0; bit < FRAME_BITS; ++bit) {
            const int16_t a = bits[bit] ? magnitude_ : static_cast<int16_t>(-magnitude_);
            for (int half = 0; half < 2; ++half) {
                const int16_t level = half ? static_cast<int16_t>(-a) : a;
                for (int j = 0; j < 10; ++j) {
                    buffer[used++] = level;
                    if (used == chunkSize) { callback_(buffer, used); used = 0; }
                }
            }
        }
        if (used) callback_(buffer, used);
        return true;
    }
    void reset() {}
private:
    FreeDv2400bSampleCallback callback_;
    int16_t magnitude_;
};
}

using freedv2400b::FreeDv2400bEncoder;
using freedv2400b::FreeDv2400bModulator;
using freedv2400b::FreeDv2400bSampleCallback;
