#include <Arduino.h>
#include "driver/i2s_std.h"

#define MIC_SCK 4
#define MIC_WS  5
#define MIC_SD  34

#define SAMPLE_RATE 16000

i2s_chan_handle_t rx_chan = NULL;

unsigned long lastPrint = 0;
int64_t peak3sec = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== INMP441 TEST ===");

  // I2S RX 채널 생성
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(
          I2S_NUM_0,
          I2S_ROLE_MASTER
      );

  esp_err_t err = i2s_new_channel(
      &chan_cfg,
      NULL,
      &rx_chan
  );

  if (err != ESP_OK) {
    Serial.print("Channel create failed: ");
    Serial.println(err);
    while (1) delay(1000);
  }

  // Philips I2S / 32-bit stereo
  i2s_std_config_t std_cfg = {

    .clk_cfg =
        I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),

    .slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO
        ),

    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = (gpio_num_t)MIC_SCK,
        .ws   = (gpio_num_t)MIC_WS,

        .dout = I2S_GPIO_UNUSED,
        .din  = (gpio_num_t)MIC_SD,

        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv   = false
        }
    }
  };

  err = i2s_channel_init_std_mode(
      rx_chan,
      &std_cfg
  );

  if (err != ESP_OK) {
    Serial.print("I2S config failed: ");
    Serial.println(err);
    while (1) delay(1000);
  }

  err = i2s_channel_enable(rx_chan);

  if (err != ESP_OK) {
    Serial.print("I2S enable failed: ");
    Serial.println(err);
    while (1) delay(1000);
  }

  Serial.println("I2S READY");
  Serial.println("Speak or clap near microphone.");
}

void loop() {

  int32_t samples[256];
  size_t bytesRead = 0;

  esp_err_t err = i2s_channel_read(
      rx_chan,
      samples,
      sizeof(samples),
      &bytesRead,
      portMAX_DELAY
  );

  if (err != ESP_OK || bytesRead == 0) {
    return;
  }

  int sampleCount =
      bytesRead / sizeof(int32_t);

  int64_t currentPeak = 0;

  // L/R = GND → LEFT 슬롯 사용
  for (int i = 0; i + 1 < sampleCount; i += 2) {

    int32_t raw = samples[i];

    // 32-bit slot → 24-bit microphone data
    int32_t pcm = raw >> 8;

    int64_t level = (int64_t)pcm;

    // 음수 제거 → 항상 0 이상
    if (level < 0) {
      level = -level;
    }

    // 혹시라도 이상값이면 0 아래로 내려가지 않게
    if (level < 0) {
      level = 0;
    }

    if (level > currentPeak) {
      currentPeak = level;
    }
  }

  if (currentPeak > peak3sec) {
    peak3sec = currentPeak;
  }

  // 3초마다 한 번 출력
  if (millis() - lastPrint >= 3000) {

    if (peak3sec < 0) {
      peak3sec = 0;
    }

    Serial.print("MIC PEAK = ");
    Serial.println((long long)peak3sec);

    peak3sec = 0;
    lastPrint = millis();
  }
}