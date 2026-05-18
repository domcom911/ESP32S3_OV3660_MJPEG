#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"

// Pins used by the photographed ESP32-S3 WROOM N16R8 CAM board
// family (Keyestudio MB0184 / DIYables-style): SD_MMC 1-bit mode.
#define SD_MMC_CLK 39
#define SD_MMC_CMD 38
#define SD_MMC_D0  40

// 1-bit mode is safer and uses only CLK/CMD/D0.
// If your slot is wired for 4-bit mode, add D1/D2/D3 and call setPins with 6 args.
#define SD_MMC_1BIT_MODE true

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32-S3 SD_MMC test");
  Serial.printf("CLK=%d CMD=%d D0=%d 1-bit=%s\n",
                SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0,
                SD_MMC_1BIT_MODE ? "yes" : "no");

  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", SD_MMC_1BIT_MODE)) {
    Serial.println("SD_MMC mount failed. Check pins, card format, pullups, and 3.3V power.");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card detected.");
    return;
  }

  Serial.print("Card type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC/SDXC");
  } else {
    Serial.println("unknown");
  }

  Serial.printf("Card size: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));

  File file = SD_MMC.open("/esp32s3_test.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open test file for writing.");
    return;
  }
  file.println("ESP32-S3 SD_MMC write test OK");
  file.close();

  file = SD_MMC.open("/esp32s3_test.txt", FILE_READ);
  if (!file) {
    Serial.println("Failed to open test file for reading.");
    return;
  }

  Serial.println("File content:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("SD_MMC test finished.");
}

void loop() {
  delay(1000);
}
