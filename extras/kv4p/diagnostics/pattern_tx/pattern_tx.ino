// SPDX-License-Identifier: LGPL-2.1-only
// KV4P-HT-compatible FreeDV 2400B fixed-pattern transmitter.

#include <Arduino.h>
#include <AudioTools.h>
#include <DRA818.h>
#include <FreeDv2400b.h>

using namespace freedv2400b;
using namespace audio_tools;

#ifndef ENABLE_RF_TX
#define ENABLE_RF_TX 0
#endif

static constexpr int PIN_AUDIO_OUT = 25;  // ESP32 DAC channel 1 to SA818 mic.
static constexpr int PIN_RADIO_RX = 16;
static constexpr int PIN_RADIO_TX = 17;
static constexpr int PIN_RADIO_PTT = 18;
static constexpr int PIN_RADIO_PD = 19;
static constexpr float RADIO_FREQUENCY_MHZ = 446.0f;
static constexpr uint16_t TX_MAGNITUDE = 16383;  // Codec2 reference level.
static constexpr size_t AUDIO_BLOCK_SAMPLES = 256;
static constexpr unsigned FRAMES_PER_BURST = 125;
static constexpr uint32_t RX_INTERVAL_MS = 0;
static constexpr uint32_t DAC_DRAIN_MS = 100;

static const uint8_t PAYLOAD[PAYLOAD_BYTES] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x70
};

static AnalogAudioStream audioOut;
static DRA818 radio(&Serial2, SA818_UHF);
static AudioInfo audioInfo(SAMPLE_RATE, 1, 16);
static int16_t audioBlock[AUDIO_BLOCK_SAMPLES];
static uint32_t txBytesWritten;
static uint32_t txShortWrites;
static uint32_t txZeroWrites;

static void onTxSamples(const int16_t *samples, size_t count) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(samples);
  size_t remaining = count * sizeof(samples[0]);
  while (remaining) {
    const size_t written = audioOut.write(data, remaining);
    if (!written) {
      ++txZeroWrites;
      delay(1);
      continue;
    }
    if (written < remaining) ++txShortWrites;
    txBytesWritten += written;
    data += written;
    remaining -= written;
  }
}

static FreeDv2400bModulator modulator(onTxSamples);

static bool configureRadio() {
  pinMode(PIN_RADIO_PD, OUTPUT);
  digitalWrite(PIN_RADIO_PD, HIGH);
  pinMode(PIN_RADIO_PTT, OUTPUT);
  digitalWrite(PIN_RADIO_PTT, HIGH);
  Serial2.begin(9600, SERIAL_8N1, PIN_RADIO_RX, PIN_RADIO_TX);
  Serial2.setTimeout(10);
  delay(2000);
  if (!radio.handshake()) return false;
  if (!radio.group(DRA818_25K, RADIO_FREQUENCY_MHZ, RADIO_FREQUENCY_MHZ,
                   0, 0, 0)) return false;
  if (!radio.volume(8)) return false;
  return radio.filters(false, false, false);
}

static bool beginAudioOutput() {
  auto config = audioOut.defaultConfig(TX_MODE);
  config.copyFrom(audioInfo);
  config.use_apll = true;
  config.buffer_size = 512;
  config.buffer_count = 8;
  return audioOut.begin(config);
}

static void transmitBurst() {
  txBytesWritten = txShortWrites = txZeroWrites = 0;
  const uint32_t started = millis();
  digitalWrite(PIN_RADIO_PTT, LOW);
  for (unsigned frame = 0; frame < FRAMES_PER_BURST; ++frame) {
    modulator.modulate(PAYLOAD, sizeof(PAYLOAD), audioBlock,
                       AUDIO_BLOCK_SAMPLES);
  }
  // write() returns while about one DMA queue remains; do not truncate it.
  delay(DAC_DRAIN_MS);
  digitalWrite(PIN_RADIO_PTT, HIGH);
  Serial.printf("TX_STATS frames=%u samples=%u bytes=%lu elapsed_ms=%lu "
                "magnitude=%u short_writes=%lu zero_writes=%lu\n",
                FRAMES_PER_BURST, FRAMES_PER_BURST * TX_SAMPLES,
                static_cast<unsigned long>(txBytesWritten),
                static_cast<unsigned long>(millis() - started), TX_MAGNITUDE,
                static_cast<unsigned long>(txShortWrites),
                static_cast<unsigned long>(txZeroWrites));
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("FreeDV 2400B KV4P fixed-pattern transmitter");
  pinMode(PIN_RADIO_PTT, OUTPUT);
  digitalWrite(PIN_RADIO_PTT, HIGH);
#if !ENABLE_RF_TX
  Serial.println("RF transmission disabled; rebuild with ENABLE_RF_TX=1 to opt in");
  return;
#else
  if (!configureRadio() || !beginAudioOutput()) {
    Serial.println("SA818 or DAC initialization failed");
    while (true) delay(1000);
  }
  modulator.setMagnitude(TX_MAGNITUDE);
  Serial.printf("SA818 %.4f MHz, 25 kHz wide, filters off, TX magnitude %u\n",
                RADIO_FREQUENCY_MHZ, TX_MAGNITUDE);
#endif
}

void loop() {
#if ENABLE_RF_TX
  transmitBurst();
  delay(RX_INTERVAL_MS);
#else
  delay(1000);
#endif
}
