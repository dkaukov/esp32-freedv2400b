// SPDX-License-Identifier: LGPL-2.1-only
#pragma once

#include "FreeDv2400b.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace freedv2400b {
namespace detail {

class MovingSum {
public:
    MovingSum() { reset(); }
    int32_t push(int32_t sample) {
        sum_ = sum_ - samples_[pointer_] + sample;
        samples_[pointer_] = sample;
        if (++pointer_ == 10) pointer_ = 0;
        return sum_;
    }
    void reset() { memset(samples_, 0, sizeof(samples_)); pointer_ = 0; sum_ = 0; }
private:
    int32_t samples_[10];
    int pointer_;
    int32_t sum_;
};

class ManchesterTimingEstimator {
public:
    static inline float estimate(const int32_t *filtered, int length) {
        static const float c[10] = {1.0f,.809016994f,.309016994f,-.309016994f,-.809016994f,-1.0f,-.809016994f,-.309016994f,.309016994f,.809016994f};
        static const float s[10] = {0.0f,.587785252f,.951056516f,.951056516f,.587785252f,0.0f,-.587785252f,-.951056516f,-.951056516f,-.587785252f};
        float re = 0, im = 0;
        for (int i = 0; i < length; ++i) {
            float v = static_cast<float>(filtered[i]);
            float sq = v * v;
            re += sq * c[i % 10]; im += sq * s[i % 10];
        }
        return atan2f(im, re) / 6.2831853071795864769f - .42f;
    }
};

class VhfTypeADeframer {
public:
    VhfTypeADeframer() { reset(); }
    bool accept(const uint8_t *in, uint8_t *payload) {
        bool extracted = false;
        for (int i = 0; i < FRAME_BITS; ++i) {
            normal_[ptr_] = in[i]; inverted_[ptr_] = in[i] ^ 1; ptr_ = (ptr_ + 1) % FRAME_BITS;
            if (sync_) {
                if (++lastUw_ == FRAME_BITS) {
                    lastUw_ = 0;
                    bool matched = match(onInverted_ ? inverted_ : normal_, 3);
                    if (matched) misses_ = 0; else ++misses_;
                    if (misses_ > 4) sync_ = false;
                    type_ = matchType_; errors_ = matchErrors_; updateBer(); extracted = true;
                    if (type_ == FreeDv2400bFrameType::VOICE) extract(onInverted_ ? inverted_ : normal_, payload);
                }
            } else {
                if (match(inverted_, 1)) { acquire(true); extracted = true; if (type_ == FreeDv2400bFrameType::VOICE) extract(inverted_, payload); }
                if (match(normal_, 1)) { acquire(false); extracted = true; if (type_ == FreeDv2400bFrameType::VOICE) extract(normal_, payload); }
            }
        }
        if (!extracted) { type_ = FreeDv2400bFrameType::NONE; errors_ = 0; }
        return extracted;
    }
    bool synchronizedNow() const { return sync_; }
    FreeDv2400bFrameType frameType() const { return type_; }
    uint8_t errors() const { return errors_; }
    float uniqueWordBerEma() const { return ber_; }
    // Deprecated compatibility accessor.
    float bitErrorRate() const { return uniqueWordBerEma(); }
    void reset() {
        memset(normal_, 0, sizeof(normal_)); memset(inverted_, 0, sizeof(inverted_));
        ptr_ = lastUw_ = misses_ = errors_ = matchErrors_ = 0; sync_ = onInverted_ = false; ber_ = 0;
        type_ = matchType_ = FreeDv2400bFrameType::NONE;
    }
private:
    void acquire(bool inverted) { sync_ = true; lastUw_ = misses_ = 0; onInverted_ = inverted; type_ = matchType_; errors_ = matchErrors_; updateBer(); }
    void updateBer() { ber_ = .995f * ber_ + .005f * (static_cast<float>(errors_) / 16.0f); }
    bool match(const uint8_t *bits, int tolerance) {
        int voice = 0, data = 0;
        for (int i = 0; i < 16; ++i) {
            int b = bits[(ptr_ + 40 + i) % FRAME_BITS];
            voice += b ^ ((0x67ad >> (15 - i)) & 1); data += b ^ ((0xf1fc >> (15 - i)) & 1);
        }
        bool v = voice < data; matchErrors_ = v ? voice : data;
        matchType_ = v ? FreeDv2400bFrameType::VOICE : FreeDv2400bFrameType::DATA;
        return matchErrors_ <= tolerance;
    }
    void extract(const uint8_t *bits, uint8_t *out) {
        memset(out, 0, PAYLOAD_BYTES);
        for (int i = 0; i < PAYLOAD_BITS; ++i) {
            int pos = i < 24 ? ptr_ + 16 + i : ptr_ + 56 + i - 24;
            out[i >> 3] |= bits[pos % FRAME_BITS] << (7 - (i & 7));
        }
        out[6] &= 0xf0;
    }
    uint8_t normal_[FRAME_BITS], inverted_[FRAME_BITS];
    int ptr_, lastUw_, misses_, errors_, matchErrors_;
    float ber_;
    bool sync_, onInverted_;
    FreeDv2400bFrameType type_, matchType_;
};
}

class FreeDv2400bDecoder {
public:
    FreeDv2400bDecoder() { reset(); }
    int inputSamplesRequired() const { return required_; }
    int maximumInputSamples() const { return MAX_RX_INPUT; }
    bool decode(const int16_t *input, size_t count, uint8_t *payload, FreeDv2400bDecodeResult &result) {
        if (!input || count < static_cast<size_t>(required_) || !payload) return false;
        const int retained = 1960 - required_, tailOffset = 45 - retained;
        for (int i = 0; i < 1930 + 9; ++i) {
            int sample = i < retained ? tail_[tailOffset + i] : input[i - retained];
            int32_t sum = boxcar_.push(sample);
            if (i >= 9) filtered_[i - 9] = sum;
        }
        memcpy(tail_, input + required_ - 45, sizeof(tail_));
        float timing = detail::ManchesterTimingEstimator::estimate(filtered_, 1930);
        int rxTiming = static_cast<int>(roundf(timing * 10));
        float delta = timing - previousTiming_; previousTiming_ = timing;
        if (fabsf(delta) < .2f) ppm_ = .9f * ppm_ + .1f * (1000000.0f * delta / 192.0f);
        required_ = timing > -.2f ? 1925 : (timing < -.65f ? 1915 : 1920);
        int offset = 14 + rxTiming;
        float last = lastOdd_, lastAbs = fabsf(last), even = 0, odd = 0, signal = 0, noise = 0;
        for (int i = 0; i < 192; ++i) {
            float current = static_cast<float>(filtered_[offset + 10 * i]);
            float diff = last - current; int bit = diff > 0 ? 1 : 0; last = current;
            signal += current * current; float a = fabsf(current), nd = a - lastAbs; noise += nd * nd; lastAbs = a;
            float confidence = fabsf(diff);
            if (i & 1) { even += confidence; bits_[i >> 1] |= bit; }
            else { odd += confidence; bits_[i >> 1] = static_cast<uint8_t>(bit << 1); }
        }
        for (int i = 0; i < FRAME_BITS; ++i) bits_[i] = even > odd ? (bits_[i] & 1) : ((bits_[i] & 2) >> 1);
        lastOdd_ = last; noise *= .5f;
        float currentSnr = 10.0f * log10f((signal + 1e-6f / 3.1f) / (noise + 1e-6f));
        snr_ = snr_ < .1f ? currentSnr : .9f * snr_ + .1f * currentSnr;
        bool present = deframer_.accept(bits_, payload);
        result.framePresent = present; result.synchronized = deframer_.synchronizedNow();
        result.frameType = present ? deframer_.frameType() : FreeDv2400bFrameType::NONE;
        result.uniqueWordErrors = present ? deframer_.errors() : 0;
        result.uniqueWordBerEma = deframer_.uniqueWordBerEma();
        result.discriminatorSnrDb = snr_; result.clockOffsetPpm = ppm_;
        return true;
    }
    void reset() {
        memset(tail_, 0, sizeof(tail_)); memset(filtered_, 0, sizeof(filtered_)); memset(bits_, 0, sizeof(bits_));
        boxcar_.reset(); required_ = TX_SAMPLES; lastOdd_ = previousTiming_ = ppm_ = snr_ = 0; deframer_.reset();
    }
private:
    int16_t tail_[45]; int32_t filtered_[1930]; uint8_t bits_[FRAME_BITS];
    detail::MovingSum boxcar_; detail::VhfTypeADeframer deframer_; int required_;
    float lastOdd_, previousTiming_, ppm_, snr_;
};

typedef void (*FreeDv2400bFrameCallback)(const uint8_t *, size_t, const FreeDv2400bDecodeResult &);

class FreeDv2400bDemodulator {
public:
    explicit FreeDv2400bDemodulator(FreeDv2400bFrameCallback callback = 0) : callback_(callback), buffered_(0), sync_(false) {}
    void processSamples(const int16_t *samples, size_t count) {
        if (!samples) return;
        while (count) {
            size_t need = static_cast<size_t>(decoder_.inputSamplesRequired()) - buffered_;
            size_t n = count < need ? count : need;
            memcpy(fifo_ + buffered_, samples, n * sizeof(int16_t)); buffered_ += n; samples += n; count -= n;
            if (buffered_ == static_cast<size_t>(decoder_.inputSamplesRequired())) {
                FreeDv2400bDecodeResult r;
                decoder_.decode(fifo_, buffered_, payload_, r); buffered_ = 0; sync_ = r.synchronized;
                if (r.framePresent && callback_) callback_(payload_, r.frameType == FreeDv2400bFrameType::VOICE ? PAYLOAD_BYTES : 0, r);
            }
        }
    }
    void processSample(int16_t sample) { processSamples(&sample, 1); }
    void reset() { decoder_.reset(); buffered_ = 0; sync_ = false; memset(payload_, 0, sizeof(payload_)); }
    bool synchronizedNow() const { return sync_; }
    size_t bufferedSamples() const { return buffered_; }
private:
    FreeDv2400bDecoder decoder_; FreeDv2400bFrameCallback callback_;
    int16_t fifo_[MAX_RX_INPUT]; uint8_t payload_[PAYLOAD_BYTES]; size_t buffered_; bool sync_;
};
}

using freedv2400b::FreeDv2400bDecodeResult;
using freedv2400b::FreeDv2400bDecoder;
using freedv2400b::FreeDv2400bDemodulator;
using freedv2400b::FreeDv2400bFrameCallback;
using freedv2400b::FreeDv2400bFrameType;
