// SPDX-License-Identifier: GPL-3.0-only
// Measure the effective ESP32 legacy-I2S ADC rate and recommend a request rate.

#include <Arduino.h>
#include <AudioTools.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <math.h>

using namespace audio_tools;

static constexpr int TARGET_SAMPLE_RATE = 48000;
static constexpr int INITIAL_REQUEST_RATE = 48000;
static constexpr int PIN_AUDIO_IN = 34;
static constexpr int PIN_ADC_BIAS = 26;
static constexpr float ADC_BIAS_VOLTS = 1.75f;
static constexpr uint32_t WARMUP_MS = 100;
static constexpr uint32_t MEASURE_MS = 1000;
// A small read block limits start/end quantization. At one second, a
// 16-sample boundary is about 333 ppm at 48 kHz.
static constexpr size_t BLOCK_SAMPLES = 16;

static AnalogAudioStream adcStream;
static int16_t samples[BLOCK_SAMPLES];

static void injectAdcBias() {
  dac_output_enable(DAC_CHANNEL_2);
  const float code = 255.0f * ADC_BIAS_VOLTS / 3.3f;
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(code + 0.5f));
}

static bool beginAdc(int requestedRate) {
  AudioInfo info(TARGET_SAMPLE_RATE, 1, 16);
  auto config = adcStream.defaultConfig(RX_MODE);
  config.copyFrom(info);
  config.is_auto_center_read = false;
  config.use_apll = true;
  config.auto_clear = false;
  config.adc_pin = PIN_AUDIO_IN;
  config.sample_rate = requestedRate;
  config.buffer_size = 512;
  // About 43 ms at 48 kHz, so the 100 ms drain clears any startup prefill.
  config.buffer_count = 4;
  return adcStream.begin(config);
}

static float measureRate(uint32_t durationMs) {
  uint64_t count = 0;
  const uint32_t started = micros();
  uint32_t elapsedUs = 0;
  do {
    const size_t bytes = adcStream.readBytes(
        reinterpret_cast<uint8_t *>(samples), sizeof(samples));
    count += bytes / sizeof(samples[0]);
    elapsedUs = micros() - started;
  } while (elapsedUs < durationMs * 1000U);
  return 1000000.0f * static_cast<float>(count) / elapsedUs;
}

static void discardFor(uint32_t durationMs) {
  const uint32_t started = millis();
  while (millis() - started < durationMs) {
    adcStream.readBytes(reinterpret_cast<uint8_t *>(samples), sizeof(samples));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("KV4P ESP32 ADC sample-rate calibration");
  injectAdcBias();
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);

  if (!beginAdc(INITIAL_REQUEST_RATE)) {
    Serial.println("ERROR: ADC start failed");
    return;
  }
  discardFor(WARMUP_MS);
  const float measured = measureRate(MEASURE_MS);
  const float calculated = INITIAL_REQUEST_RATE *
      static_cast<float>(TARGET_SAMPLE_RATE) / measured;
  // Ten-Hz rounding makes repeated runs stable without hiding useful
  // board/driver differences.
  const int recommended = 10 * static_cast<int>(lroundf(calculated / 10.0f));
  Serial.printf("CALIBRATION requested=%d measured=%.2f target=%d recommended=%d\n",
                INITIAL_REQUEST_RATE, measured, TARGET_SAMPLE_RATE, recommended);

  adcStream.end();
  delay(100);
  if (!beginAdc(recommended)) {
    Serial.println("ERROR: calibrated ADC restart failed");
    return;
  }
  discardFor(WARMUP_MS);
  const float verified = measureRate(MEASURE_MS);
  const float errorPpm = 1000000.0f * (verified - TARGET_SAMPLE_RATE) /
                         TARGET_SAMPLE_RATE;
  Serial.printf("VERIFICATION requested=%d measured=%.2f error_ppm=%.1f\n",
                recommended, verified, errorPpm);
  Serial.printf("Set ADC_REQUEST_SAMPLE_RATE = %d in the KV4P RX example.\n",
                recommended);
}

void loop() {
  delay(1000);
}
