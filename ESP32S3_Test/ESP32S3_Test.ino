#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ===== ESP32-S3 ↔ TFT =====
#define TFT_CS    4
#define TFT_RST   5
#define TFT_DC    6

#define TFT_MOSI  15
#define TFT_SCK   16
#define TFT_MISO  17

Adafruit_ILI9341 tft(
  TFT_CS,
  TFT_DC,
  TFT_RST
);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32-S3 TFT TEST START");

  // SPI 시작
  SPI.begin(
    TFT_SCK,     // SCK
    TFT_MISO,    // MISO
    TFT_MOSI,    // MOSI
    TFT_CS
  );

  // TFT 초기화
  tft.begin();
  tft.setRotation(1);

  // 1. 검은 화면
  tft.fillScreen(ILI9341_BLACK);
  delay(500);

  // 2. 테스트 글자
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(35, 70);
  tft.println("ESP32-S3");

  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(3);
  tft.setCursor(45, 120);
  tft.println("TFT OK!");

  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(30, 180);
  tft.println("DISPLAY TEST");

  Serial.println("DISPLAY DRAW COMPLETE");
}

void loop() {
  // 아무것도 하지 않음.
  // 화면이 계속 유지되는지 확인하기 위한 테스트.
}