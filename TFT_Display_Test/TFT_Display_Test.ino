#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ===== TFT pins =====
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_DC    32
#define TFT_RST   33
#define TFT_CS    25

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("TFT TEST START");

  // ESP32 SPI
  SPI.begin(
    TFT_SCK,    // SCK
    -1,         // MISO 사용 안 함
    TFT_MOSI,   // MOSI
    TFT_CS
  );

  // TFT 시작
  tft.begin();
  tft.setRotation(1);

  // 검은색 화면
  tft.fillScreen(ILI9341_BLACK);

  // 테스트 글씨
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(3);

  tft.setCursor(40, 60);
  tft.println("TFT TEST");

  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);

  tft.setCursor(40, 120);
  tft.println("DISPLAY OK");

  Serial.println("TFT DRAW COMPLETE");
}

void loop() {
  // 일부러 아무것도 안 함.
  // 한번 그린 화면이 계속 유지되는지 확인.
}