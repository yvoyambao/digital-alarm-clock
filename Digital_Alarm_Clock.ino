
#include <Wire.h>
#include <RTClib.h>

RTC_DS1307 rtc;

const int LATCH_PIN = 9;
const int CLOCK_PIN = 10;
const int DATA_PIN = 8;

const int DIGIT_PINS[4] = { 7, 6, 5, 4 };

const int MODE_BTN = A0;
const int UP_BTN = A1;
const int ALARM_BTN = A2;
const int STOP_ALARM_BTN = A3;

const int buzzerPin = 3;

const bool COMMON_ANODE = false;
const int DIGIT_ON_LEVEL = COMMON_ANODE ? HIGH : LOW;
const int DIGIT_OFF_LEVEL = COMMON_ANODE ? LOW : HIGH;

const byte digitTable[10] = {
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};

volatile int dispDigits[4] = { 1, 2, 3, 4 };

struct Button {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeTime;
};

Button modeButton = {MODE_BTN, HIGH, HIGH, 0};
Button upButton = {UP_BTN, HIGH, HIGH, 0};
Button alarmButton = {ALARM_BTN, HIGH, HIGH, 0};
Button stopAlarmButton = { STOP_ALARM_BTN, HIGH, HIGH, 0};

const unsigned long DEBOUNCE_MS = 75;

enum Mode { NORMAL,
            SET_HOUR,
            SET_MINUTE,
            SET_ALARM_HOUR,
            SET_ALARM_MINUTE };
Mode mode = NORMAL;

int tempHour = 0, tempMinute = 0, alarmHour = 0, alarmMinute = 0;

bool alarmTriggered = false;
bool alarmSounding = false;
bool alarmEnabled = true;

void setup() {
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);

  pinMode(buzzerPin, OUTPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(DIGIT_PINS[i], OUTPUT);
    digitalWrite(DIGIT_PINS[i], DIGIT_OFF_LEVEL);
  }

  pinMode(MODE_BTN, INPUT_PULLUP);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(ALARM_BTN, INPUT_PULLUP);
  pinMode(STOP_ALARM_BTN, INPUT_PULLUP);

  Serial.begin(9600);

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println("DS1307 not found");  // if clock not found print to serial monitor
  }

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  setupDisplayTimer();
}

byte getPattern(int val) {
  byte pattern;
  if (val < 0 || val > 9) {
    pattern = 0x00;
  } else {
    pattern = digitTable[val];
  }

  if (COMMON_ANODE) {
    pattern = ~pattern;
  }
  return pattern;
}

void refreshDisplay() {
  static int currentDigit = 0;

  for (int i = 0; i < 4; i++) {
    digitalWrite(DIGIT_PINS[i], DIGIT_OFF_LEVEL);
  }

  byte pattern = getPattern(dispDigits[currentDigit]);

  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, pattern);
  digitalWrite(LATCH_PIN, HIGH);

  digitalWrite(DIGIT_PINS[currentDigit], DIGIT_ON_LEVEL);
  currentDigit = (currentDigit + 1) % 4;
}

void setupDisplayTimer() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 5999;             // 16Mhz / prescaler
  TCCR1B |= (1 << WGM12);   // CTC mode
  TCCR1B |= (1 << CS11);    // prescaler = 8
  TIMSK1 |= (1 << OCIE1A);  // enable Timer1 compare-A interrupt
  interrupts();
}

ISR(TIMER1_COMPA_vect) {
  refreshDisplay();
}

// BUTTONS

bool buttonPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastChangeTime = millis();
  }
  bool pressedEdge = false;
  if (millis() - b.lastChangeTime > DEBOUNCE_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        pressedEdge = true;
      }
    }
  }
  b.lastReading = reading;
  return pressedEdge;
}

void loop() {
  static unsigned long lastRTCPoll = 0;
  static DateTime cachedNow;

  if (millis() - lastRTCPoll > 200) {
    lastRTCPoll = millis();
    cachedNow = rtc.now();
  }

  int displayHour, displayMinute;
  if (mode == NORMAL) {
    displayHour = cachedNow.hour();
    displayMinute = cachedNow.minute();
  } else {
    displayHour = tempHour;
    displayMinute = tempMinute;
  }

  dispDigits[0] = displayHour / 10;
  dispDigits[1] = displayHour % 10;
  dispDigits[2] = displayMinute / 10;
  dispDigits[3] = displayMinute % 10;

  if (buttonPressed(modeButton)) {
    if (mode == NORMAL) {
      mode = SET_HOUR;
      tempHour = cachedNow.hour();
      tempMinute = cachedNow.minute();
      Serial.println("Editing HOUR");

    } else if (mode == SET_HOUR) {
      mode = SET_MINUTE;
      Serial.println("Editing MINUTE");

    } else if (mode == SET_MINUTE) {
      rtc.adjust(DateTime(cachedNow.year(), cachedNow.month(), cachedNow.day(), tempHour, tempMinute, 0));
      mode = NORMAL;
      Serial.print("Time set to ");
      Serial.print(tempHour);
      Serial.print(":");
      if (tempMinute < 10) Serial.print('0');
      Serial.println(tempMinute);
    }
  }

  if (buttonPressed(alarmButton)) {
    if (mode == NORMAL) {
      mode = SET_ALARM_HOUR;
      tempHour = cachedNow.hour();
      tempMinute = cachedNow.minute();
      Serial.println("Editing ALARM HOUR");

    } else if (mode == SET_ALARM_HOUR) {
      mode = SET_ALARM_MINUTE;
      Serial.println("Editing ALARM MINUTE");

    } else if (mode == SET_ALARM_MINUTE) {
      mode = NORMAL;
      alarmHour = tempHour;
      alarmMinute = tempMinute;
      Serial.print("Alarm set to ");
      Serial.print(alarmHour);
      Serial.print(":");
      if (alarmMinute < 10) Serial.print('0');
      Serial.println(alarmMinute);
    }
  }

  if (buttonPressed(upButton)) {
    if (mode == SET_HOUR) {
      tempHour = (tempHour + 1) % 24;
    } else if (mode == SET_MINUTE) {
      tempMinute = (tempMinute + 1) % 60;
    } else if (mode == SET_ALARM_HOUR) {
      tempHour = (tempHour + 1) % 24;
    } else if (mode == SET_ALARM_MINUTE) {
      tempMinute = (tempMinute + 1) % 60;
    } else if (mode == NORMAL) {
      alarmEnabled = !alarmEnabled;
      Serial.println(alarmEnabled ? "Alarm ENABLED" : "Alarm DISABLED");
    }
  }

  if (buttonPressed(stopAlarmButton)) {
    if (alarmSounding) {
      alarmSounding = false;
      Serial.println("Alarm stopped");
    }
  }

  bool timeMatches = (cachedNow.hour() == alarmHour && cachedNow.minute() == alarmMinute);

  if (timeMatches && !alarmTriggered && mode == NORMAL && alarmEnabled) {
    alarmTriggered = true;
    alarmSounding = true;
    Serial.println("ALARM!");
  } else if (!timeMatches) {
    alarmTriggered = false;
    alarmSounding = false;
  }
  digitalWrite(buzzerPin, alarmSounding ? HIGH : LOW);
}
