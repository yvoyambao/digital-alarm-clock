## Digital Alarm-Clock
A fully custom digital alarm clock built on the Arduino Uno (ATmega328P), driving a 4-digit 7-segment display through one 74HC595 shift register, keeping accurate time with a RTC, and controlled entirely through four push buttons. 

## Demo
<img width="640" height="480" alt="IMG_0290" src="https://github.com/user-attachments/assets/38881980-69fd-41b6-b534-4391f58896ff" />
<img width="640" height="480" alt="IMG_0296" src="https://github.com/user-attachments/assets/55822bc1-05de-4b29-9b9d-62aff06d444c" />

### Adjusting clock hour and minute

https://github.com/user-attachments/assets/0906bb83-9a55-456b-aa7c-2ec08f7b245a

### Enabling/Disabling Alarm Mode

https://github.com/user-attachments/assets/44a5fe26-1ddb-4bcd-bd1f-9996d88c0d13

### Setting alarm hour and minute

https://github.com/user-attachments/assets/b6882a2e-0139-463f-a615-54ef008549f8

### Setting alarm time and turning off alarm

https://github.com/user-attachments/assets/831bda32-579c-44fa-aa14-81f3c07763f0


## Features
- 4-digit-segment display, multiplexed across all four digits using only one shift register
- DS1307 RTC for accurate time keeping across power loss.
- Settable time and alarm, both adjusted through the same two-stage hour/minute interface
- Alarm enable and disable toggle

## Hardware
- Arduino Uno (ATmega328P)
- 5641AS 4-digit 7-segment display
- 74HC595 shift register
- DS1307 RTC
- 4 push buttons
- Active buzzer
- 8 330 ohm resistors

### Arduino pin map

| Pin | Connects to |
|---|---|
| 4, 5, 6, 7 | Display digit-common pins (direct-wired, no transistors) |
| 8 | 74HC595 DS (data) |
| 9 | 74HC595 ST_CP (latch) |
| 10 | 74HC595 SH_CP (clock) |
| 3 | Buzzer signal |
| A0 | MODE button |
| A1 | UP button |
| A2 | ALARM button |
| A3 | STOP button |
| A4 / A5 | RTC SDA / SCL (I2C) |

### 74HC595 pinout

| Pin | Function | Connects to |
|---|---|---|
| 1 | Q1 (Seg B) | Display pin 7 |
| 2 | Q2 (Seg C) | Display pin 4 |
| 3 | Q3 (Seg D) | Display pin 2 |
| 4 | Q4 (Seg E) | Display pin 1 |
| 5 | Q5 (Seg F) | Display pin 10 |
| 6 | Q6 (Seg G) | Display pin 5 |
| 7 | Q7 (Seg DP) | Display pin 3 |
| 8 | GND | Ground rail |
| 9 | Serial out | Not used |
| 10 | MR (reset) | 5V |
| 11 | SH_CP (clock) | Arduino pin 10 |
| 12 | ST_CP (latch) | Arduino pin 9 |
| 13 | OE (enable) | GND |
| 14 | DS (data) | Arduino pin 8 |
| 15 | Q0 (Seg A) | Display pin 11 |
| 16 | VCC | 5V |

### 5641AS display pinout

| Pin | Function | Connects to |
|---|---|---|
| 1 | Segment E | 74HC595 pin 4 |
| 2 | Segment D | 74HC595 pin 3 |
| 3 | Segment DP | 74HC595 pin 7 |
| 4 | Segment C | 74HC595 pin 2 |
| 5 | Segment G | 74HC595 pin 6 |
| 6 | Digit 1 (leftmost) common | Arduino pin 7 |
| 7 | Segment B | 74HC595 pin 1 |
| 8 | Digit 2 common | Arduino pin 6 |
| 9 | Digit 3 common | Arduino pin 5 |
| 10 | Segment F | 74HC595 pin 5 |
| 11 | Segment A | 74HC595 pin 15 |
| 12 | Digit 4 (rightmost) common | Arduino pin 4 |

## Controls

| Button | In Normal mode | While editing |
|---|---|---|
| MODE | Enter Set Hour | Advance: Set Hour → Set Minute → commit to RTC |
| ALARM | Enter Set Alarm Hour | Advance: Set Alarm Hour → Set Alarm Minute → save |
| UP | Toggle alarm enabled/disabled | Increment the active field |
| STOP | Silence a sounding alarm | — |

## Getting it running

1. Wire everything per the tables above.
2. Install the **RTClib** library (by Adafruit) via the Arduino Library Manager.
3. Upload the sketch.


## Possible future improvements

- Custom PCB version

## License

MIT

