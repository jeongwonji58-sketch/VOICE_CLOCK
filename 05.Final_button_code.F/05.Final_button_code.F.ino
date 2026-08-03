// =====================================================
// 버튼 핀 설정
// 일반 택트 스위치: GPIO와 GND 사이에 연결
// INPUT_PULLUP 사용 → 누르면 LOW
// =====================================================

constexpr int PIN_BUTTON_LEFT   = 25;
constexpr int PIN_BUTTON_CENTER = 26;
constexpr int PIN_BUTTON_RIGHT  = 27;

constexpr int PRESSED_STATE = LOW;

// 버튼 디바운싱 시간
constexpr unsigned long DEBOUNCE_TIME = 50;

// 중앙 버튼 길게 누르는 기준
constexpr unsigned long LONG_PRESS_TIME = 2000;

// 최대 녹음 시간: 1분
constexpr unsigned long MAX_RECORDING_TIME = 60000;


// =====================================================
// 버튼 상태 구조체
// =====================================================

struct Button {
  int pin;

  int rawState = HIGH;
  int previousRawState = HIGH;
  int stableState = HIGH;

  unsigned long lastChangeTime = 0;
  unsigned long pressedTime = 0;

  bool pressedEvent = false;
  bool releasedEvent = false;
  bool longPressHandled = false;
};


// 버튼 객체
Button leftButton   = { PIN_BUTTON_LEFT };
Button centerButton = { PIN_BUTTON_CENTER };
Button rightButton  = { PIN_BUTTON_RIGHT };


// =====================================================
// 화면 상태
// =====================================================

enum ScreenMode {
  CLOCK_SCREEN,
  STOPWATCH_SCREEN,
  RECORDING_SCREEN
};

ScreenMode currentScreen = CLOCK_SCREEN;


// =====================================================
// 스톱워치 상태
// =====================================================

bool stopwatchRunning = false;

unsigned long stopwatchStartTime = 0;
unsigned long accumulatedTime = 0;


// 좌측+중앙 동시 입력 관리
bool resetCombinationActive = false;


// =====================================================
// 녹음 상태
// =====================================================

bool isRecording = false;
unsigned long recordingStartTime = 0;

constexpr int MAX_FILES = 4;

bool fileExists[MAX_FILES] = {
  false, false, false, false
};

// 다음 녹음 저장 위치
int nextWriteSlot = 0;

// 다음 파일 재생 위치
int nextPlaySlot = 0;

// 현재 저장된 파일 수
int savedFileCount = 0;


// =====================================================
// setup
// =====================================================

void setup() {
  Serial.begin(115200);

  initializeButton(leftButton);
  initializeButton(centerButton);
  initializeButton(rightButton);

  Serial.println();
  Serial.println("================================");
  Serial.println("전자시계 버튼 통합 테스트 시작");
  Serial.println("================================");

  printCurrentScreen();
}


// =====================================================
// loop
// =====================================================

void loop() {
  updateButton(leftButton);
  updateButton(centerButton);
  updateButton(rightButton);

  // 우측 버튼: 화면 전환
  handleRightButton();

  // 좌측 + 중앙 버튼: 스톱워치 초기화
  handleResetCombination();

  /*
   * 초기화 조합 중에는
   * 좌측·중앙 버튼 개별 기능을 실행하지 않음
   */
  if (!resetCombinationActive) {
    handleLeftButton();
    handleCenterButton();
  }

  // 녹음 1분 자동 종료 확인
  handleAutomaticRecordingStop();

  /*
   * 실행 중 스톱워치 시간을 계속 출력하지 않음.
   * 정지하거나 화면을 전환할 때만 시간을 한 번 출력함.
   */
}


// =====================================================
// 버튼 초기화
// =====================================================

void initializeButton(Button &button) {
  pinMode(button.pin, INPUT_PULLUP);

  int initialState = digitalRead(button.pin);

  button.rawState = initialState;
  button.previousRawState = initialState;
  button.stableState = initialState;
}


// =====================================================
// 버튼 상태 갱신 및 디바운싱
// =====================================================

void updateButton(Button &button) {
  button.pressedEvent = false;
  button.releasedEvent = false;

  button.rawState = digitalRead(button.pin);

  // 읽힌 값이 변경되면 디바운싱 시간 측정 시작
  if (button.rawState != button.previousRawState) {
    button.lastChangeTime = millis();
    button.previousRawState = button.rawState;
  }

  // 일정 시간 이상 같은 상태가 유지됐을 때만 인정
  if (millis() - button.lastChangeTime >= DEBOUNCE_TIME) {
    if (button.rawState != button.stableState) {
      button.stableState = button.rawState;

      // 버튼을 누른 순간
      if (button.stableState == PRESSED_STATE) {
        button.pressedEvent = true;
        button.pressedTime = millis();
        button.longPressHandled = false;
      }

      // 버튼에서 손을 뗀 순간
      else {
        button.releasedEvent = true;
      }
    }
  }
}


// 버튼이 현재 눌려 있는지 확인
bool isButtonPressed(const Button &button) {
  return button.stableState == PRESSED_STATE;
}


// =====================================================
// 우측 버튼: 화면 전환
// =====================================================

void handleRightButton() {
  if (!rightButton.pressedEvent) {
    return;
  }

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


// =====================================================
// 현재 화면 출력
// =====================================================

void printCurrentScreen() {
  Serial.println();

  switch (currentScreen) {
    case CLOCK_SCREEN:
      Serial.println("현재 화면: 시계");
      break;

    case STOPWATCH_SCREEN:
      Serial.println("현재 화면: 스톱워치");
      printStopwatchImmediately();
      break;

    case RECORDING_SCREEN:
      Serial.println("현재 화면: 녹음");
      break;
  }
}


// =====================================================
// 좌측 버튼: 스톱워치 시작·정지
// =====================================================

void handleLeftButton() {
  // 스톱워치 화면에서만 동작
  if (currentScreen != STOPWATCH_SCREEN) {
    return;
  }

  if (leftButton.pressedEvent) {
    toggleStopwatch();
  }
}


// =====================================================
// 스톱워치 시작·정지
// =====================================================

void toggleStopwatch() {
  if (!stopwatchRunning) {
    stopwatchStartTime = millis();
    stopwatchRunning = true;

    Serial.println();
    Serial.println("=== 스톱워치 시작 ===");
  }

  else {
    accumulatedTime += millis() - stopwatchStartTime;
    stopwatchRunning = false;

    Serial.println();
    Serial.println("=== 스톱워치 정지 ===");

    // 정지한 순간의 시간만 한 번 출력
    printStopwatchImmediately();
  }
}


// =====================================================
// 현재 스톱워치 시간 반환
// =====================================================

unsigned long getElapsedTime() {
  if (stopwatchRunning) {
    return accumulatedTime +
           (millis() - stopwatchStartTime);
  }

  return accumulatedTime;
}


// =====================================================
// 스톱워치 시간 한 번 출력
// =====================================================

void printStopwatchImmediately() {
  unsigned long elapsedTime = getElapsedTime();

  unsigned long minutes =
    elapsedTime / 60000;

  unsigned long seconds =
    (elapsedTime % 60000) / 1000;

  unsigned long centiseconds =
    (elapsedTime % 1000) / 10;

  Serial.printf(
    "%02lu:%02lu.%02lu [%s]\n",
    minutes,
    seconds,
    centiseconds,
    stopwatchRunning ? "RUN" : "STOP"
  );
}


// =====================================================
// 좌측+중앙 버튼: 스톱워치 초기화
// =====================================================

void handleResetCombination() {
  bool leftPressed =
    isButtonPressed(leftButton);

  bool centerPressed =
    isButtonPressed(centerButton);

  // 두 버튼이 동시에 눌린 순간
  if (
    leftPressed &&
    centerPressed &&
    !resetCombinationActive
  ) {
    resetCombinationActive = true;
    resetStopwatch();
  }

  /*
   * 두 버튼에서 모두 손을 떼어야
   * 다시 개별 버튼 기능 사용 가능
   */
  if (
    resetCombinationActive &&
    !leftPressed &&
    !centerPressed
  ) {
    resetCombinationActive = false;
  }
}


// =====================================================
// 스톱워치 초기화
// =====================================================

void resetStopwatch() {
  stopwatchRunning = false;
  stopwatchStartTime = 0;
  accumulatedTime = 0;

  Serial.println();
  Serial.println("====================");
  Serial.println("스톱워치 초기화");
  Serial.println("00:00.00 [STOP]");
  Serial.println("====================");
}


// =====================================================
// 중앙 버튼: 녹음 시작·종료·재생
// =====================================================

void handleCenterButton() {
  // 녹음 화면에서만 중앙 버튼 기능 사용
  if (currentScreen != RECORDING_SCREEN) {
    return;
  }

  /*
   * 녹음 중이 아닐 때
   * 중앙 버튼을 2초 이상 누르면 녹음 시작
   */
  if (
    isButtonPressed(centerButton) &&
    !isRecording &&
    !centerButton.longPressHandled &&
    millis() - centerButton.pressedTime >= LONG_PRESS_TIME
  ) {
    centerButton.longPressHandled = true;
    startRecording();
  }

  // 버튼에서 손을 뗀 순간
  if (centerButton.releasedEvent) {
    unsigned long pressDuration =
      millis() - centerButton.pressedTime;

    /*
     * 길게 눌러 녹음을 시작한 뒤 손을 뗀 것은
     * 녹음 종료로 처리하지 않음
     */
    if (centerButton.longPressHandled) {
      return;
    }

    // 녹음 중 짧게 누르면 녹음 종료
    if (isRecording) {
      stopRecording(false);
      return;
    }

    // 녹음 대기 중 짧게 누르면 파일 재생
    if (pressDuration < LONG_PRESS_TIME) {
      playNextFile();
    }
  }
}


// =====================================================
// 녹음 시작
// =====================================================

void startRecording() {
  isRecording = true;
  recordingStartTime = millis();

  Serial.println();
  Serial.println("▶ 녹음 시작");

  Serial.printf(
    "저장 예정 슬롯: %d번\n",
    nextWriteSlot + 1
  );

  // 추후 실제 기능 연결
  // setLedWhite();
  // startMicrophoneRecording();
}


// =====================================================
// 녹음 종료 및 슬롯 저장
// =====================================================

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
    Serial.println("■ 1분 경과: 녹음 자동 종료");

    // 추후 부저 구현
    // playBuzzer();
  }

  else {
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

  /*
   * 다음 저장 슬롯으로 이동
   * 4번 다음에는 1번으로 돌아가 덮어쓰기
   */
  nextWriteSlot =
    (nextWriteSlot + 1) % MAX_FILES;

  // 추후 실제 기능 연결
  // stopMicrophoneRecording();
  // saveWavFile(savedSlot);
  // setLedRed();

  printFileStatus();
}


// =====================================================
// 저장된 녹음 파일 순차 재생
// =====================================================

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

      // 추후 실제 기능 연결
      // playWavFile(nextPlaySlot);
      // setLedBlue();

      nextPlaySlot =
        (nextPlaySlot + 1) % MAX_FILES;

      return;
    }

    nextPlaySlot =
      (nextPlaySlot + 1) % MAX_FILES;
  }
}


// =====================================================
// 녹음 1분 경과 확인
// =====================================================

void handleAutomaticRecordingStop() {
  if (
    isRecording &&
    millis() - recordingStartTime >= MAX_RECORDING_TIME
  ) {
    stopRecording(true);
  }
}


// =====================================================
// 녹음 슬롯 상태 출력
// =====================================================

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