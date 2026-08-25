// SPDX-License-Identifier: GPL-3.0-only
// Measure the ESP32 DAC/PDM clock by counting completed I2S DMA descriptors.

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int SAMPLE_RATE_HZ = 48000;
static constexpr int PIN_AUDIO_OUT = 25;
static constexpr int PIN_PDM_WS = 27;
static constexpr size_t DMA_FRAMES = 256;
static constexpr int DMA_BUFFER_COUNT = 8;
static constexpr uint64_t REPORT_US = 2000000;

static QueueHandle_t i2sEvents;

#if OUTPUT_PDM
static int16_t txBlock[DMA_FRAMES];
#else
// Built-in DAC I2S uses stereo DMA frames. DAC1/GPIO25 is the right channel.
static int16_t txBlock[DMA_FRAMES * 2];
#endif

static bool beginI2s() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(
      I2S_MODE_MASTER | I2S_MODE_TX |
#if OUTPUT_PDM
      I2S_MODE_PDM
#else
      I2S_MODE_DAC_BUILT_IN
#endif
  );
  config.sample_rate = SAMPLE_RATE_HZ;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
#if OUTPUT_PDM
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
#else
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#endif
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = DMA_BUFFER_COUNT;
  config.dma_buf_len = DMA_FRAMES;
  config.use_apll = true;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT;
  config.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;

  if (i2s_driver_install(I2S_PORT, &config, 32, &i2sEvents) != ESP_OK)
    return false;

#if OUTPUT_PDM
  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_PIN_NO_CHANGE;
  pins.ws_io_num = PIN_PDM_WS;
  pins.data_out_num = PIN_AUDIO_OUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) return false;
#else
  if (i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN) != ESP_OK) return false;
#endif
  return i2s_zero_dma_buffer(I2S_PORT) == ESP_OK;
}

static void fillTone() {
  constexpr float TONE_HZ = 1200.0f;
  for (size_t frame = 0; frame < DMA_FRAMES; ++frame) {
    const float phase = 2.0f * static_cast<float>(M_PI) * TONE_HZ *
                        static_cast<float>(frame) / SAMPLE_RATE_HZ;
#if OUTPUT_PDM
    txBlock[frame] = static_cast<int16_t>(16383.0f * sinf(phase));
#else
    // Internal DAC consumes the high byte as unsigned PCM.
    const uint16_t dac = static_cast<uint16_t>(
        (128.0f + 63.0f * sinf(phase))) << 8;
    txBlock[frame * 2] = static_cast<int16_t>(dac);     // right/DAC1
    txBlock[frame * 2 + 1] = static_cast<int16_t>(dac);
#endif
  }
}

static void producerTask(void *) {
  while (true) {
    size_t written = 0;
    i2s_write(I2S_PORT, txBlock, sizeof(txBlock), &written, portMAX_DELAY);
  }
}

void setup() {
  Serial.begin(115200);
  fillTone();
  if (!beginI2s()) {
    Serial.println("I2S setup failed");
    while (true) delay(1000);
  }
  xTaskCreatePinnedToCore(producerTask, "i2s-producer", 2048, nullptr, 2,
                          nullptr, 0);
  Serial.printf("DMA_CLOCK mode=%s request=%d frames_per_eof=%u\n",
                OUTPUT_PDM ? "PDM" : "DAC", SAMPLE_RATE_HZ,
                static_cast<unsigned>(DMA_FRAMES));
}

void loop() {
  static uint64_t firstEofUs = 0;
  static uint64_t eofCount = 0;
  static uint64_t nextReportUs = REPORT_US;
  i2s_event_t event;
  if (xQueueReceive(i2sEvents, &event, portMAX_DELAY) != pdTRUE) return;
  if (event.type != I2S_EVENT_TX_DONE) return;

  const uint64_t nowUs = esp_timer_get_time();
  if (firstEofUs == 0) {
    firstEofUs = nowUs;
    eofCount = 1;
    return;
  }
  ++eofCount;
  const uint64_t elapsedUs = nowUs - firstEofUs;
  if (elapsedUs < nextReportUs) return;

  const uint64_t completedFrames = (eofCount - 1) * DMA_FRAMES;
  const double rate = static_cast<double>(completedFrames) * 1000000.0 /
                      static_cast<double>(elapsedUs);
  const double errorPpm = (rate / SAMPLE_RATE_HZ - 1.0) * 1.0e6;
  Serial.printf(
      "DMA_CLOCK mode=%s elapsed=%.6f eofs=%llu frames=%llu "
      "rate=%.3f sps error=%+.1f ppm\n",
      OUTPUT_PDM ? "PDM" : "DAC", elapsedUs / 1000000.0,
      static_cast<unsigned long long>(eofCount),
      static_cast<unsigned long long>(completedFrames), rate, errorPpm);
  nextReportUs += REPORT_US;
}
