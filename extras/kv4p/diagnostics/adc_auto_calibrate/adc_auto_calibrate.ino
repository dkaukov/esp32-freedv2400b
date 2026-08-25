// SPDX-License-Identifier: GPL-3.0-only
// Automatically find the legacy ESP32 I2S ADC request closest to 48 kHz.

#include <Arduino.h>
#include <driver/adc.h>
#include <driver/dac.h>
#include <driver/i2s.h>
#include <math.h>

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int TARGET_RATE = 48000;
static constexpr int PIN_AUDIO_IN = 34;
static constexpr float ADC_BIAS_VOLTS = 1.75f;
static constexpr size_t DMA_FRAMES = 256;
static constexpr int DMA_BUFFER_COUNT = 8;
static constexpr uint32_t BASELINE_EOF_INTERVALS = 500;
static constexpr uint32_t PROBE_EOF_INTERVALS = 250;
static constexpr uint32_t FINAL_EOF_INTERVALS = 500;
static constexpr int INITIAL_BRACKET_RADIUS = 16;
static constexpr int MAX_BRACKET_RADIUS = 256;

#ifndef ADC_USE_APLL
#define ADC_USE_APLL 1
#endif

struct Measurement {
  int request;
  double rate;
  double errorPpm;
  uint64_t frames;
  uint64_t elapsedUs;
  uint32_t overflows;
  uint32_t dmaErrors;
  bool valid;
};

static QueueHandle_t i2sEvents;
static TaskHandle_t consumerHandle;
static volatile bool stopConsumer;
static uint16_t rxBlock[DMA_FRAMES];

static void injectAdcBias() {
  dac_output_enable(DAC_CHANNEL_2);
  const float code = 255.0f * ADC_BIAS_VOLTS / 3.3f;
  dac_output_voltage(DAC_CHANNEL_2, static_cast<uint8_t>(code + 0.5f));
}

static bool installAdc(int request) {
  i2sEvents = nullptr;
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(
      I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
  config.sample_rate = request;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = DMA_BUFFER_COUNT;
  config.dma_buf_len = DMA_FRAMES;
  config.use_apll = ADC_USE_APLL;
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
  while (!stopConsumer) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT, rxBlock, sizeof(rxBlock), &bytesRead,
             pdMS_TO_TICKS(50));
  }
  consumerHandle = nullptr;
  vTaskDelete(nullptr);
}

static void stopAdc() {
  stopConsumer = true;
  const uint64_t deadline = esp_timer_get_time() + 250000;
  while (consumerHandle != nullptr && esp_timer_get_time() < deadline)
    delay(1);
  i2s_adc_disable(I2S_PORT);
  i2s_driver_uninstall(I2S_PORT);
  i2sEvents = nullptr;
  delay(20);
}

static Measurement measure(int request, uint32_t eofIntervals) {
  Measurement result = {};
  result.request = request;
  if (!installAdc(request)) return result;

  stopConsumer = false;
  consumerHandle = nullptr;
  if (xTaskCreatePinnedToCore(consumerTask, "adc-consumer", 2048, nullptr, 2,
                              &consumerHandle, 0) != pdPASS) {
    stopAdc();
    return result;
  }

  uint64_t firstUs = 0;
  uint64_t lastUs = 0;
  uint64_t eofCount = 0;
  double sumX = 0.0;
  double sumY = 0.0;
  double sumXX = 0.0;
  double sumXY = 0.0;
  while (eofCount < static_cast<uint64_t>(eofIntervals) + 1) {
    i2s_event_t event;
    if (xQueueReceive(i2sEvents, &event, pdMS_TO_TICKS(250)) != pdTRUE)
      continue;
    if (event.type == I2S_EVENT_RX_Q_OVF) {
      ++result.overflows;
    } else if (event.type == I2S_EVENT_DMA_ERROR) {
      ++result.dmaErrors;
    } else if (event.type == I2S_EVENT_RX_DONE) {
      const uint64_t nowUs = esp_timer_get_time();
      if (firstUs == 0) firstUs = nowUs;
      lastUs = nowUs;
      const double x = static_cast<double>(eofCount);
      const double y = static_cast<double>(nowUs - firstUs);
      sumX += x;
      sumY += y;
      sumXX += x * x;
      sumXY += x * y;
      ++eofCount;
    }
  }
  stopAdc();

  if (eofCount < 2 || lastUs <= firstUs) return result;
  result.frames = (eofCount - 1) * DMA_FRAMES;
  result.elapsedUs = lastUs - firstUs;
  const double n = static_cast<double>(eofCount);
  const double denominator = n * sumXX - sumX * sumX;
  if (denominator <= 0.0) return result;
  const double usPerEof = (n * sumXY - sumX * sumY) / denominator;
  result.rate = static_cast<double>(DMA_FRAMES) * 1000000.0 / usPerEof;
  result.errorPpm = (result.rate / TARGET_RATE - 1.0) * 1.0e6;
  result.valid = true;
  return result;
}

static void printMeasurement(const char *label, const Measurement &m) {
  Serial.printf(
      "%s request=%d rate=%.3f_sps error=%+.1f_ppm frames=%llu "
      "elapsed=%.6f_s overflow=%u dma_error=%u\n",
      label, m.request, m.rate, m.errorPpm,
      static_cast<unsigned long long>(m.frames), m.elapsedUs / 1000000.0,
      m.overflows, m.dmaErrors);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  injectAdcBias();
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_12);

  Serial.printf(
      "ADC_AUTO_CAL target=%d use_apll=%d baseline_eofs=%u probe_eofs=%u "
      "final_eofs=%u\n",
      TARGET_RATE, ADC_USE_APLL, BASELINE_EOF_INTERVALS,
      PROBE_EOF_INTERVALS, FINAL_EOF_INTERVALS);

  const Measurement baseline = measure(TARGET_RATE, BASELINE_EOF_INTERVALS);
  if (!baseline.valid) {
    Serial.println("ADC_AUTO_CAL failed=baseline");
    return;
  }
  printMeasurement("BASELINE", baseline);

  const int estimate = static_cast<int>(lround(
      static_cast<double>(TARGET_RATE) * TARGET_RATE / baseline.rate));
  int radius = INITIAL_BRACKET_RADIUS;
  int lowRequest = estimate - radius;
  int highRequest = estimate + radius;
  Serial.printf("ADC_AUTO_CAL estimated_request=%d initial_bracket=%d..%d\n",
                estimate, lowRequest, highRequest);

  Measurement low = measure(lowRequest, PROBE_EOF_INTERVALS);
  Measurement high = measure(highRequest, PROBE_EOF_INTERVALS);
  printMeasurement("BRACKET_LOW", low);
  printMeasurement("BRACKET_HIGH", high);
  while (low.valid && low.errorPpm > 0.0 && radius < MAX_BRACKET_RADIUS) {
    radius *= 2;
    lowRequest = estimate - radius;
    low = measure(lowRequest, PROBE_EOF_INTERVALS);
    printMeasurement("EXPAND_LOW", low);
  }
  while (high.valid && high.errorPpm < 0.0 && radius < MAX_BRACKET_RADIUS) {
    radius *= 2;
    highRequest = estimate + radius;
    high = measure(highRequest, PROBE_EOF_INTERVALS);
    printMeasurement("EXPAND_HIGH", high);
  }
  if (!low.valid || !high.valid || low.errorPpm > 0.0 || high.errorPpm < 0.0) {
    Serial.println("ADC_AUTO_CAL failed=could_not_bracket_target");
    return;
  }

  while (highRequest - lowRequest > 2) {
    const int middleRequest = lowRequest + (highRequest - lowRequest) / 2;
    const Measurement middle = measure(middleRequest, PROBE_EOF_INTERVALS);
    if (!middle.valid) {
      Serial.printf("PROBE request=%d failed\n", middleRequest);
      return;
    }
    printMeasurement("PROBE", middle);
    if (middle.errorPpm < 0.0) {
      lowRequest = middleRequest;
      low = middle;
    } else {
      highRequest = middleRequest;
      high = middle;
    }
  }

  // Probe jitter is acceptable for bracketing, but choose the final integer
  // from equal, longer measurements of every remaining neighboring request.
  Measurement best = {};
  for (int request = lowRequest; request <= highRequest; ++request) {
    const Measurement candidate = measure(request, FINAL_EOF_INTERVALS);
    if (!candidate.valid) continue;
    printMeasurement("FINAL", candidate);
    if (!best.valid || fabs(candidate.errorPpm) < fabs(best.errorPpm)) {
      best = candidate;
    }
  }

  if (!best.valid) {
    Serial.println("ADC_AUTO_CAL failed=no_candidate");
    return;
  }
  Serial.printf(
      "ADC_AUTO_CAL optimal_request=%d measured_rate=%.3f_sps "
      "error=%+.1f_ppm overflow=%u dma_error=%u\n",
      best.request, best.rate, best.errorPpm,
      best.overflows, best.dmaErrors);
}

void loop() { delay(1000); }
