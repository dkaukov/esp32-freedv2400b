// SPDX-License-Identifier: GPL-3.0-only
// KV4P-HT-compatible ESP32-WROOM ADC receiver example.

#include <Arduino.h>
#include <AudioTools.h>
#include <DRA818.h>
#include <FreeDv2400b.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <math.h>

using namespace freedv2400b;
using namespace audio_tools;

static constexpr int PIN_AUDIO_IN = 34;   // KV4P-HT discriminator/audio input.
static constexpr int PIN_ADC_BIAS = 26;   // KV4P-HT DAC bias output.
static constexpr int PIN_RADIO_RX = 16;   // ESP32 RX from SA818 TXD.
static constexpr int PIN_RADIO_TX = 17;   // ESP32 TX to SA818 RXD.
static constexpr int PIN_RADIO_PTT = 18;  // Active-low transmit; held high here.
static constexpr int PIN_RADIO_PD = 19;   // Active-high module power enable.
static constexpr float ADC_BIAS_VOLTS = 1.75f;
static constexpr float RADIO_FREQUENCY_MHZ = 446.0f;
// Run extras/kv4p/diagnostics/adc_auto_calibrate for another ADC/I2S setup.
static constexpr int ADC_REQUEST_SAMPLE_RATE = 48188;
static constexpr float INPUT_GAIN = 16.0f;
static constexpr float DC_DECAY_SECONDS = 0.25f;
static constexpr size_t AUDIO_BLOCK_SAMPLES = 256;
static constexpr size_t USB_UART_RX_BUFFER_BYTES = 1024;
static constexpr size_t USB_UART_TX_BUFFER_BYTES = 8192;

#ifndef ADC_USE_APLL
#define ADC_USE_APLL 1
#endif

static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result);
static AnalogAudioStream adcStream;
static DRA818 radio(&Serial2, SA818_UHF);
static AudioInfo audioInfo(SAMPLE_RATE, 1, 16);
static FreeDv2400bDemodulator demodulator(onFrame);
static int16_t audioBlock[AUDIO_BLOCK_SAMPLES];
static float dcEstimate = 0.0f;
static float dcAlpha = 0.0f;
static uint32_t statsStarted = 0;
static uint32_t statsSamples = 0;
static uint32_t statsReads = 0;
static uint32_t statsZeroReads = 0;
static uint32_t statsProcessUs = 0;
static uint32_t statsMaxProcessUs = 0;
static uint32_t statsVoiceFrames = 0;
static uint32_t statsExactFrames = 0;
static uint32_t statsDataFrames = 0;
static uint32_t statsPayloadErrors = 0;

static const uint8_t TEST_PAYLOAD[PAYLOAD_BYTES] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x70
};

static uint8_t payloadBitErrors(const uint8_t *payload) {
  uint8_t errors = 0;
  for (size_t i = 0; i < 6; ++i) {
    errors += __builtin_popcount(static_cast<unsigned>(payload[i] ^ TEST_PAYLOAD[i]));
  }
  errors += __builtin_popcount(
      static_cast<unsigned>((payload[6] ^ TEST_PAYLOAD[6]) & 0xf0));
  return errors;
}

static void onFrame(const uint8_t *payload, size_t length,
                    const FreeDv2400bDecodeResult &result) {
  Serial.printf("%s sync=%d uw_errors=%u uw_ber_ema=%.5f snr=%.1f dB clock=%.1f ppm",
                result.frameType == FreeDv2400bFrameType::VOICE ? "VOICE" : "DATA",
                result.synchronized, result.uniqueWordErrors,
                result.uniqueWordBerEma, result.discriminatorSnrDb, result.clockOffsetPpm);
  if (result.frameType == FreeDv2400bFrameType::VOICE && length == PAYLOAD_BYTES) {
    ++statsVoiceFrames;
    const uint8_t errors = payloadBitErrors(payload);
    statsPayloadErrors += errors;
    if (!errors) ++statsExactFrames;
    Serial.print(" payload=");
    for (size_t i = 0; i < length; ++i) Serial.printf("%02x", payload[i]);
    Serial.printf(" payload_errors=%u/52", errors);
  } else if (result.frameType == FreeDv2400bFrameType::DATA) {
    ++statsDataFrames;
  }
  Serial.println();
}

static void injectAdcBias() {
  dac_output_enable(DAC_CHANNEL_2);  // DAC channel 2 is GPIO26 on ESP32-WROOM.
  const float dacCode = 255.0f * ADC_BIAS_VOLTS / 3.3f;
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(dacCode + 0.5f));
}

static bool configureRadio() {
  pinMode(PIN_RADIO_PD, OUTPUT);
  digitalWrite(PIN_RADIO_PD, HIGH);
  pinMode(PIN_RADIO_PTT, OUTPUT);
  digitalWrite(PIN_RADIO_PTT, HIGH);  // Receive only; this example never transmits.
  Serial2.begin(9600, SERIAL_8N1, PIN_RADIO_RX, PIN_RADIO_TX);
  Serial2.setTimeout(10);
  delay(2000);  // Allow the module to finish powering up.

  if (!radio.handshake()) {
    Serial.println("SA818 handshake failed");
    return false;
  }
  if (!radio.group(DRA818_25K, RADIO_FREQUENCY_MHZ, RADIO_FREQUENCY_MHZ,
                   0, 0, 0)) {
    Serial.println("SA818 frequency/group configuration failed");
    return false;
  }
  if (!radio.volume(8)) {
    Serial.println("SA818 volume configuration failed");
    return false;
  }
  if (!radio.filters(false, false, false)) {
    Serial.println("SA818 filter configuration failed");
    return false;
  }
  Serial.printf("SA818 configured: %.4f MHz, 25 kHz wide, filters disabled\n",
                RADIO_FREQUENCY_MHZ);
  return true;
}

static int16_t removeBiasAndApplyGain(int16_t input) {
  dcEstimate = dcAlpha * input + (1.0f - dcAlpha) * dcEstimate;
  return static_cast<int16_t>(
      static_cast<int32_t>((input - dcEstimate) * INPUT_GAIN));
}

void setup() {
  Serial.setRxBufferSize(USB_UART_RX_BUFFER_BYTES);
  Serial.setTxBufferSize(USB_UART_TX_BUFFER_BYTES);
  Serial.begin(115200);
  delay(250);
  Serial.println("FreeDV 2400B KV4P ADC receiver");

  configureRadio();
  injectAdcBias();
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);  // GPIO34.

  dcAlpha = 1.0f - expf(-1.0f /
      (SAMPLE_RATE * (DC_DECAY_SECONDS / logf(2.0f))));

  auto config = adcStream.defaultConfig(RX_MODE);
  config.copyFrom(audioInfo);
  config.is_auto_center_read = false;  // Bias is removed below, as in KV4P-HT.
  config.use_apll = ADC_USE_APLL;
  config.auto_clear = false;
  config.adc_pin = PIN_AUDIO_IN;
  // Calibrated request for approximately 48.0 ksample/s on legacy ESP32 ADC.
  config.sample_rate = ADC_REQUEST_SAMPLE_RATE;
  config.buffer_size = 512;
  config.buffer_count = 20;  // About 213 ms of one-channel ADC buffering.
  if (!adcStream.begin(config)) {
    Serial.println("ADC stream initialization failed");
    while (true) delay(1000);
  }

  Serial.printf("ADC GPIO%d, bias GPIO%d=%.2f V request=%d use_apll=%d\n",
                PIN_AUDIO_IN, PIN_ADC_BIAS, ADC_BIAS_VOLTS,
                ADC_REQUEST_SAMPLE_RATE, ADC_USE_APLL);
  statsStarted = millis();
}

void loop() {
  size_t bytes = adcStream.readBytes(reinterpret_cast<uint8_t *>(audioBlock),
                                     sizeof(audioBlock));
  size_t samples = bytes / sizeof(audioBlock[0]);
  ++statsReads;
  if (!samples) ++statsZeroReads;
  statsSamples += samples;
  for (size_t i = 0; i < samples; ++i) {
    audioBlock[i] = removeBiasAndApplyGain(audioBlock[i]);
  }
  const uint32_t processStarted = micros();
  demodulator.processSamples(audioBlock, samples);
  const uint32_t processUs = micros() - processStarted;
  statsProcessUs += processUs;
  if (processUs > statsMaxProcessUs) statsMaxProcessUs = processUs;

  const uint32_t elapsed = millis() - statsStarted;
  if (elapsed >= 5000) {
    const uint32_t payloadBits = statsVoiceFrames * PAYLOAD_BITS;
    Serial.printf("RX_STATS samples=%lu elapsed_ms=%lu rate=%.1f reads=%lu "
                  "zero_reads=%lu decode_cpu=%.1f%% max_process_us=%lu "
                  "buffered=%u voice=%lu exact=%lu data=%lu "
                  "payload_errors=%lu/%lu payload_ber=%.6f\n",
                  static_cast<unsigned long>(statsSamples),
                  static_cast<unsigned long>(elapsed),
                  1000.0f * statsSamples / elapsed,
                  static_cast<unsigned long>(statsReads),
                  static_cast<unsigned long>(statsZeroReads),
                  100.0f * statsProcessUs / (elapsed * 1000.0f),
                  static_cast<unsigned long>(statsMaxProcessUs),
                  static_cast<unsigned>(demodulator.bufferedSamples()),
                  static_cast<unsigned long>(statsVoiceFrames),
                  static_cast<unsigned long>(statsExactFrames),
                  static_cast<unsigned long>(statsDataFrames),
                  static_cast<unsigned long>(statsPayloadErrors),
                  static_cast<unsigned long>(payloadBits),
                  payloadBits ? static_cast<float>(statsPayloadErrors) / payloadBits : 0.0f);
    statsStarted = millis();
    statsSamples = statsReads = statsZeroReads = 0;
    statsProcessUs = statsMaxProcessUs = 0;
    statsVoiceFrames = statsExactFrames = statsDataFrames = statsPayloadErrors = 0;
  }
}
