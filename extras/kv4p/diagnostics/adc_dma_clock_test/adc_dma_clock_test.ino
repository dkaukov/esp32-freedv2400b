// SPDX-License-Identifier: LGPL-2.1-only
// Measure the ESP32 legacy I2S ADC clock using completed DMA descriptors.

#include <Arduino.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <driver/i2s.h>

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int PIN_AUDIO_IN = 34;
static constexpr int PIN_ADC_BIAS = 26;
static constexpr float ADC_BIAS_VOLTS = 1.75f;
static constexpr size_t DMA_FRAMES = 256;
static constexpr int DMA_BUFFER_COUNT = 8;
static constexpr uint64_t REPORT_US = 2000000;

#ifndef ADC_REQUEST_SAMPLE_RATE
#define ADC_REQUEST_SAMPLE_RATE 48000
#endif

static QueueHandle_t i2sEvents;
static uint16_t rxBlock[DMA_FRAMES];

static void injectAdcBias() {
  dac_output_enable(DAC_CHANNEL_2);
  const float code = 255.0f * ADC_BIAS_VOLTS / 3.3f;
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(code + 0.5f));
}

static bool beginAdc() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);

  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(
      I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  config.sample_rate = ADC_REQUEST_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = DMA_BUFFER_COUNT;
  config.dma_buf_len = DMA_FRAMES;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;
  config.mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT;
  config.bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT;

  if (i2s_driver_install(I2S_PORT, &config, 32, &i2sEvents) != ESP_OK)
    return false;
  if (i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_6) != ESP_OK) return false;
  return i2s_adc_enable(I2S_PORT) == ESP_OK;
}

static void consumerTask(void *) {
  while (true) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, rxBlock, sizeof(rxBlock), &bytesRead, portMAX_DELAY);
  }
}

void setup() {
  Serial.begin(115200);
  injectAdcBias();
  if (!beginAdc()) {
    Serial.println("ADC I2S setup failed");
    while (true) delay(1000);
  }
  xTaskCreatePinnedToCore(consumerTask, "adc-consumer", 2048, nullptr, 2,
                          nullptr, 0);
  Serial.printf("ADC_DMA_CLOCK request=%d frames_per_eof=%u adc_pin=%d\n",
                ADC_REQUEST_SAMPLE_RATE,
                static_cast<unsigned>(DMA_FRAMES), PIN_AUDIO_IN);
}

void loop() {
  static uint64_t firstEofUs = 0;
  static uint64_t eofCount = 0;
  static uint64_t overflowCount = 0;
  static uint64_t dmaErrorCount = 0;
  static uint64_t nextReportUs = REPORT_US;

  i2s_event_t event;
  if (xQueueReceive(i2sEvents, &event, portMAX_DELAY) != pdTRUE) return;
  if (event.type == I2S_EVENT_RX_Q_OVF) {
    ++overflowCount;
    return;
  }
  if (event.type == I2S_EVENT_DMA_ERROR) {
    ++dmaErrorCount;
    return;
  }
  if (event.type != I2S_EVENT_RX_DONE) return;

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
  const double requestedErrorPpm =
      (rate / ADC_REQUEST_SAMPLE_RATE - 1.0) * 1.0e6;
  const double audioErrorPpm = (rate / 48000.0 - 1.0) * 1.0e6;
  Serial.printf(
      "ADC_DMA_CLOCK request=%d elapsed=%.6f eofs=%llu frames=%llu "
      "rate=%.3f sps request_error=%+.1f ppm audio_error=%+.1f ppm "
      "overflow=%llu dma_error=%llu\n",
      ADC_REQUEST_SAMPLE_RATE, elapsedUs / 1000000.0,
      static_cast<unsigned long long>(eofCount),
      static_cast<unsigned long long>(completedFrames), rate,
      requestedErrorPpm, audioErrorPpm,
      static_cast<unsigned long long>(overflowCount),
      static_cast<unsigned long long>(dmaErrorCount));
  nextReportUs += REPORT_US;
}
