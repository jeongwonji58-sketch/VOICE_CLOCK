#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ============================================================
// Wi-Fi / NTP
// ============================================================

constexpr char WIFI_SSID[]     = "와이파이 아이디";
constexpr char WIFI_PASSWORD[] = "비밀번호";

constexpr char NTP_SERVER_1[] = "time.google.com";
constexpr char NTP_SERVER_2[] = "time.cloudflare.com";
constexpr char TIME_ZONE[]    = "KST-9";

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS     = 15000;

// ============================================================
// Pin configuration
// ============================================================

// TFT / SD shared SPI
constexpr int PIN_TFT_CS   = 4;
constexpr int PIN_TFT_RST  = 5;
constexpr int PIN_TFT_DC   = 6;

constexpr int PIN_SPI_MOSI = 15;
constexpr int PIN_SPI_SCK  = 16;
constexpr int PIN_SPI_MISO = 17;

constexpr int PIN_SD_CS = 45;

// Buttons
constexpr int PIN_BUTTON_LEFT   = 19;
constexpr int PIN_BUTTON_CENTER = 20;
constexpr int PIN_BUTTON_RIGHT  = 21;

// RGB LED
constexpr int PIN_LED_R = 37;
constexpr int PIN_LED_G = 38;
constexpr int PIN_LED_B = 39;

// 공통 VCC RGB LED 모듈은 Active Low
// LOW  = 켜짐
// HIGH = 꺼짐
constexpr uint8_t LED_ON  = LOW;
constexpr uint8_t LED_OFF = HIGH;

// Buzzer
constexpr int PIN_BUZZER = 10;

// ============================================================
// Product configuration
// ============================================================

constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t LONG_PRESS_MS      = 2000;
constexpr uint32_t COMBO_HOLD_MS      = 100;

constexpr uint32_t MAX_RECORDING_TIME_MS = 60000;
constexpr uint32_t PLAY_INDICATOR_MS     = 1500;

constexpr uint16_t BUZZER_FREQUENCY_HZ = 2000;
constexpr uint16_t BUZZER_DURATION_MS   = 700;

constexpr uint32_t CLOCK_UPDATE_MS     = 1000;
constexpr uint32_t STOPWATCH_UPDATE_MS = 50;
constexpr uint32_t RECORDER_UPDATE_MS  = 100;

constexpr uint8_t MAX_RECORDING_FILES = 4;

constexpr bool ENABLE_EVENT_LOG = false;

// ============================================================
// Display
// ============================================================

Adafruit_ILI9341 tft(
  PIN_TFT_CS,
  PIN_TFT_DC,
  PIN_TFT_RST
);

// ============================================================
// Types
// ============================================================

enum class ScreenMode : uint8_t {
  CLOCK,
  STOPWATCH,
  RECORDER
};

enum class LedState : uint8_t {
  OFF,
  WHITE,
  RED,
  BLUE
};

struct Button {
  int pin;

  bool rawState;
  bool previousRawState;
  bool stableState;

  uint32_t lastChangeTime;
  uint32_t pressedTime;

  bool pressedEvent;
  bool releasedEvent;
  bool shortPressEvent;
  bool longPressEvent;

  bool longPressHandled;
  bool suppressShortPress;
};

struct RecordingSlot {
  bool exists;
  uint32_t durationMs;
};

// ============================================================
// Global state
// ============================================================

ScreenMode currentScreen = ScreenMode::CLOCK;

Button buttonLeft{
  PIN_BUTTON_LEFT,
  HIGH,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false,
  false,
  false,
  false,
  false
};

Button buttonCenter{
  PIN_BUTTON_CENTER,
  HIGH,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false,
  false,
  false,
  false,
  false
};

Button buttonRight{
  PIN_BUTTON_RIGHT,
  HIGH,
  HIGH,
  HIGH,
  0,
  0,
  false,
  false,
  false,
  false,
  false,
  false
};

// Stopwatch
bool stopwatchRunning = false;
uint32_t stopwatchStartedAt = 0;
uint32_t stopwatchElapsedMs = 0;

// Stopwatch reset combination
bool comboActive = false;
bool comboHandled = false;
uint32_t comboStartedAt = 0;

// Recorder
bool recording = false;
uint32_t recordingStartedAt = 0;

RecordingSlot recordingSlots[MAX_RECORDING_FILES] = {};

uint8_t nextWriteSlot = 0;
uint8_t nextPlaySlot = 0;
uint8_t savedFileCount = 0;

int8_t indicatedPlaySlot = -1;
uint32_t playIndicatorUntil = 0;

// System
bool wifiConnected = false;
bool timeSynchronized = false;
bool sdAvailable = false;

bool screenLayoutDirty = true;
bool screenContentDirty = true;

uint32_t lastScreenUpdate = 0;

// ============================================================
// Logging
// ============================================================

void logEvent(const char *message) {
  if (ENABLE_EVENT_LOG) {
    Serial.println(message);
  }
}

void logError(const char *message) {
  Serial.print("[ERR] ");
  Serial.println(message);
}

// ============================================================
// Buzzer
// ============================================================

void beepRecordingTimeout() {
  tone(
    PIN_BUZZER,
    BUZZER_FREQUENCY_HZ,
    BUZZER_DURATION_MS
  );
}

// ============================================================
// LED
// ============================================================

void setLed(LedState state) {
  bool red = false;
  bool green = false;
  bool blue = false;

  switch (state) {
    case LedState::WHITE:
      red = true;
      green = true;
      blue = true;
      break;

    case LedState::RED:
      red = true;
      break;

    case LedState::BLUE:
      blue = true;
      break;

    case LedState::OFF:
    default:
      break;
  }

  // Active Low 방식
  // true이면 LOW를 출력하여 LED를 켜고,
  // false이면 HIGH를 출력하여 LED를 끈다.
  digitalWrite(
    PIN_LED_R,
    red ? LED_ON : LED_OFF
  );

  digitalWrite(
    PIN_LED_G,
    green ? LED_ON : LED_OFF
  );

  digitalWrite(
    PIN_LED_B,
    blue ? LED_ON : LED_OFF
  );
}

void updateLedForCurrentState() {
  if (currentScreen == ScreenMode::RECORDER) {
    if (recording) {
      setLed(LedState::WHITE);
      return;
    }

    if (
      indicatedPlaySlot >= 0 &&
      static_cast<int32_t>(
        playIndicatorUntil - millis()
      ) > 0
    ) {
      setLed(LedState::BLUE);
      return;
    }

    setLed(LedState::RED);
    return;
  }

  if (currentScreen == ScreenMode::STOPWATCH) {
    setLed(
      stopwatchRunning
        ? LedState::WHITE
        : LedState::OFF
    );

    return;
  }

  setLed(
    timeSynchronized
      ? LedState::BLUE
      : LedState::RED
  );
}

// ============================================================
// Display utility
// ============================================================

void drawCenteredText(
  const String &text,
  int16_t centerX,
  int16_t y,
  uint8_t size,
  uint16_t color,
  uint16_t background = ILI9341_BLACK
) {
  int16_t x1;
  int16_t y1;
  uint16_t width;
  uint16_t height;

  tft.setTextSize(size);
  tft.setTextColor(color, background);

  tft.getTextBounds(
    text,
    0,
    y,
    &x1,
    &y1,
    &width,
    &height
  );

  tft.setCursor(
    centerX - static_cast<int16_t>(width / 2),
    y
  );

  tft.print(text);
}

void clearContentRegion(
  int16_t y,
  int16_t height
) {
  tft.fillRect(
    0,
    y,
    tft.width(),
    height,
    ILI9341_BLACK
  );
}

String formatStopwatchTime(uint32_t totalMs) {
  const uint32_t totalMinutes = totalMs / 60000;

  const uint32_t seconds =
    (totalMs % 60000) / 1000;

  const uint32_t centiseconds =
    (totalMs % 1000) / 10;

  char buffer[20];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02lu.%02lu",
    static_cast<unsigned long>(totalMinutes),
    static_cast<unsigned long>(seconds),
    static_cast<unsigned long>(centiseconds)
  );

  return String(buffer);
}

String formatRecordingTime(uint32_t totalMs) {
  const uint32_t seconds = totalMs / 1000;
  const uint32_t minutes = seconds / 60;

  const uint32_t remainingSeconds =
    seconds % 60;

  char buffer[12];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02lu:%02lu",
    static_cast<unsigned long>(minutes),
    static_cast<unsigned long>(remainingSeconds)
  );

  return String(buffer);
}

// ============================================================
// Wi-Fi / NTP
// ============================================================

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t startedAt = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS
  ) {
    delay(100);
  }

  wifiConnected =
    WiFi.status() == WL_CONNECTED;

  if (!wifiConnected) {
    logError("Wi-Fi connection failed");
    return false;
  }

  logEvent("Wi-Fi connected");
  return true;
}

bool synchronizeNetworkTime() {
  if (!wifiConnected) {
    return false;
  }

  configTzTime(
    TIME_ZONE,
    NTP_SERVER_1,
    NTP_SERVER_2
  );

  struct tm timeInfo {};
  const uint32_t startedAt = millis();

  while (
    millis() - startedAt <
    NTP_SYNC_TIMEOUT_MS
  ) {
    if (getLocalTime(&timeInfo, 250)) {
      timeSynchronized = true;
      logEvent("NTP synchronized");
      return true;
    }
  }

  timeSynchronized = false;
  logError("NTP synchronization failed");

  return false;
}

bool readLocalTime(struct tm &timeInfo) {
  if (getLocalTime(&timeInfo, 10)) {
    timeSynchronized = true;
    return true;
  }

  timeSynchronized = false;
  return false;
}

// ============================================================
// Button handling
// ============================================================

void initializeButton(Button &button) {
  pinMode(button.pin, INPUT_PULLUP);

  const bool initialState =
    digitalRead(button.pin);

  button.rawState = initialState;
  button.previousRawState = initialState;
  button.stableState = initialState;
}

void updateButton(Button &button) {
  button.pressedEvent = false;
  button.releasedEvent = false;
  button.shortPressEvent = false;
  button.longPressEvent = false;

  const uint32_t now = millis();

  button.rawState =
    digitalRead(button.pin);

  if (
    button.rawState !=
    button.previousRawState
  ) {
    button.previousRawState =
      button.rawState;

    button.lastChangeTime = now;
  }

  if (
    now - button.lastChangeTime >=
    BUTTON_DEBOUNCE_MS
  ) {
    if (
      button.rawState !=
      button.stableState
    ) {
      button.stableState =
        button.rawState;

      if (button.stableState == LOW) {
        button.pressedTime = now;
        button.pressedEvent = true;
        button.longPressHandled = false;
        button.suppressShortPress = false;
      } else {
        button.releasedEvent = true;

        if (
          !button.longPressHandled &&
          !button.suppressShortPress
        ) {
          button.shortPressEvent = true;
        }

        button.suppressShortPress = false;
      }
    }
  }

  if (
    button.stableState == LOW &&
    !button.longPressHandled &&
    now - button.pressedTime >=
    LONG_PRESS_MS
  ) {
    button.longPressHandled = true;
    button.longPressEvent = true;
  }
}

bool isPressed(const Button &button) {
  return button.stableState == LOW;
}

// ============================================================
// Stopwatch
// ============================================================

uint32_t getStopwatchTime() {
  if (!stopwatchRunning) {
    return stopwatchElapsedMs;
  }

  return (
    stopwatchElapsedMs +
    millis() -
    stopwatchStartedAt
  );
}

void toggleStopwatch() {
  if (!stopwatchRunning) {
    stopwatchStartedAt = millis();
    stopwatchRunning = true;

    logEvent("Stopwatch started");
  } else {
    stopwatchElapsedMs +=
      millis() - stopwatchStartedAt;

    stopwatchRunning = false;

    logEvent("Stopwatch stopped");
  }

  screenContentDirty = true;
  updateLedForCurrentState();
}

void resetStopwatch() {
  stopwatchRunning = false;
  stopwatchStartedAt = 0;
  stopwatchElapsedMs = 0;

  logEvent("Stopwatch reset");

  screenContentDirty = true;
  updateLedForCurrentState();
}

void handleStopwatchResetCombination() {
  if (
    currentScreen !=
    ScreenMode::STOPWATCH
  ) {
    comboActive = false;
    comboHandled = false;
    comboStartedAt = 0;
    return;
  }

  const bool leftPressed =
    isPressed(buttonLeft);

  const bool centerPressed =
    isPressed(buttonCenter);

  if (leftPressed && centerPressed) {
    if (!comboActive) {
      comboActive = true;
      comboHandled = false;
      comboStartedAt = millis();

      buttonLeft.suppressShortPress = true;
      buttonCenter.suppressShortPress = true;
    }

    if (
      !comboHandled &&
      millis() - comboStartedAt >=
      COMBO_HOLD_MS
    ) {
      comboHandled = true;
      resetStopwatch();
    }

    return;
  }

  if (
    comboActive &&
    !leftPressed &&
    !centerPressed
  ) {
    comboActive = false;
    comboHandled = false;
    comboStartedAt = 0;
  }
}

// ============================================================
// Recorder state
// ============================================================

uint32_t getRecordingTime() {
  if (!recording) {
    return 0;
  }

  return millis() - recordingStartedAt;
}

void startRecording() {
  if (recording) {
    return;
  }

  recording = true;
  recordingStartedAt = millis();

  indicatedPlaySlot = -1;
  playIndicatorUntil = 0;

  logEvent("Recording started");

  screenContentDirty = true;
  updateLedForCurrentState();

  // 실제 I2S 녹음 시작 코드는
  // 추후 이 위치에 연결
}

void stopRecording(bool automaticStop) {
  if (!recording) {
    return;
  }

  const uint32_t duration =
    millis() - recordingStartedAt;

  recording = false;
  recordingStartedAt = 0;

  const uint8_t savedSlot =
    nextWriteSlot;

  if (!recordingSlots[savedSlot].exists) {
    recordingSlots[savedSlot].exists = true;

    if (
      savedFileCount <
      MAX_RECORDING_FILES
    ) {
      savedFileCount++;
    }
  }

  recordingSlots[savedSlot].durationMs =
    duration;

  nextWriteSlot =
    (nextWriteSlot + 1) %
    MAX_RECORDING_FILES;

  if (automaticStop) {
    beepRecordingTimeout();
    logEvent(
      "Recording automatically stopped"
    );
  } else {
    logEvent("Recording stopped");
  }

  screenContentDirty = true;
  updateLedForCurrentState();

  // 실제 I2S 녹음 종료 및 WAV 저장 코드는
  // 추후 이 위치에 연결
}

void playNextRecording() {
  if (
    recording ||
    savedFileCount == 0
  ) {
    return;
  }

  for (
    uint8_t checked = 0;
    checked < MAX_RECORDING_FILES;
    checked++
  ) {
    const uint8_t slot =
      nextPlaySlot;

    nextPlaySlot =
      (nextPlaySlot + 1) %
      MAX_RECORDING_FILES;

    if (!recordingSlots[slot].exists) {
      continue;
    }

    indicatedPlaySlot = slot;

    playIndicatorUntil =
      millis() + PLAY_INDICATOR_MS;

    logEvent(
      "Recording playback selected"
    );

    screenContentDirty = true;
    updateLedForCurrentState();

    // 실제 WAV 재생 코드는
    // 추후 이 위치에 연결
    return;
  }
}

void handleAutomaticRecordingStop() {
  if (
    recording &&
    millis() - recordingStartedAt >=
    MAX_RECORDING_TIME_MS
  ) {
    stopRecording(true);
  }
}

void updatePlaybackIndicator() {
  if (indicatedPlaySlot < 0) {
    return;
  }

  if (
    static_cast<int32_t>(
      millis() - playIndicatorUntil
    ) >= 0
  ) {
    indicatedPlaySlot = -1;
    playIndicatorUntil = 0;

    screenContentDirty = true;
    updateLedForCurrentState();
  }
}

// ============================================================
// Screen layout
// ============================================================

void drawHeader(
  const __FlashStringHelper *title
) {
  tft.fillRect(
    0,
    0,
    tft.width(),
    32,
    ILI9341_DARKCYAN
  );

  tft.setTextSize(2);

  tft.setTextColor(
    ILI9341_WHITE,
    ILI9341_DARKCYAN
  );

  tft.setCursor(10, 8);
  tft.print(title);
}

void drawClockLayout() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader(F("CLOCK"));

  drawCenteredText(
    F("RIGHT: CHANGE MODE"),
    tft.width() / 2,
    220,
    1,
    ILI9341_LIGHTGREY
  );
}

void drawStopwatchLayout() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader(F("STOPWATCH"));

  drawCenteredText(
    F("LEFT: START / STOP"),
    tft.width() / 2,
    205,
    1,
    ILI9341_LIGHTGREY
  );

  drawCenteredText(
    F("LEFT + CENTER: RESET"),
    tft.width() / 2,
    220,
    1,
    ILI9341_LIGHTGREY
  );
}

void drawRecorderLayout() {
  tft.fillScreen(ILI9341_BLACK);
  drawHeader(F("RECORDER"));

  drawCenteredText(
    F("CENTER HOLD: RECORD"),
    tft.width() / 2,
    195,
    1,
    ILI9341_LIGHTGREY
  );

  drawCenteredText(
    F("CENTER TAP: STOP / PLAY"),
    tft.width() / 2,
    210,
    1,
    ILI9341_LIGHTGREY
  );

  drawCenteredText(
    F("RIGHT: CHANGE MODE"),
    tft.width() / 2,
    225,
    1,
    ILI9341_LIGHTGREY
  );
}

void drawCurrentLayout() {
  switch (currentScreen) {
    case ScreenMode::CLOCK:
      drawClockLayout();
      break;

    case ScreenMode::STOPWATCH:
      drawStopwatchLayout();
      break;

    case ScreenMode::RECORDER:
      drawRecorderLayout();
      break;
  }

  screenLayoutDirty = false;
  screenContentDirty = true;
}

// ============================================================
// Dynamic screen contents
// ============================================================

void drawClockContent() {
  struct tm now {};

  const bool validTime =
    readLocalTime(now);

  char timeText[12];
  char dateText[20];

  if (validTime) {
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
  } else {
    snprintf(
      timeText,
      sizeof(timeText),
      "--:--:--"
    );

    snprintf(
      dateText,
      sizeof(dateText),
      "TIME NOT SYNCED"
    );
  }

  clearContentRegion(45, 140);

  drawCenteredText(
    timeText,
    tft.width() / 2,
    65,
    5,
    validTime
      ? ILI9341_WHITE
      : ILI9341_RED
  );

  drawCenteredText(
    dateText,
    tft.width() / 2,
    125,
    2,
    validTime
      ? ILI9341_CYAN
      : ILI9341_RED
  );

  drawCenteredText(
    wifiConnected
      ? F("WIFI: CONNECTED")
      : F("WIFI: DISCONNECTED"),
    tft.width() / 2,
    165,
    1,
    wifiConnected
      ? ILI9341_GREEN
      : ILI9341_RED
  );

  updateLedForCurrentState();
}

void drawStopwatchContent() {
  clearContentRegion(45, 135);

  drawCenteredText(
    formatStopwatchTime(
      getStopwatchTime()
    ),
    tft.width() / 2,
    75,
    5,
    stopwatchRunning
      ? ILI9341_GREEN
      : ILI9341_WHITE
  );

  drawCenteredText(
    stopwatchRunning
      ? F("RUNNING")
      : F("STOPPED"),
    tft.width() / 2,
    145,
    2,
    stopwatchRunning
      ? ILI9341_GREEN
      : ILI9341_RED
  );
}

void drawRecorderSlotStatus() {
  constexpr int16_t startX = 38;
  constexpr int16_t startY = 160;
  constexpr int16_t slotWidth = 55;
  constexpr int16_t slotHeight = 22;
  constexpr int16_t slotGap = 8;

  for (
    uint8_t index = 0;
    index < MAX_RECORDING_FILES;
    index++
  ) {
    const int16_t x =
      startX +
      index * (slotWidth + slotGap);

    uint16_t borderColor =
      recordingSlots[index].exists
        ? ILI9341_GREEN
        : ILI9341_DARKGREY;

    if (
      recording &&
      index == nextWriteSlot
    ) {
      borderColor = ILI9341_WHITE;
    }

    if (indicatedPlaySlot == index) {
      borderColor = ILI9341_BLUE;
    }

    tft.drawRect(
      x,
      startY,
      slotWidth,
      slotHeight,
      borderColor
    );

    char slotText[8];

    snprintf(
      slotText,
      sizeof(slotText),
      "%u:%c",
      index + 1,
      recordingSlots[index].exists
        ? 'O'
        : '-'
    );

    drawCenteredText(
      slotText,
      x + slotWidth / 2,
      startY + 7,
      1,
      borderColor
    );
  }
}

void drawRecorderContent() {
  clearContentRegion(40, 150);

  if (recording) {
    drawCenteredText(
      F("RECORDING"),
      tft.width() / 2,
      50,
      3,
      ILI9341_RED
    );

    drawCenteredText(
      formatRecordingTime(
        getRecordingTime()
      ),
      tft.width() / 2,
      92,
      4,
      ILI9341_WHITE
    );

    char slotText[24];

    snprintf(
      slotText,
      sizeof(slotText),
      "SLOT %u / %u",
      nextWriteSlot + 1,
      MAX_RECORDING_FILES
    );

    drawCenteredText(
      slotText,
      tft.width() / 2,
      140,
      1,
      ILI9341_CYAN
    );
  } else if (
    indicatedPlaySlot >= 0
  ) {
    char title[24];

    snprintf(
      title,
      sizeof(title),
      "PLAYING SLOT %d",
      indicatedPlaySlot + 1
    );

    drawCenteredText(
      title,
      tft.width() / 2,
      55,
      2,
      ILI9341_BLUE
    );

    drawCenteredText(
      formatRecordingTime(
        recordingSlots[
          indicatedPlaySlot
        ].durationMs
      ),
      tft.width() / 2,
      95,
      3,
      ILI9341_WHITE
    );

    drawCenteredText(
      F("AUDIO OUTPUT DISABLED"),
      tft.width() / 2,
      135,
      1,
      ILI9341_LIGHTGREY
    );
  } else if (
    savedFileCount == 0
  ) {
    drawCenteredText(
      F("NO RECORDINGS"),
      tft.width() / 2,
      55,
      2,
      ILI9341_LIGHTGREY
    );

    drawCenteredText(
      F("HOLD CENTER FOR 2 SEC"),
      tft.width() / 2,
      100,
      1,
      ILI9341_CYAN
    );

    drawCenteredText(
      sdAvailable
        ? F("SD: READY")
        : F("SD: NOT AVAILABLE"),
      tft.width() / 2,
      130,
      1,
      sdAvailable
        ? ILI9341_GREEN
        : ILI9341_RED
    );
  } else {
    char savedText[24];

    snprintf(
      savedText,
      sizeof(savedText),
      "SAVED: %u / %u",
      savedFileCount,
      MAX_RECORDING_FILES
    );

    drawCenteredText(
      F("READY"),
      tft.width() / 2,
      50,
      3,
      ILI9341_GREEN
    );

    drawCenteredText(
      savedText,
      tft.width() / 2,
      95,
      2,
      ILI9341_WHITE
    );

    char nextSlotText[24];

    snprintf(
      nextSlotText,
      sizeof(nextSlotText),
      "NEXT SLOT: %u",
      nextWriteSlot + 1
    );

    drawCenteredText(
      nextSlotText,
      tft.width() / 2,
      130,
      1,
      ILI9341_CYAN
    );
  }

  drawRecorderSlotStatus();
}

void updateScreen(bool force = false) {
  if (screenLayoutDirty) {
    drawCurrentLayout();
    force = true;
  }

  uint32_t updateInterval =
    CLOCK_UPDATE_MS;

  switch (currentScreen) {
    case ScreenMode::CLOCK:
      updateInterval =
        CLOCK_UPDATE_MS;
      break;

    case ScreenMode::STOPWATCH:
      updateInterval =
        stopwatchRunning
          ? STOPWATCH_UPDATE_MS
          : 1000;
      break;

    case ScreenMode::RECORDER:
      updateInterval =
        recording
          ? RECORDER_UPDATE_MS
          : 1000;
      break;
  }

  if (
    !force &&
    !screenContentDirty &&
    millis() - lastScreenUpdate <
    updateInterval
  ) {
    return;
  }

  lastScreenUpdate = millis();
  screenContentDirty = false;

  switch (currentScreen) {
    case ScreenMode::CLOCK:
      drawClockContent();
      break;

    case ScreenMode::STOPWATCH:
      drawStopwatchContent();
      break;

    case ScreenMode::RECORDER:
      drawRecorderContent();
      break;
  }
}

// ============================================================
// Input handling
// ============================================================

void changeScreen() {
  const uint8_t nextScreen =
    (
      static_cast<uint8_t>(
        currentScreen
      ) + 1
    ) % 3;

  currentScreen =
    static_cast<ScreenMode>(
      nextScreen
    );

  comboActive = false;
  comboHandled = false;
  comboStartedAt = 0;

  screenLayoutDirty = true;
  screenContentDirty = true;

  updateLedForCurrentState();
}

void handleButtonEvents() {
  handleStopwatchResetCombination();

  if (buttonRight.shortPressEvent) {
    changeScreen();
    return;
  }

  if (comboActive) {
    return;
  }

  switch (currentScreen) {
    case ScreenMode::CLOCK:
      break;

    case ScreenMode::STOPWATCH:
      if (buttonLeft.shortPressEvent) {
        toggleStopwatch();
      }
      break;

    case ScreenMode::RECORDER:
      if (
        buttonCenter.longPressEvent &&
        !recording
      ) {
        startRecording();
        return;
      }

      if (
        buttonCenter.shortPressEvent
      ) {
        if (recording) {
          stopRecording(false);
        } else {
          playNextRecording();
        }
      }
      break;
  }
}

// ============================================================
// Initialization
// ============================================================

void initializeDisplay() {
  tft.begin();

  // 가로 방향 320 x 240
  tft.setRotation(1);

  tft.fillScreen(ILI9341_BLACK);

  drawCenteredText(
    F("VOICE CLOCK"),
    tft.width() / 2,
    80,
    3,
    ILI9341_WHITE
  );

  drawCenteredText(
    F("STARTING..."),
    tft.width() / 2,
    125,
    2,
    ILI9341_CYAN
  );
}

bool initializeStorage() {
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);

  digitalWrite(
    PIN_TFT_CS,
    HIGH
  );

  digitalWrite(
    PIN_SD_CS,
    HIGH
  );

  if (!SD.begin(PIN_SD_CS, SPI)) {
    logError("SD card not detected");
    return false;
  }

  logEvent("SD card initialized");
  return true;
}

// ============================================================
// Arduino setup / loop
// ============================================================

void setup() {
  Serial.begin(115200);

  initializeButton(buttonLeft);
  initializeButton(buttonCenter);
  initializeButton(buttonRight);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  // Active Low LED는 HIGH가 OFF이므로
  // 핀 설정 직후 먼저 모두 꺼준다.
  digitalWrite(PIN_LED_R, LED_OFF);
  digitalWrite(PIN_LED_G, LED_OFF);
  digitalWrite(PIN_LED_B, LED_OFF);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  setLed(LedState::OFF);

  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);

  digitalWrite(
    PIN_TFT_CS,
    HIGH
  );

  digitalWrite(
    PIN_SD_CS,
    HIGH
  );

  SPI.begin(
    PIN_SPI_SCK,
    PIN_SPI_MISO,
    PIN_SPI_MOSI
  );

  initializeDisplay();

  sdAvailable =
    initializeStorage();

  wifiConnected =
    connectWiFi();

  if (wifiConnected) {
    synchronizeNetworkTime();
  }

  updateLedForCurrentState();

  screenLayoutDirty = true;
  screenContentDirty = true;

  updateScreen(true);
}

void loop() {
  updateButton(buttonLeft);
  updateButton(buttonCenter);
  updateButton(buttonRight);

  handleButtonEvents();
  handleAutomaticRecordingStop();
  updatePlaybackIndicator();
  updateScreen();

  delay(1);
}