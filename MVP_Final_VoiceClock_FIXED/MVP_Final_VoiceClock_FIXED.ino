#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <driver/i2s.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
// ============================================================
// Wi-Fi / NTP
// ============================================================

constexpr char DEVICE_HOSTNAME[] = "voiceclock";

constexpr char SETUP_AP_SSID[] = "VoiceClock-Setup";
constexpr char SETUP_AP_PASSWORD[] = "12345678";

constexpr char PREFERENCES_NAMESPACE[] = "voiceclock";

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
// External common-VCC RGB LED pins (temporarily disabled)
// constexpr int PIN_LED_R = 37;
// constexpr int PIN_LED_G = 38;
// constexpr int PIN_LED_B = 39;

// ESP32-S3 onboard addressable RGB LED (WS2812 compatible)
constexpr int PIN_ONBOARD_RGB = 38;

// Buzzer
constexpr int PIN_BUZZER = 10;

// I2S audio
constexpr int PIN_AMP_BCLK = 11;
constexpr int PIN_AMP_LRC  = 12;
constexpr int PIN_AMP_DIN  = 14;

constexpr int PIN_MIC_SCK = 35;
constexpr int PIN_MIC_WS  = 36;
constexpr int PIN_MIC_SD  = 42;

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

// microSD storage
constexpr uint32_t SD_FLUSH_INTERVAL_MS = 1000;
constexpr char SD_TEST_PATH[] = "/.sdtest.tmp";

constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr uint16_t AUDIO_BITS_PER_SAMPLE = 16;
constexpr i2s_port_t I2S_PORT_AMP = I2S_NUM_0;
constexpr i2s_port_t I2S_PORT_MIC = I2S_NUM_1;

// INMP441 L/R: GND = LEFT, 3.3V = RIGHT
constexpr bool MIC_USE_LEFT_CHANNEL = true;

// Software audio gain. Each +1 shift approximately doubles amplitude.
// MIC: 3 = x8 sensitivity, PLAYBACK: 1 = x2 speaker PCM level.
// Lower these values if clipping/distortion occurs.
constexpr uint8_t MIC_GAIN_SHIFT = 3;
constexpr uint8_t PLAYBACK_GAIN_SHIFT = 1;
constexpr size_t AUDIO_FRAME_SAMPLES = 256;

constexpr bool ENABLE_EVENT_LOG = false;

// ============================================================
// Display
// ============================================================

Adafruit_ILI9341 tft(
  PIN_TFT_CS,
  PIN_TFT_DC,
  PIN_TFT_RST
);
WebServer server(80);
// ============================================================
// Types
// ============================================================
Preferences preferences;
DNSServer dnsServer;
// ============================================================
// Function prototypes
// ============================================================

void handleWifiPage();
void handleWifiPage();
void handleRecordingsPage();
void handleAudioFile();

const char *recordingPath(uint8_t slot);

void startMdnsIfNeeded();

constexpr uint16_t DNS_PORT = 53;
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
void setLed(LedState state);
void updateLedForCurrentState();


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
bool setupAccessPointActive = false;
bool webServerStarted = false;
bool mdnsStarted = false;

String savedWifiSsid;
String savedWifiPassword;

uint32_t lastWifiReconnectAttempt = 0;
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

bool playback = false;
int8_t playbackSlot = -1;

File recordingFile;
File playbackFile;
uint32_t recordingDataBytes = 0;
uint32_t playbackDataRemaining = 0;
bool audioAvailable = false;
bool storageWriteError = false;
uint32_t lastRecordingFlushAt = 0;

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

  // ESP32-S3 onboard addressable RGB LED.
  // rgbLedWrite() drives the WS2812-compatible LED on a single GPIO.
  rgbLedWrite(
    PIN_ONBOARD_RGB,
    red ? 255 : 0,
    green ? 255 : 0,
    blue ? 255 : 0
  );
}

void updateLedForCurrentState() {
  if (currentScreen == ScreenMode::RECORDER) {
    if (recording) {
      setLed(LedState::WHITE);
      return;
    }

    if (
      playback &&
      indicatedPlaySlot >= 0
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

// ============================================================
// Voice Clock Wi-Fi / Web Server
// ============================================================

void loadSavedWifiCredentials() {
  preferences.begin(PREFERENCES_NAMESPACE, true);

  savedWifiSsid =
    preferences.getString(
      "wifi_ssid",
      ""
    );

  savedWifiPassword =
    preferences.getString(
      "wifi_pass",
      ""
    );

  preferences.end();
}


void saveWifiCredentials(
  const String &ssid,
  const String &password
) {
  preferences.begin(
    PREFERENCES_NAMESPACE,
    false
  );

  preferences.putString(
    "wifi_ssid",
    ssid
  );

  preferences.putString(
    "wifi_pass",
    password
  );

  preferences.end();

  savedWifiSsid = ssid;
  savedWifiPassword = password;
}


bool connectWiFiStation(
  const String &ssid,
  const String &password
) {
  if (ssid.isEmpty()) {
    return false;
  }

  // 사용자 공유기에 연결할 때는 STA 전용
  WiFi.mode(WIFI_STA);

  WiFi.setHostname(DEVICE_HOSTNAME);

  WiFi.begin(
    ssid.c_str(),
    password.c_str()
  );

  uint32_t startedAt = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS
  ) {
    delay(100);
  }

  wifiConnected =
    WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    Serial.println("[OK] Wi-Fi connected");
    Serial.print("[OK] STA IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("[OK] Gateway: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("[OK] Subnet: ");
    Serial.println(WiFi.subnetMask());
  }

  return wifiConnected;
}

void startSetupAccessPoint() {

  WiFi.mode(WIFI_AP_STA);

  WiFi.setHostname(
    DEVICE_HOSTNAME
  );

  setupAccessPointActive =
    WiFi.softAP(
      SETUP_AP_SSID,
      SETUP_AP_PASSWORD
    );

  if (setupAccessPointActive) {

    IPAddress apIP =
      WiFi.softAPIP();
dnsServer.start(
  DNS_PORT,
  "*",
  apIP
);
    Serial.print(
      "Setup AP IP: "
    );

    Serial.println(apIP);
  }
}
void stopSetupAccessPoint() {
  if (!setupAccessPointActive) {
    return;
  }

  dnsServer.stop();

  WiFi.softAPdisconnect(true);

  setupAccessPointActive = false;

  // 실제 공유기/핫스팟 연결만 유지
  WiFi.mode(WIFI_STA);

  Serial.println("[OK] Setup AP stopped");
}
bool synchronizeNetworkTime();

void initializeNetwork() {
  loadSavedWifiCredentials();

  wifiConnected = false;

  // AP + STA 동시 사용
  WiFi.mode(WIFI_AP_STA);

  WiFi.setHostname(DEVICE_HOSTNAME);

  // 무조건 설정 AP 생성
  startSetupAccessPoint();

  Serial.println();
  Serial.println("============================");
  Serial.println("Voice Clock Setup Mode");
  Serial.println("Wi-Fi: VoiceClock-Setup");
  Serial.println("Open: http://192.168.4.1");
  Serial.println("============================");
}
void handleWifiConnectedPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Voice Clock</title>
<style>
body {
  margin: 0;
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #f5f7fb;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  color: #172033;
}
.card {
  width: calc(100% - 40px);
  max-width: 420px;
  background: white;
  border-radius: 24px;
  padding: 34px 26px;
  text-align: center;
  box-shadow: 0 10px 35px rgba(0,0,0,0.08);
}
a {
  display: block;
  margin-top: 24px;
  padding: 16px;
  background: #526ff5;
  color: white;
  text-decoration: none;
  border-radius: 14px;
  font-weight: 700;
}
</style>
</head>
<body>
<div class="card">
  <h2>Wi-Fi 연결 완료</h2>
  <p>
    Voice Clock이 인터넷용 Wi-Fi에 연결되었습니다.<br>
    현재 기기는 VoiceClock-Setup에 계속 연결해 두세요.
  </p>
  <a href="http://192.168.4.1/recordings">녹음 페이지 열기</a>
</div>
</body>
</html>
)rawliteral";

  server.send(
    200,
    "text/html; charset=utf-8",
    html
  );
}
void handleWifiPage() {

  String html;

  html +=
    "<!doctype html>"
    "<html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<title>Voice Clock Wi-Fi</title>"
    "</head><body>";

  html +=
    "<h2>Voice Clock Wi-Fi Setup</h2>";

  html +=
    "<form method='POST' "
    "action='/wifi/save'>";

  html +=
    "<label>Wi-Fi</label><br>";

  html +=
    "<input name='ssid' "
    "maxlength='32' required><br><br>";

  html +=
    "<label>Password</label><br>";

  html +=
    "<input name='password' "
    "type='password' "
    "maxlength='64'><br><br>";

  html +=
    "<button type='submit'>"
    "Connect"
    "</button>";

  html +=
    "</form></body></html>";

  server.send(
    200,
    "text/html; charset=utf-8",
    html
  );
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.isEmpty()) {
    server.send(
      400,
      "text/plain; charset=utf-8",
      "SSID가 비어 있습니다."
    );
    return;
  }

  Serial.println();
  Serial.println("===== Wi-Fi Connection Attempt =====");
  Serial.print("SSID: ");
  Serial.println(ssid);

  // 설정용 AP는 계속 유지하고, STA만 사용자 Wi-Fi에 연결한다.
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false);
  delay(300);

  WiFi.begin(
    ssid.c_str(),
    password.c_str()
  );

  const uint32_t startedAt = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS
  ) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;

    saveWifiCredentials(
      ssid,
      password
    );

    Serial.println(">>> WIFI CONNECTION SUCCESS <<<");
    Serial.print("[OK] SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("[OK] STA IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[OK] Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("[OK] Subnet: ");
    Serial.println(WiFi.subnetMask());
    Serial.print("[OK] RSSI: ");
    Serial.println(WiFi.RSSI());
    Serial.print("[OK] Setup AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("[OK] Web page: http://192.168.4.1/recordings");

    // 사용자 Wi-Fi는 인터넷/NTP용으로 사용한다.
    configTzTime(
      TIME_ZONE,
      NTP_SERVER_1,
      NTP_SERVER_2
    );

    screenContentDirty = true;
    updateLedForCurrentState();

    String html =
      "<!doctype html>"
      "<html lang='ko'>"
      "<head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Voice Clock</title>"
      "<style>"
      "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f5f7fb;margin:0;padding:40px 20px;color:#172033;}"
      ".card{max-width:420px;margin:auto;background:white;padding:30px;border-radius:20px;box-shadow:0 10px 30px rgba(0,0,0,0.08);text-align:center;}"
      ".success{font-size:42px;margin-bottom:10px;}"
      ".info{background:#f0f3f8;padding:12px;border-radius:10px;word-break:break-all;}"
      "a{display:block;margin-top:20px;padding:14px;background:#526ff5;color:white;text-decoration:none;border-radius:12px;font-weight:700;}"
      "</style>"
      "</head>"
      "<body>"
      "<div class='card'>"
      "<div class='success'>✓</div>"
      "<h2>Wi-Fi 연결 성공</h2>"
      "<p>Voice Clock이 인터넷용 Wi-Fi에 연결되었습니다.</p>"
      "<div class='info'><b>SSID</b><br>" + WiFi.SSID() + "</div>"
      "<p>현재 기기는 <b>VoiceClock-Setup</b>에 계속 연결해 두세요.</p>"
      "<p>웹페이지는 항상 아래 주소로 접속합니다.</p>"
      "<div class='info'>http://192.168.4.1</div>"
      "<a href='http://192.168.4.1/recordings'>녹음 페이지 열기</a>"
      "</div>"
      "</body>"
      "</html>";

    server.send(
      200,
      "text/html; charset=utf-8",
      html
    );

    return;
  }

  wifiConnected = false;
  screenContentDirty = true;
  updateLedForCurrentState();

  Serial.println(">>> WIFI CONNECTION FAILED <<<");

  String html =
    "<!doctype html>"
    "<html lang='ko'>"
    "<head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Voice Clock</title>"
    "</head>"
    "<body>"
    "<h2>Wi-Fi 연결 실패</h2>"
    "<p>SSID 또는 비밀번호를 확인해주세요.</p>"
    "<p><a href='/wifi'>다시 설정</a></p>"
    "</body>"
    "</html>";

  server.send(
    200,
    "text/html; charset=utf-8",
    html
  );
}
void startMdnsIfNeeded() {

  if (!wifiConnected) {
    return;
  }

  if (mdnsStarted) {
    return;
  }

  if (MDNS.begin(DEVICE_HOSTNAME)) {

    MDNS.addService("http", "tcp", 80);

    mdnsStarted = true;

    Serial.println("mDNS started");
    Serial.println("http://voiceclock.local");
  }
  else {
    Serial.println("[ERR] mDNS start failed");
  }
}
void initializeWebServer() {
  server.on(
  "/hotspot-detect.html",
  HTTP_GET,
  handleWifiPage
);

server.on(
  "/library/test/success.html",
  HTTP_GET,
  handleWifiPage
);

server.on(
  "/generate_204",
  HTTP_GET,
  handleWifiPage
);

server.on(
  "/gen_204",
  HTTP_GET,
  handleWifiPage
);

server.on(
  "/connecttest.txt",
  HTTP_GET,
  handleWifiPage
);

server.on(
  "/ncsi.txt",
  HTTP_GET,
  handleWifiPage
);
  server.on(
    "/",
    HTTP_GET,
    []() {
      if (!wifiConnected) {
        handleWifiPage();
      } else {
        handleRecordingsPage();
      }
    }
  );

  server.on(
    "/recordings",
    HTTP_GET,
    handleRecordingsPage
  );

  server.on(
    "/audio",
    HTTP_GET,
    handleAudioFile
  );

  server.on(
    "/wifi",
    HTTP_GET,
    handleWifiPage
  );

  server.on(
    "/wifi/save",
    HTTP_POST,
    handleWifiSave
  );

  server.onNotFound([]() {

  if (!wifiConnected) {

    server.sendHeader(
      "Location",
      "http://192.168.4.1/wifi",
      true
    );

    server.send(
      302,
      "text/plain",
      ""
    );

  } else {

    server.send(
      404,
      "text/plain",
      "Not found"
    );
  }
});

  server.begin();
  webServerStarted = true;

  Serial.println("[OK] Voice Clock web server started");
  Serial.println("[OK] Open http://192.168.4.1");
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
// I2S audio / WAV
// ============================================================

const char *recordingPath(uint8_t slot) {
  static const char *paths[MAX_RECORDING_FILES] = {
    "/REC1.WAV", "/REC2.WAV", "/REC3.WAV", "/REC4.WAV"
  };
  return paths[slot % MAX_RECORDING_FILES];
}
void handleAudioFile() {

  if (!server.hasArg("slot")) {
    server.send(
      400,
      "text/plain",
      "Missing slot"
    );
    return;
  }

  int slot =
    server.arg("slot").toInt();

  if (
    slot < 1 ||
    slot > MAX_RECORDING_FILES
  ) {
    server.send(
      400,
      "text/plain",
      "Invalid slot"
    );
    return;
  }
  // 녹음 중 SD 파일을 동시에 읽지 않도록 방지
  if (recording) {
    server.send(
      409,
      "text/plain",
      "Recording in progress"
    );
    return;
  }

  const char *path =
    recordingPath(slot - 1);

  if (!SD.exists(path)) {
    server.send(
      404,
      "text/plain",
      "Recording not found"
    );
    return;
  }

  File file =
    SD.open(
      path,
      FILE_READ
    );

  if (!file) {
    server.send(
      500,
      "text/plain",
      "Unable to open recording"
    );
    return;
  }

  server.sendHeader(
    "Cache-Control",
    "no-store"
  );

  server.streamFile(
    file,
    "audio/wav"
  );

  file.close();
}


void handleRecordingsPage() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ko">

<head>

<meta charset="UTF-8">

<meta
  name="viewport"
  content="width=device-width, initial-scale=1.0"
>

<title>Voice Clock</title>

<style>

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: #f5f7fb;
  color: #172033;

  font-family:
    -apple-system,
    BlinkMacSystemFont,
    "Segoe UI",
    sans-serif;
}

header {
  padding: 20px 7%;

  display: flex;
  align-items: center;
  justify-content: space-between;

  background: white;

  border-bottom: 1px solid #e8ebf2;
}

.logo {
  font-size: 18px;
  font-weight: 800;
}

.connected {
  padding: 7px 12px;

  background: #effaf5;

  color: #169c68;

  border-radius: 20px;

  font-size: 11px;
  font-weight: 700;
}

main {
  max-width: 1000px;

  margin: auto;

  padding: 45px 20px 70px;
}

.eyebrow {
  color: #526ff5;

  font-size: 12px;
  font-weight: 800;

  letter-spacing: 1.2px;
}

h1 {
  margin: 8px 0;

  font-size: 34px;
}

.description {
  margin-bottom: 32px;

  color: #7b8497;

  line-height: 1.7;
}

.grid {
  display: grid;

  grid-template-columns:
    repeat(2, 1fr);

  gap: 20px;
}

.card {
  padding: 24px;

  background: white;

  border:
    1px solid #e7eaf0;

  border-radius: 20px;

  box-shadow:
    0 8px 30px
    rgba(23, 32, 51, 0.04);
}

.cardHeader {
  display: flex;

  align-items: center;
  justify-content: space-between;

  margin-bottom: 18px;
}

.titleBox {
  display: flex;

  align-items: center;

  gap: 11px;
}

.icon {
  width: 40px;
  height: 40px;

  display: flex;
  align-items: center;
  justify-content: center;

  background: #eef2ff;

  border-radius: 12px;

  font-size: 19px;
}

.recordTitle {
  font-size: 14px;

  font-weight: 800;
}

.fileName {
  margin-top: 3px;

  color: #8b93a5;

  font-size: 11px;
}

.badge {
  padding: 6px 10px;

  border-radius: 20px;

  background: #eef2ff;

  color: #526ff5;

  font-size: 10px;
  font-weight: 800;
}

audio {
  width: 100%;

  height: 42px;
}

.refreshButton {
  display: block;

  margin: 30px auto 0;

  padding: 12px 22px;

  border: none;

  border-radius: 12px;

  background: #526ff5;

  color: white;

  font-size: 13px;
  font-weight: 700;

  cursor: pointer;
}

footer {
  padding: 25px;

  color: #a0a6b2;

  text-align: center;

  font-size: 11px;
}

@media (
  max-width: 700px
) {

  .grid {
    grid-template-columns: 1fr;
  }

  h1 {
    font-size: 29px;
  }

}

</style>

</head>


<body>

<header>

  <div class="logo">
    🎙️ VOICE CLOCK
  </div>

  <div class="connected">
    ● DEVICE CONNECTED
  </div>

</header>


<main>

  <div class="eyebrow">
    RECORDING LIBRARY
  </div>

  <h1>
    Your Recordings
  </h1>

  <div class="description">
    Voice Clock에 저장된 녹음을
    스마트폰에서 바로 확인하고 재생할 수 있습니다.
  </div>


  <div class="grid">


    <div class="card">

      <div class="cardHeader">

        <div class="titleBox">

          <div class="icon">
            🎙️
          </div>

          <div>

            <div class="recordTitle">
              RECORD 01
            </div>

            <div class="fileName">
              REC1.WAV
            </div>

          </div>

        </div>

        <div class="badge">
          SLOT 1
        </div>

      </div>

      <audio
        controls
        preload="none"
        src="/audio?slot=1"
      >
      </audio>

    </div>


    <div class="card">

      <div class="cardHeader">

        <div class="titleBox">

          <div class="icon">
            🎙️
          </div>

          <div>

            <div class="recordTitle">
              RECORD 02
            </div>

            <div class="fileName">
              REC2.WAV
            </div>

          </div>

        </div>

        <div class="badge">
          SLOT 2
        </div>

      </div>

      <audio
        controls
        preload="none"
        src="/audio?slot=2"
      >
      </audio>

    </div>


    <div class="card">

      <div class="cardHeader">

        <div class="titleBox">

          <div class="icon">
            🎙️
          </div>

          <div>

            <div class="recordTitle">
              RECORD 03
            </div>

            <div class="fileName">
              REC3.WAV
            </div>

          </div>

        </div>

        <div class="badge">
          SLOT 3
        </div>

      </div>

      <audio
        controls
        preload="none"
        src="/audio?slot=3"
      >
      </audio>

    </div>


    <div class="card">

      <div class="cardHeader">

        <div class="titleBox">

          <div class="icon">
            🎙️
          </div>

          <div>

            <div class="recordTitle">
              RECORD 04
            </div>

            <div class="fileName">
              REC4.WAV
            </div>

          </div>

        </div>

        <div class="badge">
          SLOT 4
        </div>

      </div>

      <audio
        controls
        preload="none"
        src="/audio?slot=4"
      >
      </audio>

    </div>

  </div>


  <button
    class="refreshButton"
    onclick="location.reload()"
  >
    녹음 목록 새로고침
  </button>

</main>


<footer>
  VOICE CLOCK · Local Recording Library
</footer>


</body>
</html>
)rawliteral";

  server.sendHeader(
    "Cache-Control",
    "no-store"
  );

  server.send(
    200,
    "text/html; charset=utf-8",
    html
  );
}

void putLe16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
}

void putLe32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

uint16_t getLe16(const uint8_t *p) {
  return
    static_cast<uint16_t>(p[0]) |
    (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t getLe32(const uint8_t *p) {
  return
    static_cast<uint32_t>(p[0]) |
    (static_cast<uint32_t>(p[1]) << 8) |
    (static_cast<uint32_t>(p[2]) << 16) |
    (static_cast<uint32_t>(p[3]) << 24);
}

void makeWavHeader(uint8_t *h, uint32_t dataBytes) {
  memset(h, 0, 44);

  memcpy(h + 0, "RIFF", 4);
  putLe32(h + 4, dataBytes + 36);
  memcpy(h + 8, "WAVE", 4);

  memcpy(h + 12, "fmt ", 4);
  putLe32(h + 16, 16);
  putLe16(h + 20, 1);
  putLe16(h + 22, 1);
  putLe32(h + 24, AUDIO_SAMPLE_RATE);
  putLe32(h + 28, AUDIO_SAMPLE_RATE * 2);
  putLe16(h + 32, 2);
  putLe16(h + 34, AUDIO_BITS_PER_SAMPLE);

  memcpy(h + 36, "data", 4);
  putLe32(h + 40, dataBytes);
}

bool initMicI2s() {
  i2s_config_t cfg {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = MIC_USE_LEFT_CHANNEL
    ? I2S_CHANNEL_FMT_ONLY_LEFT
    : I2S_CHANNEL_FMT_ONLY_RIGHT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = AUDIO_FRAME_SAMPLES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  if (i2s_driver_install(I2S_PORT_MIC, &cfg, 0, nullptr) != ESP_OK) {
    logError("INMP441 I2S install failed");
    return false;
  }

  i2s_pin_config_t pins {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_MIC_SCK;
  pins.ws_io_num = PIN_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_MIC_SD;

  if (i2s_set_pin(I2S_PORT_MIC, &pins) != ESP_OK) {
    logError("INMP441 I2S pin setup failed");
    i2s_driver_uninstall(I2S_PORT_MIC);
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT_MIC);
  return true;
}

bool initAmpI2s() {
  i2s_config_t cfg {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = AUDIO_FRAME_SAMPLES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  if (i2s_driver_install(I2S_PORT_AMP, &cfg, 0, nullptr) != ESP_OK) {
    logError("MAX98357A I2S install failed");
    return false;
  }

  i2s_pin_config_t pins {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_AMP_BCLK;
  pins.ws_io_num = PIN_AMP_LRC;
  pins.data_out_num = PIN_AMP_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin(I2S_PORT_AMP, &pins) != ESP_OK) {
    logError("MAX98357A I2S pin setup failed");
    i2s_driver_uninstall(I2S_PORT_AMP);
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT_AMP);
  return true;
}

bool initializeAudio() {
  const bool micOk = initMicI2s();
  const bool ampOk = initAmpI2s();

  if (!micOk || !ampOk) {
    return false;
  }

  logEvent("I2S audio initialized");
  return true;
}

int16_t micToPcm16(int32_t raw) {
  int32_t sample = raw >> (16 - MIC_GAIN_SHIFT);

  if (sample > INT16_MAX) sample = INT16_MAX;
  if (sample < INT16_MIN) sample = INT16_MIN;

  return static_cast<int16_t>(sample);
}

int16_t applyPlaybackGain(int16_t sample) {
  int32_t amplified = static_cast<int32_t>(sample) << PLAYBACK_GAIN_SHIFT;

  if (amplified > INT16_MAX) amplified = INT16_MAX;
  if (amplified < INT16_MIN) amplified = INT16_MIN;

  return static_cast<int16_t>(amplified);
}

void loadRecordingsFromSd() {

  savedFileCount = 0;
  nextWriteSlot = 0;
  nextPlaySlot = 0;

  bool foundEmptySlot = false;

  for (
    uint8_t slot = 0;
    slot < MAX_RECORDING_FILES;
    slot++
  ) {

    recordingSlots[slot] = {};

    File f =
      SD.open(
        recordingPath(slot),
        FILE_READ
      );

    bool validRecording = false;

    if (f && f.size() >= 44) {

      uint8_t header[44];

      if (
        f.read(
          header,
          sizeof(header)
        ) == sizeof(header) &&

        memcmp(
          header,
          "RIFF",
          4
        ) == 0 &&

        memcmp(
          header + 8,
          "WAVE",
          4
        ) == 0 &&

        memcmp(
          header + 12,
          "fmt ",
          4
        ) == 0 &&

        memcmp(
          header + 36,
          "data",
          4
        ) == 0 &&

        getLe16(header + 20) == 1 &&
        getLe16(header + 22) == 1 &&
        getLe32(header + 24) ==
          AUDIO_SAMPLE_RATE &&
        getLe16(header + 34) ==
          AUDIO_BITS_PER_SAMPLE
      ) {

        uint32_t dataBytes =
          getLe32(header + 40);

        uint32_t maxDataBytes =
          static_cast<uint32_t>(
            f.size() - 44
          );

        if (
          dataBytes >
          maxDataBytes
        ) {
          dataBytes =
            maxDataBytes;
        }

        // 실제 음성 데이터가 있어야
        // 유효한 녹음으로 인정
        if (dataBytes > 0) {

          recordingSlots[slot].exists =
            true;

          recordingSlots[slot].durationMs =
            static_cast<uint32_t>(
              (
                static_cast<uint64_t>(
                  dataBytes
                ) * 1000ULL
              ) /
              (
                AUDIO_SAMPLE_RATE *
                2ULL
              )
            );

          savedFileCount++;

          validRecording = true;
        }
      }
    }

    if (f) {
      f.close();
    }

    // 최초의 유효하지 않은 슬롯을
    // 다음 녹음 위치로 설정
    if (
      !validRecording &&
      !foundEmptySlot
    ) {

      nextWriteSlot = slot;
      foundEmptySlot = true;
    }

    Serial.print("[SD] SLOT ");
    Serial.print(slot + 1);
    Serial.print(": ");

    if (validRecording) {
      Serial.println("VALID");
    } else {
      Serial.println("EMPTY / INVALID");
    }
  }

  // 4개 모두 차 있으면
  // 다음 녹음은 1번부터 덮어쓰기
  // 4개 모두 차 있으면
// 이전에 저장해 둔 다음 녹음 슬롯을 복원
if (!foundEmptySlot) {
  preferences.begin(PREFERENCES_NAMESPACE, true);

  nextWriteSlot = preferences.getUChar(
    "next_slot",
    0
  );

  preferences.end();

  if (nextWriteSlot >= MAX_RECORDING_FILES) {
    nextWriteSlot = 0;
  }
}

  Serial.print(
    "[SD] recordings found: "
  );
  Serial.println(savedFileCount);

  Serial.print(
    "[SD] next write slot: "
  );
  Serial.println(nextWriteSlot + 1);
}
void serviceRecordingAudio() {
  if (!recording || !recordingFile) {
    return;
  }

  int32_t raw[AUDIO_FRAME_SAMPLES];
  int16_t pcm[AUDIO_FRAME_SAMPLES];
  size_t bytesRead = 0;

  if (
    i2s_read(
      I2S_PORT_MIC,
      raw,
      sizeof(raw),
      &bytesRead,
      0
    ) != ESP_OK ||
    bytesRead == 0
  ) {
    return;
  }

  const size_t count = bytesRead / sizeof(int32_t);

  for (size_t i = 0; i < count; i++) {
    pcm[i] = micToPcm16(raw[i]);
  }

  const size_t bytesToWrite = count * sizeof(int16_t);
  const size_t bytesWritten = recordingFile.write(
    reinterpret_cast<const uint8_t *>(pcm),
    bytesToWrite
  );

  recordingDataBytes += static_cast<uint32_t>(bytesWritten);

  if (bytesWritten != bytesToWrite) {
    storageWriteError = true;
    logError("SD audio write failed");
    return;
  }

  const uint32_t now = millis();
  if (now - lastRecordingFlushAt >= SD_FLUSH_INTERVAL_MS) {
    recordingFile.flush();
    lastRecordingFlushAt = now;
  }
}

void finishPlayback() {
  if (playbackFile) {
    playbackFile.close();
  }

  playback = false;
  playbackSlot = -1;
  playbackDataRemaining = 0;
  indicatedPlaySlot = -1;
  playIndicatorUntil = 0;

  i2s_zero_dma_buffer(I2S_PORT_AMP);

  screenContentDirty = true;
  updateLedForCurrentState();
}

void stopPlayback() {
  if (!playback) {
    return;
  }

  finishPlayback();
  logEvent("Playback stopped");
}

void servicePlaybackAudio() {
  if (!playback || !playbackFile) {
    return;
  }

  if (playbackDataRemaining == 0) {
    finishPlayback();
    logEvent("Playback finished");
    return;
  }

  int16_t mono[AUDIO_FRAME_SAMPLES];
  int16_t stereo[AUDIO_FRAME_SAMPLES * 2];

  const size_t requested = min(
    static_cast<uint32_t>(sizeof(mono)),
    playbackDataRemaining
  );

  const size_t bytesRead = playbackFile.read(
    reinterpret_cast<uint8_t *>(mono),
    requested
  );

  if (bytesRead == 0) {
    finishPlayback();
    logError("WAV read failed");
    return;
  }

  const size_t count = bytesRead / sizeof(int16_t);

  for (size_t i = 0; i < count; i++) {
    const int16_t sample = applyPlaybackGain(mono[i]);
    stereo[i * 2] = sample;
    stereo[i * 2 + 1] = sample;
  }

  size_t bytesWritten = 0;

  if (
    i2s_write(
      I2S_PORT_AMP,
      stereo,
      count * 2 * sizeof(int16_t),
      &bytesWritten,
      pdMS_TO_TICKS(20)
    ) != ESP_OK
  ) {
    finishPlayback();
    logError("I2S playback failed");
    return;
  }

  playbackDataRemaining -= static_cast<uint32_t>(bytesRead);
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
  if (
    recording ||
    playback ||
    !sdAvailable ||
    !audioAvailable
  ) {
    return;
  }

  const uint8_t slot = nextWriteSlot;
  const char *path = recordingPath(slot);

  if (SD.exists(path)) {
    SD.remove(path);
  }

  recordingFile = SD.open(path, FILE_WRITE);

  if (!recordingFile) {
    logError("Unable to create WAV file");
    return;
  }

  uint8_t header[44];
  makeWavHeader(header, 0);

  if (
    recordingFile.write(header, sizeof(header)) !=
    sizeof(header)
  ) {
    recordingFile.close();
    SD.remove(path);
    logError("Unable to write WAV header");
    return;
  }

  recordingDataBytes = 0;
  storageWriteError = false;
  lastRecordingFlushAt = millis();
  i2s_zero_dma_buffer(I2S_PORT_MIC);

  recording = true;
  recordingStartedAt = millis();

  indicatedPlaySlot = -1;
  playIndicatorUntil = 0;

  logEvent("Recording started");
  screenContentDirty = true;
  updateLedForCurrentState();
}

void stopRecording(bool automaticStop) {
  if (!recording) {
    return;
  }

  for (uint8_t i = 0; i < 4; i++) {
    serviceRecordingAudio();
  }

  const uint32_t duration = millis() - recordingStartedAt;

  recording = false;
  recordingStartedAt = 0;

  const uint8_t savedSlot = nextWriteSlot;

  bool fileSaved = !storageWriteError && recordingDataBytes > 0;

  if (recordingFile) {
    if (fileSaved) {
      uint8_t header[44];
      makeWavHeader(header, recordingDataBytes);

      if (
        !recordingFile.seek(0) ||
        recordingFile.write(header, sizeof(header)) != sizeof(header)
      ) {
        fileSaved = false;
        logError("Unable to finalize WAV header");
      }

      recordingFile.flush();
    }

    recordingFile.close();
  }

  if (!fileSaved) {
    SD.remove(recordingPath(savedSlot));
    recordingSlots[savedSlot] = {};
    logError("Recording was not saved to SD");
  } else {
    if (!recordingSlots[savedSlot].exists) {
      recordingSlots[savedSlot].exists = true;
      if (savedFileCount < MAX_RECORDING_FILES) {
        savedFileCount++;
      }
    }

    // File length is authoritative for the saved duration.
    recordingSlots[savedSlot].durationMs =
      static_cast<uint32_t>(
        (static_cast<uint64_t>(recordingDataBytes) * 1000ULL) /
        (AUDIO_SAMPLE_RATE * 2ULL)
      );

    nextWriteSlot =
      (nextWriteSlot + 1) %
      MAX_RECORDING_FILES;
  }
preferences.begin(PREFERENCES_NAMESPACE, false);
preferences.putUChar("next_slot", nextWriteSlot);
preferences.end();
  recordingDataBytes = 0;
  storageWriteError = false;

  if (automaticStop) {
    beepRecordingTimeout();
    logEvent("Recording automatically stopped");
  } else {
    logEvent("Recording stopped");
  }

  screenContentDirty = true;
  updateLedForCurrentState();
}

bool beginPlayback(uint8_t slot) {
  if (
    recording ||
    playback ||
    !sdAvailable ||
    !audioAvailable ||
    !recordingSlots[slot].exists
  ) {
    return false;
  }

  playbackFile = SD.open(recordingPath(slot), FILE_READ);

  if (!playbackFile || playbackFile.size() < 44) {
    if (playbackFile) playbackFile.close();
    logError("Unable to open WAV file");
    return false;
  }

  uint8_t header[44];

  if (
    playbackFile.read(header, sizeof(header)) != sizeof(header) ||
    memcmp(header, "RIFF", 4) != 0 ||
    memcmp(header + 8, "WAVE", 4) != 0 ||
    memcmp(header + 12, "fmt ", 4) != 0 ||
    memcmp(header + 36, "data", 4) != 0 ||
    getLe16(header + 20) != 1 ||
    getLe16(header + 22) != 1 ||
    getLe32(header + 24) != AUDIO_SAMPLE_RATE ||
    getLe16(header + 34) != AUDIO_BITS_PER_SAMPLE
  ) {
    playbackFile.close();
    logError("Invalid WAV file");
    return false;
  }

  const uint32_t fileDataBytes =
    static_cast<uint32_t>(playbackFile.size() - 44);

  playbackDataRemaining = min(
    getLe32(header + 40),
    fileDataBytes
  );

  if (playbackDataRemaining == 0) {
    playbackFile.close();
    logError("WAV file is empty");
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT_AMP);

  playback = true;
  playbackSlot = slot;
  indicatedPlaySlot = slot;
  playIndicatorUntil = 0;

  logEvent("Recording playback started");
  screenContentDirty = true;
  updateLedForCurrentState();

  return true;
}

void playNextRecording() {
  if (
    recording ||
    playback ||
    savedFileCount == 0
  ) {
    return;
  }

  for (
    uint8_t checked = 0;
    checked < MAX_RECORDING_FILES;
    checked++
  ) {
    const uint8_t slot = nextPlaySlot;

    nextPlaySlot =
      (nextPlaySlot + 1) %
      MAX_RECORDING_FILES;

    if (!recordingSlots[slot].exists) {
      continue;
    }

    beginPlayback(slot);
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
  if (playback) {
    return;
  }

  if (indicatedPlaySlot >= 0) {
    indicatedPlaySlot = -1;
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
      F("CENTER TAP: STOP PLAYBACK"),
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
  if (playback) {
    stopPlayback();
  }

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
        } else if (playback) {
          stopPlayback();
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

bool testStorageReadWrite() {
  if (SD.exists(SD_TEST_PATH)) {
    SD.remove(SD_TEST_PATH);
  }

  File test = SD.open(SD_TEST_PATH, FILE_WRITE);
  if (!test) {
    logError("SD write test open failed");
    return false;
  }

  static const uint8_t pattern[] = {
    0x53, 0x44, 0x54, 0x45, 0x53, 0x54
  };

  const bool writeOk =
    test.write(pattern, sizeof(pattern)) == sizeof(pattern);
  test.flush();
  test.close();

  if (!writeOk) {
    SD.remove(SD_TEST_PATH);
    logError("SD write test failed");
    return false;
  }

  test = SD.open(SD_TEST_PATH, FILE_READ);
  if (!test) {
    SD.remove(SD_TEST_PATH);
    logError("SD read test open failed");
    return false;
  }

  uint8_t readback[sizeof(pattern)] = {};
  const bool readOk =
    test.read(readback, sizeof(readback)) == sizeof(readback) &&
    memcmp(readback, pattern, sizeof(pattern)) == 0;

  test.close();
  SD.remove(SD_TEST_PATH);

  if (!readOk) {
    logError("SD read test failed");
    return false;
  }

  return true;
}

void logStorageInfo() {
  Serial.print("[SD] card size: ");
  Serial.print(static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
  Serial.println(" MB");

  Serial.print("[SD] total: ");
  Serial.print(static_cast<unsigned long long>(SD.totalBytes() / (1024ULL * 1024ULL)));
  Serial.print(" MB, used: ");
  Serial.print(static_cast<unsigned long long>(SD.usedBytes() / (1024ULL * 1024ULL)));
  Serial.println(" MB");
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
    logError("SD card not detected or mount failed");
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    logError("No microSD card inserted");
    SD.end();
    return false;
  }

  if (!testStorageReadWrite()) {
    logError("microSD is not writable/readable");
    SD.end();
    return false;
  }

  logStorageInfo();
  loadRecordingsFromSd();

  Serial.print("[SD] recordings found: ");
  Serial.println(savedFileCount);

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

  // External 3-pin RGB LED is temporarily disabled.
  // The onboard ESP32-S3 addressable RGB LED on GPIO38 is used instead.
  rgbLedWrite(PIN_ONBOARD_RGB, 0, 0, 0);

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

  audioAvailable =
    initializeAudio();

 initializeNetwork();
initializeWebServer();

  updateLedForCurrentState();

  screenLayoutDirty = true;
  screenContentDirty = true;

  updateScreen(true);
}

void loop() {

 if (setupAccessPointActive) {
  dnsServer.processNextRequest();
}

  if (webServerStarted) {
    server.handleClient();
  }

  updateButton(buttonLeft);
  updateButton(buttonCenter);
  updateButton(buttonRight);

  handleButtonEvents();

  serviceRecordingAudio();

  if (recording && storageWriteError) {
    stopRecording(false);
  }

  servicePlaybackAudio();

  handleAutomaticRecordingStop();
  updatePlaybackIndicator();
  updateScreen();

  delay(1);
}