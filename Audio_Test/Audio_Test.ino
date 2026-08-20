#include <Arduino.h>
#include "driver/i2s_std.h"

// ============================================================
// PIN
// ============================================================

// INMP441
#define MIC_SCK 35
#define MIC_WS  36
#define MIC_SD  42

// MAX98357A
#define AMP_BCLK 11
#define AMP_LRC  12
#define AMP_DIN  14

// ============================================================
// AUDIO SETTINGS
// ============================================================

#define SAMPLE_RATE 16000
#define RECORD_SECONDS 3

// 16kHz × 3초 × 16bit mono
#define SAMPLE_COUNT (SAMPLE_RATE * RECORD_SECONDS)

// 녹음 데이터 RAM
int16_t *recordBuffer = nullptr;

// I2S handles
i2s_chan_handle_t micRx = NULL;
i2s_chan_handle_t ampTx = NULL;

// ============================================================
// MIC INIT
// ============================================================

bool initMic() {

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(
          I2S_NUM_0,
          I2S_ROLE_MASTER
      );

  esp_err_t err =
      i2s_new_channel(
          &chan_cfg,
          NULL,
          &micRx
      );

  if (err != ESP_OK) {
    Serial.println("MIC channel create failed");
    return false;
  }

  i2s_std_config_t cfg = {

    .clk_cfg =
        I2S_STD_CLK_DEFAULT_CONFIG(
            SAMPLE_RATE
        ),

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

  err =
      i2s_channel_init_std_mode(
          micRx,
          &cfg
      );

  if (err != ESP_OK) {
    Serial.println("MIC config failed");
    return false;
  }

  return true;
}

// ============================================================
// AMP INIT
// ============================================================

bool initAmp() {

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(
          I2S_NUM_1,
          I2S_ROLE_MASTER
      );

  esp_err_t err =
      i2s_new_channel(
          &chan_cfg,
          &ampTx,
          NULL
      );

  if (err != ESP_OK) {
    Serial.println("AMP channel create failed");
    return false;
  }

  i2s_std_config_t cfg = {

    .clk_cfg =
        I2S_STD_CLK_DEFAULT_CONFIG(
            SAMPLE_RATE
        ),

    .slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),

    .gpio_cfg = {

      .mclk = I2S_GPIO_UNUSED,

      .bclk = (gpio_num_t)AMP_BCLK,
      .ws   = (gpio_num_t)AMP_LRC,

      .dout = (gpio_num_t)AMP_DIN,
      .din  = I2S_GPIO_UNUSED,

      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false
      }
    }
  };

  err =
      i2s_channel_init_std_mode(
          ampTx,
          &cfg
      );

  if (err != ESP_OK) {
    Serial.println("AMP config failed");
    return false;
  }

  return true;
}

// ============================================================
// RECORD
// ============================================================

bool recordAudio() {

  Serial.println();
  Serial.println("=== RECORD START ===");
  Serial.println("Speak now!");

  if (
    i2s_channel_enable(micRx)
    != ESP_OK
  ) {
    Serial.println("MIC enable failed");
    return false;
  }

  int32_t raw[256];

  size_t recordedSamples = 0;

  while (
    recordedSamples <
    SAMPLE_COUNT
  ) {

    size_t bytesRead = 0;

    esp_err_t err =
        i2s_channel_read(
            micRx,
            raw,
            sizeof(raw),
            &bytesRead,
            portMAX_DELAY
        );

    if (
      err != ESP_OK ||
      bytesRead == 0
    ) {
      continue;
    }

    size_t rawCount =
        bytesRead /
        sizeof(int32_t);

    // L/R = GND → LEFT 슬롯
    for (
      size_t i = 0;
      i + 1 < rawCount &&
      recordedSamples < SAMPLE_COUNT;
      i += 2
    ) {

      int32_t sample24 =
          raw[i] >> 8;

      // 24bit → 16bit
      int16_t sample16 =
          (int16_t)(
            sample24 >> 8
          );

      recordBuffer[
        recordedSamples++
      ] = sample16;
    }
  }

  i2s_channel_disable(
      micRx
  );

  Serial.println("=== RECORD COMPLETE ===");

  return true;
}

// ============================================================
// PLAY
// ============================================================

void playAudio() {

  Serial.println();
  Serial.println("=== PLAY START ===");

  if (
    i2s_channel_enable(ampTx)
    != ESP_OK
  ) {
    Serial.println("AMP enable failed");
    return;
  }

  int16_t stereo[256];

  size_t playedSamples = 0;

  while (
    playedSamples <
    SAMPLE_COUNT
  ) {

    size_t chunk =
        min(
          (size_t)128,
          (size_t)(
            SAMPLE_COUNT -
            playedSamples
          )
        );

    for (
      size_t i = 0;
      i < chunk;
      i++
    ) {

      int16_t s =
          recordBuffer[
            playedSamples + i
          ];

      stereo[i * 2]     = s;
      stereo[i * 2 + 1] = s;
    }

    size_t bytesWritten = 0;

    i2s_channel_write(
        ampTx,
        stereo,
        chunk * 2 *
        sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY
    );

    playedSamples +=
        chunk;
  }

  i2s_channel_disable(
      ampTx
  );

  Serial.println("=== PLAY COMPLETE ===");
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32-S3 AUDIO TEST");

  // 3초 mono 16bit
  recordBuffer =
      (int16_t *)malloc(
          SAMPLE_COUNT *
          sizeof(int16_t)
      );

  if (
    recordBuffer == nullptr
  ) {

    Serial.println(
      "RAM allocation failed"
    );

    while (true) {
      delay(1000);
    }
  }

  Serial.print("Buffer bytes = ");

  Serial.println(
      SAMPLE_COUNT *
      sizeof(int16_t)
  );

  if (!initMic()) {

    Serial.println(
      "MIC INIT FAILED"
    );

    while (true) {
      delay(1000);
    }
  }

  if (!initAmp()) {

    Serial.println(
      "AMP INIT FAILED"
    );

    while (true) {
      delay(1000);
    }
  }

  Serial.println(
    "Audio devices ready"
  );

  delay(1000);

  // 녹음
  if (recordAudio()) {

    delay(1000);

    // 재생
    playAudio();
  }
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // 5초마다 다시 녹음 → 재생 테스트
  delay(5000);

  if (recordAudio()) {

    delay(1000);

    playAudio();
  }
}