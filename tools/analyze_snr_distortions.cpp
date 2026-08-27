// SPDX-License-Identifier: LGPL-2.1-only
// Reproduce FreeDV 2400B discriminator-SNR distortion experiments.

#include <FreeDv2400b.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace freedv2400b;

static int voiceFrames;

static void onFrame(const uint8_t *, size_t,
                    const FreeDv2400bDecodeResult &result) {
  if (result.frameType == FreeDv2400bFrameType::VOICE) ++voiceFrames;
}

static void measure(const char *name, const std::vector<int16_t> &samples,
                    float injectedSnrDb = NAN) {
  voiceFrames = 0;
  FreeDv2400bDemodulator demodulator(onFrame);
  demodulator.processSamples(samples.data(), samples.size());
  if (std::isnan(injectedSnrDb)) {
    std::printf("%-24s quality=%7.2f dB sync=%d voice=%d\n", name,
                demodulator.discriminatorSnrDbNow(),
                demodulator.synchronizedNow(), voiceFrames);
  } else {
    std::printf("%-24s injected=%6.1f dB quality=%7.2f dB sync=%d voice=%d\n",
                name, injectedSnrDb, demodulator.discriminatorSnrDbNow(),
                demodulator.synchronizedNow(), voiceFrames);
  }
}

static std::vector<int16_t> lowPass(const std::vector<int16_t> &input,
                                    float cutoffHz) {
  std::vector<int16_t> output(input.size());
  const float alpha = 1.0f - std::exp(-6.283185307f * cutoffHz / SAMPLE_RATE);
  float state = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    state += alpha * (input[i] - state);
    output[i] = static_cast<int16_t>(state);
  }
  return output;
}

static std::vector<int16_t> highPass(const std::vector<int16_t> &input,
                                     float cutoffHz) {
  std::vector<int16_t> output(input.size());
  const float alpha = std::exp(-6.283185307f * cutoffHz / SAMPLE_RATE);
  float state = 0, previous = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    state = alpha * (state + input[i] - previous);
    previous = input[i];
    output[i] = static_cast<int16_t>(state);
  }
  return output;
}

static std::vector<int16_t> addNoise(const std::vector<int16_t> &input,
                                     float fraction, float &actualSnrDb) {
  std::vector<int16_t> output(input.size());
  uint32_t state = 0x13579bdf;
  double signalPower = 0, noisePower = 0;
  for (size_t i = 0; i < input.size(); ++i) {
    state = state * 1664525u + 1013904223u;
    const float u1 = ((state >> 8) + 1.0f) / 16777217.0f;
    state = state * 1664525u + 1013904223u;
    const float u2 = ((state >> 8) + 1.0f) / 16777217.0f;
    const float gaussian = std::sqrt(-2.0f * std::log(u1)) *
                           std::cos(6.283185307f * u2);
    int sample = static_cast<int>(std::lround(
        input[i] + gaussian * 16383.0f * fraction));
    sample = std::max(-32768, std::min(32767, sample));
    output[i] = static_cast<int16_t>(sample);
    signalPower += static_cast<double>(input[i]) * input[i];
    const double error = output[i] - input[i];
    noisePower += error * error;
  }
  actualSnrDb = static_cast<float>(10.0 * std::log10(signalPower / noisePower));
  return output;
}

int main() {
  FILE *input = std::fopen("test/fixtures/modem_2400b_short.wav", "rb");
  if (!input || std::fseek(input, 44, SEEK_SET)) {
    std::fprintf(stderr, "Run from the repository root; fixture not found\n");
    return 1;
  }
  std::vector<int16_t> ideal(48000);
  if (std::fread(ideal.data(), sizeof(int16_t), ideal.size(), input) !=
      ideal.size()) {
    std::fprintf(stderr, "Golden fixture is truncated\n");
    std::fclose(input);
    return 1;
  }
  std::fclose(input);

  measure("ideal", ideal);
  measure("low-pass 5400 Hz", lowPass(ideal, 5400));
  measure("low-pass 4000 Hz", lowPass(ideal, 4000));
  measure("low-pass 3000 Hz", lowPass(ideal, 3000));
  measure("band-pass 500-4000", lowPass(highPass(ideal, 500), 4000));
  const std::vector<int16_t> bandPass = lowPass(highPass(ideal, 200), 4000);
  measure("band-pass 200-4000", bandPass);

  static const float noiseFractions[] = {
      .01f, .02f, .05f, .10f, .20f, .30f, .40f, .50f, .75f, 1.00f};
  for (size_t i = 0; i < sizeof(noiseFractions) / sizeof(noiseFractions[0]); ++i) {
    float actualSnrDb;
    const std::vector<int16_t> noisy =
        addNoise(bandPass, noiseFractions[i], actualSnrDb);
    char name[32];
    std::snprintf(name, sizeof(name), "BPF + %.0f%% AWGN",
                  noiseFractions[i] * 100.0f);
    measure(name, noisy, actualSnrDb);
  }
  return 0;
}
