
#include <Arduino.h>
#include <ESP_I2S.h>

constexpr int I2S_BCLK = 27;
constexpr int I2S_LRC  = 26;
constexpr int I2S_DOUT = 25;

constexpr uint32_t SAMPLE_RATE = 16000;

I2SClass AudioOut;

void setup() {
  Serial.begin(115200);

  // BCLK, WS, DOUT, DIN
  // 앰프만 테스트하므로 DIN은 사용하지 않음
  AudioOut.setPins(
    I2S_BCLK,
    I2S_LRC,
    I2S_DOUT,
    -1
  );

  if (!AudioOut.begin(
        I2S_MODE_STD,
        SAMPLE_RATE,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
      )) {
    Serial.println("I2S init failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Speaker test start");
}

void loop() {
  constexpr float FREQUENCY = 1000.0;
  constexpr int16_t AMPLITUDE = 4000;

  static uint32_t sampleIndex = 0;

  int16_t sample =
    static_cast<int16_t>(
      sin(
        2.0 * PI *
        FREQUENCY *
        sampleIndex /
        SAMPLE_RATE
      ) * AMPLITUDE
    );

  sampleIndex++;

  // Stereo I2S:
  // L, R 두 채널에 같은 신호를 보냄
  int16_t frame[2] = {
    sample,
    sample
  };

  AudioOut.write(
    reinterpret_cast<uint8_t *>(frame),
    sizeof(frame)
  );
}
