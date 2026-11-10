// Smart Bin: a touchless rubbish bin with fill-level monitoring.
//
// - The front ultrasonic sensor sees a hand: the servo opens the lid, and closes it
//   a few seconds after the hand has gone.
// - The inside ultrasonic sensor measures how full the bin is: green / yellow / red LEDs,
//   and when it is full the lid stays closed and the buzzer beeps.
// - Everything is non-blocking (no delay() in the loop), so the bin keeps measuring and
//   reacting while the lid moves.
// - Status is printed on the serial monitor (9600 baud) as CSV for logging.
//
// Wiring: see README.md. Logic (filter, lid, fill level): bin_logic.h, unit-tested on PC.

#include <Servo.h>
#include "bin_logic.h"

// ---------- pins ----------
const uint8_t FRONT_TRIG = 9, FRONT_ECHO = 8;   // hand detection (outside, front)
const uint8_t INSIDE_TRIG = 11, INSIDE_ECHO = 10; // fill level (under the lid, looking down)
const uint8_t SERVO_PIN = 6;
const uint8_t LED_GREEN = 3, LED_YELLOW = 4, LED_RED = 5;
const uint8_t BUZZER = 7;

// ---------- settings (adjust to your bin) ----------
const int HAND_CM = 25;                  // a hand closer than this opens the lid
const uint32_t HOLD_MS = 3000;           // lid stays open this long after the hand leaves
const uint32_t MIN_OPEN_MS = 1500;       // never close sooner than this after opening
const int BIN_EMPTY_CM = 40;             // inside sensor -> bottom of the empty bin
const int BIN_FULL_CM = 8;               // inside sensor -> rubbish when the bin is full
const uint8_t LID_OPEN_ANGLE = 0;        // servo angles for your mounting
const uint8_t LID_CLOSED_ANGLE = 90;
const uint32_t MEASURE_EVERY_MS = 60;    // HC-SR04 needs ~60 ms between pings
const uint32_t FILL_EVERY_MS = 1000;     // the fill level changes slowly
const uint32_t REPORT_EVERY_MS = 2000;

Servo lid;
MedianFilter<5> frontFilter;
MedianFilter<5> insideFilter;
LidController controller(HAND_CM, HOLD_MS, MIN_OPEN_MS);
FillGauge gauge(BIN_EMPTY_CM, BIN_FULL_CM);

uint32_t lastMeasure = 0, lastFill = 0, lastReport = 0, lastBeep = 0;

// One ping. Returns the distance in cm, or -1 if no echo came back in time
// (pulseIn's timeout keeps the loop from freezing for a full second when nothing is in front).
int readDistanceCm(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 25000UL);  // 25 ms ≈ 4 m round trip
  if (us == 0) return -1;
  return (int)(us * 0.0343 / 2);  // speed of sound 343 m/s, there and back
}

void showLevel(FillGauge::Level level) {
  digitalWrite(LED_GREEN, level == FillGauge::LOW_LEVEL || level == FillGauge::MEDIUM_LEVEL);
  digitalWrite(LED_YELLOW, level == FillGauge::MEDIUM_LEVEL || level == FillGauge::HIGH_LEVEL);
  digitalWrite(LED_RED, level == FillGauge::HIGH_LEVEL || level == FillGauge::FULL_LEVEL);
}

void setup() {
  pinMode(FRONT_TRIG, OUTPUT);
  pinMode(FRONT_ECHO, INPUT);
  pinMode(INSIDE_TRIG, OUTPUT);
  pinMode(INSIDE_ECHO, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  lid.attach(SERVO_PIN);
  lid.write(LID_CLOSED_ANGLE);
  Serial.begin(9600);
  Serial.println(F("time_ms,front_cm,fill_pct,level,lid,opens"));
}

void loop() {
  uint32_t now = millis();

  // fill level first: a full bin must not open
  if (now - lastFill >= FILL_EVERY_MS) {
    lastFill = now;
    insideFilter.add(readDistanceCm(INSIDE_TRIG, INSIDE_ECHO));
    showLevel(gauge.update(insideFilter.median()));
  }

  if (now - lastMeasure >= MEASURE_EVERY_MS) {
    lastMeasure = now;
    frontFilter.add(readDistanceCm(FRONT_TRIG, FRONT_ECHO));
    int front = frontFilter.median();
    if (controller.update(front, gauge.full(), now)) {
      lid.write(controller.state() == LidController::OPEN ? LID_OPEN_ANGLE : LID_CLOSED_ANGLE);
    }
    // someone reaches for a full bin: two short beeps, at most every 3 s
    if (gauge.full() && front > 0 && front <= HAND_CM && now - lastBeep > 3000) {
      lastBeep = now;
      tone(BUZZER, 2000, 150);
    }
  }

  if (now - lastReport >= REPORT_EVERY_MS) {
    lastReport = now;
    const char* levels[] = {"low", "medium", "high", "full"};
    Serial.print(now); Serial.print(',');
    Serial.print(frontFilter.median()); Serial.print(',');
    Serial.print(gauge.percent()); Serial.print(',');
    Serial.print(levels[gauge.level()]); Serial.print(',');
    Serial.print(controller.state() == LidController::OPEN ? "open" : "closed"); Serial.print(',');
    Serial.println(controller.opens());
  }
}
