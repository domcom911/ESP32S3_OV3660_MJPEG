#include <Arduino.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN -1
#endif

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) {
    delay(10);
  }

  if (LED_BUILTIN >= 0) {
    pinMode(LED_BUILTIN, OUTPUT);
  }

  Serial.println();
  Serial.println("ESP32-S3 firmware upload test");
  Serial.printf("Chip: %s rev %u, cores: %u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("Flash size: %u bytes\n", (unsigned)ESP.getFlashChipSize());
  Serial.printf("PSRAM found: %s\n", psramFound() ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", (unsigned)ESP.getPsramSize());
}

void loop() {
  static bool ledState = false;
  static uint32_t counter = 0;

  if (LED_BUILTIN >= 0) {
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
    ledState = !ledState;
  }

  Serial.printf("Alive %lu ms, counter=%lu, free_heap=%u, free_psram=%u\n",
                (unsigned long)millis(),
                (unsigned long)counter++,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  delay(1000);
}
