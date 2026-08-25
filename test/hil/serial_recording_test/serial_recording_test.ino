// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <FreeDv2400b.h>

using namespace freedv2400b;

static FreeDv2400bDecoder decoder;
static int16_t inputSamples[MAX_RX_INPUT];
static uint8_t payload[PAYLOAD_BYTES];

static bool readExact(uint8_t *destination, size_t length) {
  size_t received = 0;
  const uint32_t deadline = millis() + 10000;
  while (received < length && static_cast<int32_t>(deadline - millis()) > 0) {
    int available = Serial.available();
    if (available > 0) {
      size_t count = min(length - received, static_cast<size_t>(available));
      received += Serial.readBytes(destination + received, count);
    } else {
      delay(1);
    }
  }
  return received == length;
}

static void printPayload(const uint8_t *bytes) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  for (int i = 0; i < PAYLOAD_BYTES; ++i) {
    Serial.write(HEX_DIGITS[bytes[i] >> 4]);
    Serial.write(HEX_DIGITS[bytes[i] & 15]);
  }
}

void setup() {
  Serial.begin(921600);
  Serial.setTimeout(10000);
  delay(100);
  uint32_t totalSamples = 0;
  uint32_t handshakeDeadline = millis() + 30000;
  while (Serial.available() < static_cast<int>(sizeof(totalSamples)) &&
         static_cast<int32_t>(handshakeDeadline - millis()) > 0) {
    Serial.println("FREEDV2400B_SERIAL_RX_V1");
    delay(250);
  }
  if (!readExact(reinterpret_cast<uint8_t *>(&totalSamples), sizeof(totalSamples))) {
    Serial.println("ERROR=sample-count-timeout");
    return;
  }

  uint32_t consumed = 0;
  int calls = 0;
  while (totalSamples - consumed >= static_cast<uint32_t>(decoder.inputSamplesRequired())) {
    int required = decoder.inputSamplesRequired();
    Serial.printf("NEED=%d\n", required);
    if (!readExact(reinterpret_cast<uint8_t *>(inputSamples), required * sizeof(int16_t))) {
      Serial.println("ERROR=pcm-timeout");
      return;
    }
    FreeDv2400bDecodeResult result;
    decoder.decode(inputSamples, required, payload, result);
    consumed += required;
    ++calls;
    Serial.printf("RESULT=%d,%d,%d,%d,%u,", calls, result.synchronized ? 1 : 0,
                  result.framePresent ? 1 : 0, static_cast<int>(result.frameType),
                  result.uniqueWordErrors);
    if (result.framePresent && result.frameType == FreeDv2400bFrameType::VOICE) {
      printPayload(payload);
    } else {
      Serial.write('-');
    }
    Serial.println();
  }
  Serial.printf("DONE=%d,%lu,%d\n", calls, static_cast<unsigned long>(consumed),
                decoder.inputSamplesRequired());
}

void loop() {}
