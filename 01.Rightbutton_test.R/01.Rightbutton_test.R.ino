const int RIGHT_BUTTON_PIN = 27;

// 화면 상태
enum ScreenMode {
  CLOCK_SCREEN,
  STOPWATCH_SCREEN,
  RECORDING_SCREEN
};

ScreenMode currentScreen = CLOCK_SCREEN;

// 버튼 상태 저장
int previousButtonState = LOW;

// 디바운싱
unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 200;

void setup() {
  Serial.begin(115200);

  pinMode(RIGHT_BUTTON_PIN, INPUT);

  Serial.println("오른쪽 버튼 화면 전환 테스트");
  printCurrentScreen();
}

void loop() {
  int currentButtonState = digitalRead(RIGHT_BUTTON_PIN);

  // LOW → HIGH로 바뀌는 순간, 즉 버튼을 누른 순간
  if (previousButtonState == LOW && currentButtonState == HIGH) {

    // 버튼 채터링 방지
    if (millis() - lastButtonTime >= debounceDelay) {
      lastButtonTime = millis();

      changeScreen();
    }
  }

  previousButtonState = currentButtonState;
}

void changeScreen() {
  switch (currentScreen) {

    case CLOCK_SCREEN:
      currentScreen = STOPWATCH_SCREEN;
      break;

    case STOPWATCH_SCREEN:
      currentScreen = RECORDING_SCREEN;
      break;

    case RECORDING_SCREEN:
      currentScreen = CLOCK_SCREEN;
      break;
  }

  printCurrentScreen();
}

void printCurrentScreen() {
  switch (currentScreen) {

    case CLOCK_SCREEN:
      Serial.println("현재 화면: 시계");
      break;

    case STOPWATCH_SCREEN:
      Serial.println("현재 화면: 스톱워치");
      break;

    case RECORDING_SCREEN:
      Serial.println("현재 화면: 녹음");
      break;
  }
}