// SPDX-License-Identifier: GPL-3.0-only
// One-shot KV4P discriminator capture for host-side modem comparison.

#include <Arduino.h>
#include <AudioTools.h>
#include <DRA818.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <math.h>

using namespace audio_tools;

static constexpr int SAMPLE_RATE_HZ = 48000;
static constexpr int PIN_AUDIO_IN = 34;
static constexpr int PIN_ADC_BIAS = 26;
static constexpr int PIN_RADIO_RX = 16;
static constexpr int PIN_RADIO_TX = 17;
static constexpr int PIN_RADIO_PTT = 18;
static constexpr int PIN_RADIO_PD = 19;
static constexpr float ADC_BIAS_VOLTS = 1.75f;
static constexpr float RADIO_FREQUENCY_MHZ = 446.0f;
static constexpr int ADC_REQUEST_SAMPLE_RATE = 48188;
static constexpr float INPUT_GAIN = 16.0f;
static constexpr float DC_DECAY_SECONDS = 0.25f;
static constexpr size_t CAPTURE_SAMPLES = 10 * SAMPLE_RATE_HZ;
static constexpr size_t BLOCK_SAMPLES = 256;

#ifndef INJECT_1200HZ_TONE
#define INJECT_1200HZ_TONE 0
#endif

#ifndef ADC_USE_APLL
#define ADC_USE_APLL 1
#endif

static AnalogAudioStream adcStream;
static DRA818 radio(&Serial2, SA818_UHF);
static AudioInfo audioInfo(SAMPLE_RATE_HZ, 1, 16);
static int16_t block[BLOCK_SAMPLES];
static int8_t pcm8[BLOCK_SAMPLES];
static size_t captured = 0;
static float dcEstimate = 0.0f;
static float dcAlpha = 0.0f;
static float testTonePhase = 0.0f;

static void configureRadio() {
  pinMode(PIN_RADIO_PD, OUTPUT);
  digitalWrite(PIN_RADIO_PD, HIGH);
  pinMode(PIN_RADIO_PTT, OUTPUT);
  digitalWrite(PIN_RADIO_PTT, HIGH);
  Serial2.begin(9600, SERIAL_8N1, PIN_RADIO_RX, PIN_RADIO_TX);
  Serial2.setTimeout(10);
  delay(8000);
  radio.handshake();
  radio.group(DRA818_25K, RADIO_FREQUENCY_MHZ, RADIO_FREQUENCY_MHZ, 0, 0, 0);
  radio.volume(8);
  radio.filters(false, false, false);
}

static void injectAdcBias() {
  dac_output_enable(DAC_CHANNEL_2);
  const float code = 255.0f * ADC_BIAS_VOLTS / 3.3f;
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(code + 0.5f));
}

void setup() {
  Serial.setTxBufferSize(8192);
  Serial.begin(921600);
  configureRadio();
  injectAdcBias();
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);
  dcAlpha = 1.0f - expf(-1.0f /
      (SAMPLE_RATE_HZ * (DC_DECAY_SECONDS / logf(2.0f))));

  auto config = adcStream.defaultConfig(RX_MODE);
  config.copyFrom(audioInfo);
  config.is_auto_center_read = false;
  config.use_apll = ADC_USE_APLL;
  config.auto_clear = false;
  config.adc_pin = PIN_AUDIO_IN;
  // ESP32 legacy I2S ADC runs about 0.83% slow at a requested 48 kHz.
  config.sample_rate = ADC_REQUEST_SAMPLE_RATE;
  config.buffer_size = 512;
  config.buffer_count = 20;
  adcStream.begin(config);

  // Discard bytes left by the uploader before arming the binary capture.
  delay(2000);
  while (Serial.available()) Serial.read();
  static const char trigger[] = "GO!\n";
  size_t matched = 0;
  while (matched < sizeof(trigger) - 1) {
    if (!Serial.available()) { delay(1); continue; }
    const char c = static_cast<char>(Serial.read());
    matched = c == trigger[matched] ? matched + 1 : (c == trigger[0] ? 1 : 0);
  }
  Serial.write(reinterpret_cast<const uint8_t *>("ADC8"), 4);
  const uint32_t count = CAPTURE_SAMPLES;
  Serial.write(reinterpret_cast<const uint8_t *>(&count), sizeof(count));
}

void loop() {
  if (captured < CAPTURE_SAMPLES) {
    const size_t bytes = adcStream.readBytes(
        reinterpret_cast<uint8_t *>(block), sizeof(block));
    size_t samples = bytes / sizeof(block[0]);
    if (samples > CAPTURE_SAMPLES - captured) samples = CAPTURE_SAMPLES - captured;
    for (size_t i = 0; i < samples; ++i) {
#if INJECT_1200HZ_TONE
      pcm8[i] = static_cast<int8_t>(80.0f * sinf(testTonePhase));
      testTonePhase += 2.0f * static_cast<float>(M_PI) * 1200.0f /
                       static_cast<float>(SAMPLE_RATE_HZ);
      if (testTonePhase >= 2.0f * static_cast<float>(M_PI))
        testTonePhase -= 2.0f * static_cast<float>(M_PI);
#else
      dcEstimate = dcAlpha * block[i] + (1.0f - dcAlpha) * dcEstimate;
      const int16_t processed = static_cast<int16_t>(
          static_cast<int32_t>((block[i] - dcEstimate) * INPUT_GAIN));
      pcm8[i] = static_cast<int8_t>(processed >> 8);
#endif
    }
    Serial.write(reinterpret_cast<const uint8_t *>(pcm8), samples);
    captured += samples;
    return;
  }

  adcStream.end();
  Serial.flush();
  while (true) delay(1000);
}
