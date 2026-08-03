const int BUTTON_PIN = 27;

// 이전 테스트에서 버튼을 누를 때 1이 나왔다면 HIGH
const int PRESSED_STATE = HIGH;

bool stopwatchRunning = false;

unsigned long startTime = 0;
unsigned long accumulatedTime = 0;
unsigned long lastPrintTime = 0;

// 버튼 디바운싱 변수
int stableButtonState;
int lastReading;
unsigned long lastDebounceTime = 0;

const unsigned long DEBOUNCE_DELAY = 50;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);

  // 실제 버튼의 초기 상태를 읽어서 저장
  stableButtonState = digitalRead(BUTTON_PIN);
  lastReading = stableButtonState;

  Serial.println("스톱워치 테스트 시작");
  Serial.println("한 번 클릭: 시작");
  Serial.println("다시 클릭: 정지");
}

void loop() {
  handleButton();
  printStopwatch();
}

void handleButton() {
  int currentReading = digitalRead(BUTTON_PIN);

  // 신호가 변하면 디바운싱 시간 다시 측정
  if (currentReading != lastReading) {
    lastDebounceTime = millis();
  }

  // 일정 시간 동안 같은 신호가 유지된 경우
  if (millis() - lastDebounceTime >= DEBOUNCE_DELAY) {

    // 안정된 버튼 상태가 실제로 바뀌었을 때
    if (currentReading != stableButtonState) {
      stableButtonState = currentReading;

      // 버튼을 누른 순간에만 실행
      if (stableButtonState == PRESSED_STATE) {
        toggleStopwatch();
      }
    }
  }

  lastReading = currentReading;
}

void toggleStopwatch() {
  if (!stopwatchRunning) {
    startTime = millis();
    stopwatchRunning = true;

    Serial.println("=== 스톱워치 시작 ===");
  } else {
    accumulatedTime += millis() - startTime;
    stopwatchRunning = false;

    Serial.println("=== 스톱워치 정지 ===");
  }
}

unsigned long getElapsedTime() {
  if (stopwatchRunning) {
    return accumulatedTime + (millis() - startTime);
  }

  return accumulatedTime;
}

void printStopwatch() {
  if (millis() - lastPrintTime < 100) {
    return;
  }

  lastPrintTime = millis();

  unsigned long elapsedTime = getElapsedTime();

  unsigned long minutes = elapsedTime / 60000;
  unsigned long seconds = (elapsedTime % 60000) / 1000;
  unsigned long centiseconds = (elapsedTime % 1000) / 10;

  Serial.printf(
    "%02lu:%02lu.%02lu  [%s]\n",
    minutes,
    seconds,
    centiseconds,
    stopwatchRunning ? "RUN" : "STOP"
  );
}