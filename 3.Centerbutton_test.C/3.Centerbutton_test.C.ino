const int BUTTON_PIN = 27;

// 버튼을 누르면 LOW, 떼면 HIGH인 경우
const int PRESSED_STATE = LOW;

const unsigned long LONG_PRESS_TIME = 2000;
const unsigned long MAX_RECORDING_TIME = 60000;
const unsigned long DEBOUNCE_TIME = 50;

bool isRecording = false;
unsigned long recordingStartTime = 0;

// 버튼 상태
int lastRawButtonState;
int stableButtonState;

unsigned long lastDebounceTime = 0;
unsigned long buttonPressedTime = 0;

bool buttonIsPressed = false;
bool longPressHandled = false;

// 녹음 슬롯
const int MAX_FILES = 4;

bool fileExists[MAX_FILES] = {
  false, false, false, false
};

int nextWriteSlot = 0;
int nextPlaySlot = 0;
int savedFileCount = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);

  stableButtonState = digitalRead(BUTTON_PIN);
  lastRawButtonState = stableButtonState;

  Serial.println();
  Serial.println("=== 중앙 버튼 기능 테스트 ===");
  Serial.println("2초 이상 누르기: 녹음 시작");
  Serial.println("녹음 중 짧게 누르기: 녹음 종료");
  Serial.println("대기 중 짧게 누르기: 다음 파일 재생");
}

void loop() {
  handleButton();
  handleAutomaticRecordingStop();
}

void handleButton() {
  int rawButtonState = digitalRead(BUTTON_PIN);

  // 버튼 신호가 바뀐 순간부터 디바운싱 시작
  if (rawButtonState != lastRawButtonState) {
    lastDebounceTime = millis();
  }

  // 신호가 50ms 이상 유지되면 실제 상태 변화로 인정
  if (millis() - lastDebounceTime >= DEBOUNCE_TIME) {
    if (rawButtonState != stableButtonState) {
      stableButtonState = rawButtonState;

      if (stableButtonState == PRESSED_STATE) {
        // 버튼 누른 순간
        buttonIsPressed = true;
        buttonPressedTime = millis();
        longPressHandled = false;
      } else {
        // 버튼에서 손을 뗀 순간
        if (buttonIsPressed) {
          handleButtonRelease();
        }

        buttonIsPressed = false;
      }
    }
  }

  // 실제로 버튼을 누른 상태일 때만 길게 누르기 판단
  if (
    buttonIsPressed &&
    stableButtonState == PRESSED_STATE &&
    !isRecording &&
    !longPressHandled &&
    millis() - buttonPressedTime >= LONG_PRESS_TIME
  ) {
    longPressHandled = true;
    startRecording();
  }

  lastRawButtonState = rawButtonState;
}

void handleButtonRelease() {
  unsigned long pressDuration =
    millis() - buttonPressedTime;

  // 길게 눌러 녹음을 시작한 뒤 손을 뗀 것은 무시
  if (longPressHandled) {
    return;
  }

  // 녹음 중 짧게 누르면 녹음 종료
  if (isRecording) {
    stopRecording(false);
    return;
  }

  // 녹음 중이 아닐 때 짧게 누르면 파일 재생
  if (pressDuration < LONG_PRESS_TIME) {
    playNextFile();
  }
}

void startRecording() {
  isRecording = true;
  recordingStartTime = millis();

  Serial.println();
  Serial.println("▶ 녹음 시작");

  Serial.printf(
    "저장 예정 슬롯: %d번\n",
    nextWriteSlot + 1
  );
}

void stopRecording(bool automaticStop) {
  if (!isRecording) {
    return;
  }

  isRecording = false;

  unsigned long recordingDuration =
    millis() - recordingStartTime;

  int savedSlot = nextWriteSlot;

  if (!fileExists[savedSlot]) {
    fileExists[savedSlot] = true;

    if (savedFileCount < MAX_FILES) {
      savedFileCount++;
    }
  }

  Serial.println();

  if (automaticStop) {
    Serial.println("■ 1분 초과: 녹음 자동 종료");
  } else {
    Serial.println("■ 녹음 종료");
  }

  Serial.printf(
    "%d번 슬롯에 저장 완료\n",
    savedSlot + 1
  );

  Serial.printf(
    "녹음 시간: %.1f초\n",
    recordingDuration / 1000.0
  );

  nextWriteSlot =
    (nextWriteSlot + 1) % MAX_FILES;

  printFileStatus();
}

void playNextFile() {
  if (savedFileCount == 0) {
    Serial.println();
    Serial.println("재생할 녹음 파일이 없습니다.");
    return;
  }

  for (int count = 0; count < MAX_FILES; count++) {
    if (fileExists[nextPlaySlot]) {
      Serial.println();

      Serial.printf(
        "▶ %d번 녹음 파일 재생\n",
        nextPlaySlot + 1
      );

      nextPlaySlot =
        (nextPlaySlot + 1) % MAX_FILES;

      return;
    }

    nextPlaySlot =
      (nextPlaySlot + 1) % MAX_FILES;
  }
}

void handleAutomaticRecordingStop() {
  if (
    isRecording &&
    millis() - recordingStartTime >= MAX_RECORDING_TIME
  ) {
    stopRecording(true);
  }
}

void printFileStatus() {
  Serial.print("현재 파일: ");

  for (int i = 0; i < MAX_FILES; i++) {
    Serial.printf(
      "[%d번:%s] ",
      i + 1,
      fileExists[i] ? "저장됨" : "비어 있음"
    );
  }

  Serial.println();
}