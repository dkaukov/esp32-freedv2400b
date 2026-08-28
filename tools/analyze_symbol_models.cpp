// SPDX-License-Identifier: LGPL-2.1-only
// Compare decision-directed symbol/channel quality models offline.

#include <FreeDv2400b.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace freedv2400b;

struct Chip {
  float samples[10];
  int sign;
  int previousSign;
};

static std::vector<Chip> extractChips(const std::vector<int16_t> &input) {
  std::vector<Chip> chips;
  detail::MovingSum boxcar;
  int16_t tail[45] = {};
  int32_t filtered[1930];
  int16_t extended[1939];
  int required = TX_SAMPLES;
  size_t position = 0;
  int previousSign = 0;

  while (position + required <= input.size()) {
    const int retained = 1960 - required;
    const int tailOffset = 45 - retained;
    for (int i = 0; i < 1939; ++i) {
      extended[i] = i < retained ? tail[tailOffset + i]
                                 : input[position + i - retained];
      const int32_t sum = boxcar.push(extended[i]);
      if (i >= 9) filtered[i - 9] = sum;
    }
    std::memcpy(tail, input.data() + position + required - 45, sizeof(tail));
    position += required;

    const float timing = detail::ManchesterTimingEstimator::estimate(filtered, 1930);
    required = timing > -.2f ? 1925 : (timing < -.65f ? 1915 : 1920);
    const int offset = 14 + static_cast<int>(std::round(timing * 10));
    float threshold = 0;
    for (int i = 0; i < 192; ++i) threshold += filtered[offset + 10 * i];
    threshold /= 192.0f;

    for (int i = 0; i < 192; ++i) {
      Chip chip;
      chip.sign = filtered[offset + 10 * i] >= threshold ? 1 : -1;
      chip.previousSign = previousSign ? previousSign : -chip.sign;
      previousSign = chip.sign;
      for (int j = 0; j < 10; ++j) chip.samples[j] = extended[offset + 10 * i + j];
      chips.push_back(chip);
    }
  }
  return chips;
}

static float regressionSnr(const std::vector<float> &x,
                           const std::vector<float> &y) {
  double meanX = 0, meanY = 0;
  for (size_t i = 0; i < x.size(); ++i) { meanX += x[i]; meanY += y[i]; }
  meanX /= x.size(); meanY /= y.size();
  double varianceX = 0, covariance = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double dx = x[i] - meanX;
    varianceX += dx * dx;
    covariance += dx * (y[i] - meanY);
  }
  const double gain = covariance / varianceX;
  const double offset = meanY - gain * meanX;
  double signal = 0, error = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    const double predicted = gain * x[i] + offset;
    const double centered = predicted - meanY;
    const double residual = y[i] - predicted;
    signal += centered * centered;
    error += residual * residual;
  }
  return static_cast<float>(10.0 * std::log10(signal / error));
}

static float polaritySnr(const std::vector<Chip> &chips, size_t begin) {
  std::vector<float> expected, received;
  expected.reserve((chips.size() - begin) * 10);
  received.reserve(expected.capacity());
  for (size_t i = begin; i < chips.size(); ++i) {
    for (int j = 0; j < 10; ++j) {
      expected.push_back(static_cast<float>(chips[i].sign));
      received.push_back(chips[i].samples[j]);
    }
  }
  return regressionSnr(expected, received);
}

static float rampSnr(const std::vector<Chip> &chips, size_t begin) {
  double globalMean = 0;
  size_t count = 0;
  for (size_t i = begin; i < chips.size(); ++i)
    for (int j = 0; j < 10; ++j) { globalMean += chips[i].samples[j]; ++count; }
  globalMean /= count;

  double signal = 0, error = 0;
  for (size_t i = begin; i < chips.size(); ++i) {
    double mean = 0, covariance = 0, variance = 0;
    for (int j = 0; j < 10; ++j) mean += chips[i].samples[j];
    mean /= 10.0;
    for (int j = 0; j < 10; ++j) {
      const double time = j - 4.5;
      covariance += time * (chips[i].samples[j] - mean);
      variance += time * time;
    }
    const double slope = covariance / variance;
    for (int j = 0; j < 10; ++j) {
      const double predicted = mean + slope * (j - 4.5);
      const double residual = chips[i].samples[j] - predicted;
      signal += (predicted - globalMean) * (predicted - globalMean);
      error += residual * residual;
    }
  }
  return static_cast<float>(10.0 * std::log10(signal / error));
}

static float templateSnr(const std::vector<Chip> &chips, size_t split) {
  double sums[4][10] = {};
  int counts[4] = {};
  for (size_t i = 1; i < split; ++i) {
    const int category = (chips[i].previousSign > 0 ? 2 : 0) +
                         (chips[i].sign > 0 ? 1 : 0);
    ++counts[category];
    for (int j = 0; j < 10; ++j) sums[category][j] += chips[i].samples[j];
  }
  for (int category = 0; category < 4; ++category)
    for (int j = 0; j < 10; ++j) sums[category][j] /= counts[category];

  std::vector<float> expected, received;
  expected.reserve((chips.size() - split) * 10);
  received.reserve(expected.capacity());
  for (size_t i = split; i < chips.size(); ++i) {
    const int category = (chips[i].previousSign > 0 ? 2 : 0) +
                         (chips[i].sign > 0 ? 1 : 0);
    for (int j = 0; j < 10; ++j) {
      expected.push_back(static_cast<float>(sums[category][j]));
      received.push_back(chips[i].samples[j]);
    }
  }
  return regressionSnr(expected, received);
}

static void analyze(const char *name, const std::vector<int16_t> &samples) {
  const std::vector<Chip> chips = extractChips(samples);
  const size_t split = chips.size() / 2;
  std::printf("%-20s chips=%6zu polarity=%7.2f dB ramp=%7.2f dB templates=%7.2f dB\n",
              name, chips.size(), polaritySnr(chips, split),
              rampSnr(chips, split), templateSnr(chips, split));
}

static std::vector<int16_t> loadWav(const char *path) {
  FILE *file = std::fopen(path, "rb");
  if (!file || std::fseek(file, 44, SEEK_SET)) return {};
  std::vector<int16_t> samples(48000);
  if (std::fread(samples.data(), sizeof(int16_t), samples.size(), file) !=
      samples.size()) samples.clear();
  std::fclose(file);
  return samples;
}

static std::vector<int16_t> loadPcm8(const char *path) {
  FILE *file = std::fopen(path, "rb");
  if (!file) return {};
  std::vector<int16_t> samples;
  int value;
  while ((value = std::fgetc(file)) != EOF)
    samples.push_back(static_cast<int16_t>(static_cast<int8_t>(value)) * 256);
  std::fclose(file);
  return samples;
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
                                     float fraction) {
  std::vector<int16_t> output(input.size());
  uint32_t state = 0x13579bdf;
  for (size_t i = 0; i < input.size(); ++i) {
    state = state * 1664525u + 1013904223u;
    const float u1 = ((state >> 8) + 1.0f) / 16777217.0f;
    state = state * 1664525u + 1013904223u;
    const float u2 = ((state >> 8) + 1.0f) / 16777217.0f;
    const float gaussian = std::sqrt(-2.0f * std::log(u1)) *
                           std::cos(6.283185307f * u2);
    int sample = static_cast<int>(std::lround(
        input[i] + gaussian * 16383.0f * fraction));
    output[i] = static_cast<int16_t>(
        std::max(-32768, std::min(32767, sample)));
  }
  return output;
}

int main() {
  const std::vector<int16_t> ideal = loadWav("test/fixtures/modem_2400b_short.wav");
  const std::vector<int16_t> rf = loadPcm8(
      "extras/kv4p/diagnostics/adc_capture/captures/"
      "rf_2400b_11223344556670_48400_apll_s8.raw");
  if (ideal.empty() || rf.empty()) {
    std::fprintf(stderr, "Run from repository root; input fixture missing\n");
    return 1;
  }
  const std::vector<int16_t> bandPass = lowPass(highPass(ideal, 200), 4000);
  analyze("ideal", ideal);
  analyze("BPF 200-4000", bandPass);
  analyze("BPF + 20% AWGN", addNoise(bandPass, .20f));
  analyze("BPF + 50% AWGN", addNoise(bandPass, .50f));
  analyze("real RF", rf);
  return 0;
}
