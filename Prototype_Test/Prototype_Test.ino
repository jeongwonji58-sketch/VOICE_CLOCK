#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "unihiker_k10.h"
#include "driver/i2s.h"

UNIHIKER_K10 k10;

// ============================================================
// Wi-Fi / Clock
// ============================================================
// 실제 Wi-Fi 정보로 바꿔주세요.
const char* WIFI_SSID = "Kkdnd";
const char* WIFI_PASSWORD = "winwin0321@";

const char* TZ_INFO = "KST-9";
const char* NTP_1 = "time.google.com";
const char* NTP_2 = "pool.ntp.org";

bool timeSynced = false;

// ============================================================
// Screen
// ============================================================
// 가로 화면용.
// 만약 가로인데 180도 뒤집혀 보이면 1 -> 3 으로 바꿔보세요.
constexpr int SCREEN_DIR = 1;

enum ScreenMode {
  SCREEN_CLOCK,
  SCREEN_STOPWATCH,
  SCREEN_RECORDER
};

ScreenMode currentScreen = SCREEN_CLOCK;

// ============================================================
// Audio
// ============================================================
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t RECORD_SECONDS = 3;

constexpr uint32_t CHANNELS = 2;
constexpr uint32_t BYTES_PER_SAMPLE = 2;

constexpr size_t RECORD_BUFFER_SIZE =
    SAMPLE_RATE *
    RECORD_SECONDS *
    CHANNELS *
    BYTES_PER_SAMPLE;

constexpr int SLOT_COUNT = 4;

uint8_t* audioSlot[SLOT_COUNT] = {
  nullptr, nullptr, nullptr, nullptr
};

size_t audioBytes[SLOT_COUNT] = {
  0, 0, 0, 0
};

bool slotValid[SLOT_COUNT] = {
  false, false, false, false
};

// 녹음은 1 -> 2 -> 3 -> 4 -> 1 순환 덮어쓰기
int nextRecordSlot = 0;

// 재생할 슬롯
int selectedSlot = 0;

// ============================================================
// Stopwatch
// ============================================================
bool stopwatchRunning = false;
uint32_t stopwatchStartMs = 0;
uint32_t stopwatchStoredMs = 0;

// ============================================================
// Button handling
// ============================================================
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr uint32_t DEBOUNCE_MS = 40;

struct ButtonState {
  bool previousPressed = false;
  uint32_t pressedAt = 0;
  bool longHandled = false;
};

ButtonState buttonAState;
ButtonState buttonBState;

// ============================================================
// Screen refresh
// ============================================================
uint32_t lastClockDraw = 0;
uint32_t lastStopwatchDraw = 0;

// ============================================================
// Utility
// ============================================================

void clearScreen() {
  k10.canvas->canvasClear();
  k10.setScreenBackground(0x000000);
}

void drawText(
  const String& text,
  int x,
  int y,
  uint32_t color,
  Canvas::eFontSize_t font = Canvas::eCNAndENFont24
) {
  k10.canvas->canvasText(
    text,
    x,
    y,
    color,
    font,
    40,
    false
  );
}

void updateDisplay() {
  k10.canvas->updateCanvas();
}

void setRgb(uint32_t color) {
  k10.rgb->write(-1, color);
}

String formatStopwatch(uint32_t ms) {
  uint32_t minutes = ms / 60000;
  uint32_t seconds = (ms % 60000) / 1000;
  uint32_t centiseconds = (ms % 1000) / 10;

  char buf[20];

  snprintf(
    buf,
    sizeof(buf),
    "%02lu:%02lu.%02lu",
    (unsigned long)minutes,
    (unsigned long)seconds,
    (unsigned long)centiseconds
  );

  return String(buf);
}

uint32_t getStopwatchMs() {
  if (stopwatchRunning) {
    return stopwatchStoredMs + millis() - stopwatchStartMs;
  }

  return stopwatchStoredMs;
}

// ============================================================
// Wi-Fi / NTP
// ============================================================

void initializeClock() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - started < 8000
  ) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi not connected. Clock will show TIME NOT SYNCED.");
    return;
  }

  configTzTime(
    TZ_INFO,
    NTP_1,
    NTP_2
  );

  struct tm info;

  started = millis();

  while (millis() - started < 5000) {
    if (getLocalTime(&info, 200)) {
      timeSynced = true;
      Serial.println("NTP time synchronized.");
      return;
    }
  }

  Serial.println("NTP synchronization failed.");
}

// ============================================================
// Draw: Clock
// ============================================================

void drawClockScreen() {
  clearScreen();

  drawText(
    "VOICE CLOCK",
    85,
    12,
    0x00FFFF
  );

  struct tm now;

  if (getLocalTime(&now, 10)) {
    timeSynced = true;

    char timeText[16];
    char dateText[20];

    snprintf(
      timeText,
      sizeof(timeText),
      "%02d:%02d:%02d",
      now.tm_hour,
      now.tm_min,
      now.tm_sec
    );

    snprintf(
      dateText,
      sizeof(dateText),
      "%04d-%02d-%02d",
      now.tm_year + 1900,
      now.tm_mon + 1,
      now.tm_mday
    );

    drawText(
      timeText,
      72,
      78,
      0xFFFFFF
    );

    drawText(
      dateText,
      83,
      122,
      0x00FF80,
      Canvas::eCNAndENFont16
    );
  } else {
    drawText(
      "TIME NOT SYNCED",
      65,
      90,
      0xFF4040,
      Canvas::eCNAndENFont16
    );
  }

  drawText(
    "B : NEXT MODE",
    92,
    202,
    0xA0A0A0,
    Canvas::eCNAndENFont16
  );

  updateDisplay();
}

// ============================================================
// Draw: Stopwatch
// ============================================================

void drawStopwatchScreen() {
  clearScreen();

  drawText(
    "STOPWATCH",
    98,
    15,
    0x00FFFF
  );

  drawText(
    formatStopwatch(getStopwatchMs()),
    70,
    82,
    stopwatchRunning ? 0x00FF00 : 0xFFFFFF
  );

  drawText(
    stopwatchRunning ? "RUNNING" : "STOPPED",
    115,
    128,
    stopwatchRunning ? 0x00FF00 : 0xFF4040,
    Canvas::eCNAndENFont16
  );

  drawText(
    "A : START / STOP",
    20,
    177,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  drawText(
    "HOLD A : RESET   B : NEXT",
    20,
    207,
    0xA0A0A0,
    Canvas::eCNAndENFont16
  );

  updateDisplay();
}

// ============================================================
// Draw: Recorder
// ============================================================

void drawRecorderScreen() {
  clearScreen();

  drawText(
    "RECORDER",
    105,
    10,
    0x00FFFF
  );

  char selectedText[32];

  snprintf(
    selectedText,
    sizeof(selectedText),
    "SELECT SLOT : %d",
    selectedSlot + 1
  );

  drawText(
    selectedText,
    75,
    53,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  char nextText[32];

  snprintf(
    nextText,
    sizeof(nextText),
    "NEXT REC : %d",
    nextRecordSlot + 1
  );

  drawText(
    nextText,
    92,
    80,
    0xFFD000,
    Canvas::eCNAndENFont16
  );

  String slots;

  for (int i = 0; i < SLOT_COUNT; i++) {
    slots += String(i + 1);
    slots += slotValid[i] ? ":O" : ":-";

    if (i < SLOT_COUNT - 1) {
      slots += "   ";
    }
  }

  drawText(
    slots,
    45,
    115,
    0x00FF80,
    Canvas::eCNAndENFont16
  );

  drawText(
    "A : PLAY",
    20,
    157,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  drawText(
    "HOLD A : RECORD 3 SEC",
    20,
    181,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  drawText(
    "B : SLOT   HOLD B : NEXT",
    20,
    211,
    0xA0A0A0,
    Canvas::eCNAndENFont16
  );

  updateDisplay();
}

// ============================================================
// Screen management
// ============================================================

void drawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_CLOCK:
      drawClockScreen();
      break;

    case SCREEN_STOPWATCH:
      drawStopwatchScreen();
      break;

    case SCREEN_RECORDER:
      drawRecorderScreen();
      break;
  }
}

void nextScreen() {
  currentScreen =
    (ScreenMode)(
      ((int)currentScreen + 1) % 3
    );

  drawCurrentScreen();
}

// ============================================================
// Stopwatch actions
// ============================================================

void toggleStopwatch() {
  if (!stopwatchRunning) {
    stopwatchStartMs = millis();
    stopwatchRunning = true;
  } else {
    stopwatchStoredMs +=
      millis() - stopwatchStartMs;

    stopwatchRunning = false;
  }

  drawStopwatchScreen();
}

void resetStopwatch() {
  stopwatchRunning = false;
  stopwatchStartMs = 0;
  stopwatchStoredMs = 0;

  drawStopwatchScreen();
}

// ============================================================
// Recorder actions
// ============================================================

bool recordToSlot(int slot) {
  if (
    slot < 0 ||
    slot >= SLOT_COUNT ||
    audioSlot[slot] == nullptr
  ) {
    return false;
  }

  clearScreen();

  drawText(
    "RECORDING...",
    88,
    80,
    0xFF3030
  );

  char slotText[20];

  snprintf(
    slotText,
    sizeof(slotText),
    "SLOT %d / 4",
    slot + 1
  );

  drawText(
    slotText,
    115,
    125,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  drawText(
    "3 SECONDS",
    108,
    160,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  updateDisplay();

  setRgb(0xFF0000);

  i2s_set_clk(
    I2S_NUM_0,
    SAMPLE_RATE,
    I2S_BITS_PER_SAMPLE_16BIT,
    I2S_CHANNEL_STEREO
  );

  i2s_zero_dma_buffer(I2S_NUM_0);

  size_t totalRead = 0;

  while (totalRead < RECORD_BUFFER_SIZE) {
    size_t bytesRead = 0;

    size_t remaining =
      RECORD_BUFFER_SIZE -
      totalRead;

    size_t chunk =
      remaining > 1024
        ? 1024
        : remaining;

    esp_err_t result =
      i2s_read(
        I2S_NUM_0,
        audioSlot[slot] + totalRead,
        chunk,
        &bytesRead,
        portMAX_DELAY
      );

    if (result != ESP_OK) {
      Serial.println("I2S recording error.");
      setRgb(0x000000);
      return false;
    }

    totalRead += bytesRead;
  }

  audioBytes[slot] = totalRead;
  slotValid[slot] = true;

  selectedSlot = slot;

  nextRecordSlot =
    (slot + 1) %
    SLOT_COUNT;

  setRgb(0x000000);

  clearScreen();

  drawText(
    "RECORD COMPLETE",
    50,
    92,
    0x00FF80
  );

  drawText(
    "A : PLAY IT",
    102,
    145,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  updateDisplay();

  delay(700);

  drawRecorderScreen();

  return true;
}

void playSlot(int slot) {
  if (
    slot < 0 ||
    slot >= SLOT_COUNT ||
    !slotValid[slot] ||
    audioSlot[slot] == nullptr ||
    audioBytes[slot] == 0
  ) {
    clearScreen();

    drawText(
      "EMPTY SLOT",
      90,
      95,
      0xFF4040
    );

    drawText(
      "HOLD A TO RECORD",
      80,
      142,
      0xFFFFFF,
      Canvas::eCNAndENFont16
    );

    updateDisplay();

    delay(700);

    drawRecorderScreen();

    return;
  }

  clearScreen();

  drawText(
    "PLAYING...",
    102,
    80,
    0x00FF00
  );

  char slotText[20];

  snprintf(
    slotText,
    sizeof(slotText),
    "SLOT %d / 4",
    slot + 1
  );

  drawText(
    slotText,
    115,
    130,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  updateDisplay();

  setRgb(0x00FF00);

  i2s_set_clk(
    I2S_NUM_0,
    SAMPLE_RATE,
    I2S_BITS_PER_SAMPLE_16BIT,
    I2S_CHANNEL_STEREO
  );

  i2s_zero_dma_buffer(I2S_NUM_0);

  size_t totalWritten = 0;

  while (
    totalWritten <
    audioBytes[slot]
  ) {
    size_t bytesWritten = 0;

    size_t remaining =
      audioBytes[slot] -
      totalWritten;

    size_t chunk =
      remaining > 1024
        ? 1024
        : remaining;

    esp_err_t result =
      i2s_write(
        I2S_NUM_0,
        audioSlot[slot] + totalWritten,
        chunk,
        &bytesWritten,
        portMAX_DELAY
      );

    if (result != ESP_OK) {
      Serial.println("I2S playback error.");
      break;
    }

    totalWritten += bytesWritten;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);

  setRgb(0x000000);

  drawRecorderScreen();
}

// ============================================================
// Button events
// ============================================================

void handleAShort() {
  switch (currentScreen) {
    case SCREEN_CLOCK:
      break;

    case SCREEN_STOPWATCH:
      toggleStopwatch();
      break;

    case SCREEN_RECORDER:
      playSlot(selectedSlot);
      break;
  }
}

void handleALong() {
  switch (currentScreen) {
    case SCREEN_CLOCK:
      break;

    case SCREEN_STOPWATCH:
      resetStopwatch();
      break;

    case SCREEN_RECORDER:
      // 자동 순환 슬롯:
      // 1 -> 2 -> 3 -> 4 -> 1...
      recordToSlot(nextRecordSlot);
      break;
  }
}

void handleBShort() {
  if (currentScreen == SCREEN_RECORDER) {
    selectedSlot =
      (selectedSlot + 1) %
      SLOT_COUNT;

    drawRecorderScreen();
  } else {
    nextScreen();
  }
}

void handleBLong() {
  // Recorder에서는 B 짧게가 슬롯 선택이므로
  // 길게 눌러 다음 화면으로 이동.
  // 다른 화면에서도 길게 누르면 다음 화면으로 이동.
  nextScreen();
}

void updateButton(
  bool pressed,
  ButtonState& state,
  void (*shortAction)(),
  void (*longAction)()
) {
  const uint32_t now = millis();

  if (
    pressed &&
    !state.previousPressed
  ) {
    state.pressedAt = now;
    state.longHandled = false;
  }

  if (
    pressed &&
    !state.longHandled &&
    now - state.pressedAt >=
      LONG_PRESS_MS
  ) {
    state.longHandled = true;

    if (longAction != nullptr) {
      longAction();
    }
  }

  if (
    !pressed &&
    state.previousPressed
  ) {
    const uint32_t held =
      now - state.pressedAt;

    if (
      !state.longHandled &&
      held >= DEBOUNCE_MS
    ) {
      if (shortAction != nullptr) {
        shortAction();
      }
    }
  }

  state.previousPressed = pressed;
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(700);

  k10.begin();

  // 가로 화면
  k10.initScreen(SCREEN_DIR);
  k10.creatCanvas();

  k10.setScreenBackground(0x000000);

  setRgb(0x000000);

  // K10 내부 I2S
  i2s_set_clk(
    I2S_NUM_0,
    SAMPLE_RATE,
    I2S_BITS_PER_SAMPLE_16BIT,
    I2S_CHANNEL_STEREO
  );

  // 4개 녹음 슬롯을 PSRAM에 할당
  for (int i = 0; i < SLOT_COUNT; i++) {
    audioSlot[i] =
      (uint8_t*)ps_malloc(
        RECORD_BUFFER_SIZE
      );

    if (audioSlot[i] == nullptr) {
      Serial.print("PSRAM allocation failed at slot ");
      Serial.println(i + 1);

      clearScreen();

      drawText(
        "PSRAM ERROR",
        90,
        95,
        0xFF0000
      );

      updateDisplay();

      while (true) {
        delay(1000);
      }
    }
  }

  Serial.print("Each slot bytes = ");
  Serial.println(RECORD_BUFFER_SIZE);

  Serial.print("Total audio RAM = ");
  Serial.println(
    RECORD_BUFFER_SIZE *
    SLOT_COUNT
  );

  clearScreen();

  drawText(
    "VOICE CLOCK",
    85,
    70,
    0x00FFFF
  );

  drawText(
    "STARTING...",
    105,
    120,
    0xFFFFFF,
    Canvas::eCNAndENFont16
  );

  updateDisplay();

  initializeClock();

  drawClockScreen();
}

// ============================================================
// Loop
// ============================================================

void loop() {
  bool aPressed =
    k10.buttonA->isPressed();

  bool bPressed =
    k10.buttonB->isPressed();

  updateButton(
    aPressed,
    buttonAState,
    handleAShort,
    handleALong
  );

  updateButton(
    bPressed,
    buttonBState,
    handleBShort,
    handleBLong
  );

  // 주기적 화면 갱신
  if (
    currentScreen == SCREEN_CLOCK &&
    millis() - lastClockDraw >= 1000
  ) {
    lastClockDraw = millis();
    drawClockScreen();
  }

  if (
    currentScreen == SCREEN_STOPWATCH &&
    stopwatchRunning &&
    millis() - lastStopwatchDraw >= 100
  ) {
    lastStopwatchDraw = millis();
    drawStopwatchScreen();
  }

  delay(10);
}
