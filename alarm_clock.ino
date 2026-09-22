/*
  Arduino Uno Alarm Clock
  - 4-digit 7-segment display, multiplexed, driven through ONE 74HC595
  - DS1307 RTC for timekeeping (needs the Adafruit "RTClib" library)
  - 3 buttons: MODE (cycle settings), UP (increment / toggle alarm), STOP (silence alarm)
  - Buzzer for the alarm

  ---- WIRING ----
  74HC595:
    DS (pin14)    -> Arduino pin 8  (DATA_PIN)
    SH_CP (pin11) -> Arduino pin 10 (CLOCK_PIN)
    ST_CP (pin12) -> Arduino pin 9  (LATCH_PIN)
    MR (pin10)    -> 5V   (must be HIGH or the chip stays reset)
    OE (pin13)    -> GND  (must be LOW or the outputs stay disabled)
    Q0..Q7        -> segments a,b,c,d,e,f,g,dp on the display (through ~330 ohm resistors)
                     NOTE: all 4 digits' same-letter segments are wired together in parallel;
                     that's what makes multiplexing possible with only one 74HC595.

  Digit common pins (the 4 pins that select WHICH digit lights up), wired DIRECTLY
  to the Arduino with no transistors -- fine at this project's current draw:
    Digit 1 (leftmost, tens of hour)  -> Arduino pin 7
    Digit 2 (ones of hour)            -> Arduino pin 6
    Digit 3 (tens of minute)          -> Arduino pin 5
    Digit 4 (ones of minute)          -> Arduino pin 4
    (DIGIT_PINS[] below is {7,6,5,4] to match -- if your own display's common
    pins land on different physical Arduino pins, wire to those instead and
    update DIGIT_PINS[] accordingly; the left-to-right order is what matters.)

  Buttons (each: one leg to the pin, other leg to GND -- uses internal pullups):
    MODE -> A0
    UP   -> A1
    STOP -> A2

  Buzzer:
    Signal -> pin 3, other leg -> GND
    (Code uses tone()/noTone(), which suits a passive buzzer. If yours is an ACTIVE
    buzzer module, replace the tone()/noTone() calls in updateBuzzer() with
    digitalWrite(BUZZER_PIN, HIGH) / digitalWrite(BUZZER_PIN, LOW).)

  DS1307 RTC:
    SDA -> A4, SCL -> A5 (standard Uno I2C pins), VCC -> 5V, GND -> GND

  If your display is COMMON ANODE instead of common cathode, just flip
  COMMON_ANODE to true below -- the code handles the rest.

  ---- USAGE ----
  Normal mode: shows HH:MM.
    - Press UP to turn the alarm on/off (bottom digits stay lit either way; watch
      Serial Monitor if you want on-screen confirmation -- see comment in handleUpButton()).
  Press MODE to enter settings, cycling through:
    Set Hour -> Set Minute -> Set Alarm Hour -> Set Alarm Minute -> back to Normal
    While setting, the field you're editing blinks; press UP to increment it.
    Leaving Set Minute writes the new time to the RTC.
    Leaving Set Alarm Minute saves the alarm to EEPROM (survives power loss).
  When the alarm goes off, press STOP to silence it.
*/

#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>

RTC_DS1307 rtc;

// ---------------- Pin configuration ----------------
const int LATCH_PIN = 9;
const int CLOCK_PIN = 10;
const int DATA_PIN  = 8;

const int DIGIT_PINS[4] = {7, 6, 5, 4}; // reversed to match physical wiring (was mirrored)

const int MODE_BTN = A0;
const int UP_BTN   = A1;
const int STOP_BTN = A2;

const int BUZZER_PIN = 3;

const bool COMMON_ANODE = false; // set true if your display is common anode

// With a transistor on each digit-select line, driving the Arduino pin HIGH
// would turn the transistor on and pull the display's common pin LOW. Wired
// directly (no transistors) to a COMMON CATHODE display, it's the opposite:
// the Arduino pin itself must go LOW to light that digit, and HIGH to turn
// digits off. This formula is for that direct-wire, common-cathode case.
const int DIGIT_ON_LEVEL  = COMMON_ANODE ? HIGH : LOW;
const int DIGIT_OFF_LEVEL = COMMON_ANODE ? LOW  : HIGH;

// ---------------- Segment lookup table ----------------
// Bits: 0=a 1=b 2=c 3=d 4=e 5=f 6=g 7=dp  (written for a COMMON-CATHODE display;
// getPattern() inverts these automatically if COMMON_ANODE is true)
const byte digitTable[10] = {
  0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
};
const byte DP_BIT = 0x80;

// ---------------- Modes ----------------
enum Mode { NORMAL, SET_HOUR, SET_MINUTE, SET_ALARM_HOUR, SET_ALARM_MINUTE };
Mode mode = NORMAL;

int tempHour = 0, tempMinute = 0;      // scratch values while setting the clock
int alarmHour = 7, alarmMinute = 0;    // current alarm time
bool alarmEnabled = true;
bool alarmSounding = false;

bool blinkOn = true;
unsigned long lastBlinkToggle = 0;
const unsigned long BLINK_INTERVAL_MS = 400;

// Shared with the Timer1 ISR below -- must be volatile.
volatile int  dispDigits[4] = {0, 0, 0, 0}; // -1 means "blank"
volatile bool dp[4] = {false, false, false, false};

// ---------------- Button debouncing ----------------
struct Button {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeTime;
};
Button modeButton = {MODE_BTN, HIGH, HIGH, 0};
Button upButton   = {UP_BTN,   HIGH, HIGH, 0};
Button stopButton = {STOP_BTN, HIGH, HIGH, 0};
const unsigned long DEBOUNCE_MS = 30;

// ---------------- EEPROM addresses ----------------
const int EE_ALARM_HOUR = 0;
const int EE_ALARM_MIN  = 1;
const int EE_ALARM_EN   = 2;

// =====================================================

void setup() {
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(DIGIT_PINS[i], OUTPUT);
    digitalWrite(DIGIT_PINS[i], DIGIT_OFF_LEVEL);
  }

  pinMode(MODE_BTN, INPUT_PULLUP);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  Serial.begin(9600);

  Wire.begin();
  if (!rtc.begin()) {
    Serial.println(F("DS1307 not found - check wiring!"));
  }
  if (!rtc.isrunning()) {
    // Clock isn't running (e.g. first power-up with no battery, or dead battery):
    // seed it with the time this sketch was compiled. Note: RTC_DS1307 has no
    // lostPower() method (that's only on DS3231/PCF8523/PCF8563) -- isrunning()
    // is the DS1307's equivalent check.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  loadAlarmFromEEPROM();
  setupDisplayTimer();
}

// Fires refreshDisplay() every ~3ms on a hardware timer, independent of
// whatever loop() is doing (including any slow/blocking RTC reads). This is
// what keeps the display from flashing/flickering if the main loop stalls.
void setupDisplayTimer() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A = 5999;                // 16MHz / prescaler 8 = 2MHz timer clock; 6000 ticks = 3ms
  TCCR1B |= (1 << WGM12);      // CTC mode
  TCCR1B |= (1 << CS11);       // prescaler = 8
  TIMSK1 |= (1 << OCIE1A);     // enable Timer1 compare-A interrupt
  interrupts();
}

ISR(TIMER1_COMPA_vect) {
  refreshDisplay();
}

void loop() {
  if (buttonPressed(modeButton)) handleModeButton();
  if (buttonPressed(upButton))   handleUpButton();
  if (buttonPressed(stopButton)) handleStopButton();

  if (millis() - lastBlinkToggle > BLINK_INTERVAL_MS) {
    lastBlinkToggle = millis();
    blinkOn = !blinkOn;
  }

  // Poll the RTC a few times a second rather than every loop -- I2C reads take
  // a little time, and there's no need to hit the bus more often than this.
  static unsigned long lastRTCPoll = 0;
  static DateTime cachedNow;
  if (millis() - lastRTCPoll > 200) {
    lastRTCPoll = millis();
    cachedNow = rtc.now();
  }

  updateDisplayBuffer(cachedNow);
  checkAlarm(cachedNow);
  updateBuzzer();
}

// ---------------- Button actions ----------------

void handleModeButton() {
  switch (mode) {
    case NORMAL: {
      DateTime now = rtc.now();
      tempHour = now.hour();
      tempMinute = now.minute();
      mode = SET_HOUR;
      break;
    }
    case SET_HOUR:
      mode = SET_MINUTE;
      break;
    case SET_MINUTE: {
      DateTime now = rtc.now();
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), tempHour, tempMinute, 0));
      mode = SET_ALARM_HOUR;
      break;
    }
    case SET_ALARM_HOUR:
      mode = SET_ALARM_MINUTE;
      break;
    case SET_ALARM_MINUTE:
      saveAlarmToEEPROM();
      mode = NORMAL;
      break;
  }
}

void handleUpButton() {
  switch (mode) {
    case NORMAL:
      alarmEnabled = !alarmEnabled;
      Serial.print(F("Alarm "));
      Serial.println(alarmEnabled ? F("ON") : F("OFF"));
      break;
    case SET_HOUR:
      tempHour = (tempHour + 1) % 24;
      break;
    case SET_MINUTE:
      tempMinute = (tempMinute + 1) % 60;
      break;
    case SET_ALARM_HOUR:
      alarmHour = (alarmHour + 1) % 24;
      break;
    case SET_ALARM_MINUTE:
      alarmMinute = (alarmMinute + 1) % 60;
      break;
  }
}

void handleStopButton() {
  if (alarmSounding) {
    alarmSounding = false;
    noTone(BUZZER_PIN);
  }
}

// ---------------- Alarm & buzzer ----------------

void checkAlarm(const DateTime &now) {
  if (mode != NORMAL) return; // don't trigger while the user is in a settings menu
  if (alarmEnabled && !alarmSounding &&
      now.hour() == alarmHour && now.minute() == alarmMinute && now.second() == 0) {
    alarmSounding = true;
  }
}

void updateBuzzer() {
  static unsigned long lastToggle = 0;
  static bool buzzerOn = false;

  if (!alarmSounding) {
    if (buzzerOn) {
      noTone(BUZZER_PIN);
      buzzerOn = false;
    }
    return;
  }

  if (millis() - lastToggle > 300) {
    lastToggle = millis();
    buzzerOn = !buzzerOn;
    if (buzzerOn) tone(BUZZER_PIN, 1000);
    else noTone(BUZZER_PIN);
  }
}

// ---------------- Display ----------------

void updateDisplayBuffer(const DateTime &now) {
  dp[0] = dp[1] = dp[2] = dp[3] = false;

  switch (mode) {
    case NORMAL:
      dispDigits[0] = now.hour() / 10;
      dispDigits[1] = now.hour() % 10;
      dispDigits[2] = now.minute() / 10;
      dispDigits[3] = now.minute() % 10;
      break;

    case SET_HOUR:
      if (blinkOn) {
        dispDigits[0] = tempHour / 10;
        dispDigits[1] = tempHour % 10;
      } else {
        dispDigits[0] = dispDigits[1] = -1;
      }
      dispDigits[2] = tempMinute / 10;
      dispDigits[3] = tempMinute % 10;
      break;

    case SET_MINUTE:
      dispDigits[0] = tempHour / 10;
      dispDigits[1] = tempHour % 10;
      if (blinkOn) {
        dispDigits[2] = tempMinute / 10;
        dispDigits[3] = tempMinute % 10;
      } else {
        dispDigits[2] = dispDigits[3] = -1;
      }
      break;

    case SET_ALARM_HOUR:
      if (blinkOn) {
        dispDigits[0] = alarmHour / 10;
        dispDigits[1] = alarmHour % 10;
      } else {
        dispDigits[0] = dispDigits[1] = -1;
      }
      dispDigits[2] = alarmMinute / 10;
      dispDigits[3] = alarmMinute % 10;
      dp[3] = true; // decimal point = "you're editing the alarm"
      break;

    case SET_ALARM_MINUTE:
      dispDigits[0] = alarmHour / 10;
      dispDigits[1] = alarmHour % 10;
      if (blinkOn) {
        dispDigits[2] = alarmMinute / 10;
        dispDigits[3] = alarmMinute % 10;
      } else {
        dispDigits[2] = dispDigits[3] = -1;
      }
      dp[3] = true;
      break;
  }
}

byte getPattern(int val, bool showDP) {
  byte pattern;
  if (val < 0 || val > 9) {
    pattern = 0x00; // blank, before polarity inversion
  } else {
    pattern = digitTable[val];
    if (showDP) pattern |= DP_BIT;
  }
  if (COMMON_ANODE) pattern = ~pattern;
  return pattern;
}

void refreshDisplay() {
  static int currentDigit = 0;

  // Blank all digits first so segment data doesn't "ghost" onto the wrong digit
  for (int i = 0; i < 4; i++) digitalWrite(DIGIT_PINS[i], DIGIT_OFF_LEVEL);

  byte pattern = getPattern(dispDigits[currentDigit], dp[currentDigit]);
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, pattern);
  digitalWrite(LATCH_PIN, HIGH);

  digitalWrite(DIGIT_PINS[currentDigit], DIGIT_ON_LEVEL);

  currentDigit = (currentDigit + 1) % 4;
}

// ---------------- Buttons ----------------

bool buttonPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastChangeTime = millis();
  }
  bool pressedEdge = false;
  if ((millis() - b.lastChangeTime) > DEBOUNCE_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) pressedEdge = true; // pulled LOW = pressed
    }
  }
  b.lastReading = reading;
  return pressedEdge;
}

// ---------------- EEPROM (alarm persistence) ----------------

void loadAlarmFromEEPROM() {
  int h = EEPROM.read(EE_ALARM_HOUR);
  int m = EEPROM.read(EE_ALARM_MIN);
  int en = EEPROM.read(EE_ALARM_EN);

  alarmHour   = (h >= 0 && h < 24) ? h : 7;
  alarmMinute = (m >= 0 && m < 60) ? m : 0;
  alarmEnabled = (en == 1);
}

void saveAlarmToEEPROM() {
  EEPROM.update(EE_ALARM_HOUR, alarmHour);
  EEPROM.update(EE_ALARM_MIN, alarmMinute);
  EEPROM.update(EE_ALARM_EN, alarmEnabled ? 1 : 0);
}
