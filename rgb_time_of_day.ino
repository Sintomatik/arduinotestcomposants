/*
 * RGB LED Color + Motor Controller + LCD
 * ========================================
 * One potentiometer controls BOTH the LED color (cold to hot)
 * and the motor speed. Button toggles LED on/off.
 * Tracks time using millis() and shows it on the LCD.
 *
 * SET YOUR CURRENT TIME BELOW before uploading!
 *
 * Wiring - RGB LED (common-cathode):
 *   Red   pin -> 220Ω resistor -> Arduino pin 10
 *   Green pin -> 220Ω resistor -> Arduino pin 9
 *   Blue  pin -> 220Ω resistor -> Arduino pin 11
 *   Common cathode (longest leg) -> GND
 *
 * Wiring - TC1602A LCD:
 *   Pin 1  (VSS)  -> GND
 *   Pin 2  (VDD)  -> 5V
 *   Pin 3  (V0)   -> Contrast potentiometer middle leg
 *   Pin 4  (RS)   -> Arduino pin 7
 *   Pin 5  (RW)   -> GND
 *   Pin 6  (EN)   -> Arduino pin 6
 *   Pin 11 (D4)   -> Arduino pin 5
 *   Pin 12 (D5)   -> Arduino pin 4
 *   Pin 13 (D6)   -> Arduino pin 8
 *   Pin 14 (D7)   -> Arduino pin 2
 *   Pin 15 (LED+) -> 5V
 *   Pin 16 (LED-) -> GND
 *
 * Wiring - TT Motor (via NPN transistor):
 *   Arduino pin 3 -> 1kΩ resistor -> Transistor BASE
 *   Motor red wire  -> 5V
 *   Motor black wire -> Transistor COLLECTOR
 *   Transistor EMITTER -> GND
 *
 * Wiring - Control Potentiometer (color + motor speed):
 *   Left leg  -> GND
 *   Middle leg -> Arduino pin A0
 *   Right leg  -> 5V
 *
 * Wiring - LED On/Off Button:
 *   One leg  -> Arduino pin 12
 *   Other leg -> GND
 *   (uses internal pull-up, no external resistor needed)
 */

#include <LiquidCrystal.h>

// ============ USER CONFIGURATION ============

// Set the current time when you upload the sketch
const int START_HOUR   = 15;  // 0-23 (24-hour format)
const int START_MINUTE = 17;  // 0-59

// Set to true if your RGB LED is common-anode
const bool COMMON_ANODE = false;

// PWM pins for each color channel
const int PIN_RED   = 10;
const int PIN_GREEN = 9;
const int PIN_BLUE  = 11;

// Motor PWM pin (must be PWM-capable)
const int PIN_MOTOR  = 3;

// Potentiometer for color + motor speed
const int PIN_POT = A0;

// Button to toggle LED on/off
const int PIN_BUTTON = 12;

// LCD pins: RS=7, EN=6, D4=5, D5=4, D6=8, D7=2
LiquidCrystal lcd(7, 6, 5, 4, 8, 2);

// ============ END CONFIGURATION ============

// Motor speed
uint8_t motorSpeed = 0;

// Time tracking
unsigned long millisAtStart = 0;
unsigned long startTimeSeconds = 0;

// LED toggle
bool ledOn = true;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 200;

// Color temperature gradient: cold (blue) to hot (red)
// Pot position maps to these keyframes smoothly
struct ColorKeyframe {
  uint8_t r, g, b;
  const char* label;
};

const ColorKeyframe colorRamp[] = {
  {   0,   0, 255, "Cold Blue " },  // Fully cold
  {   0, 100, 255, "Cool Cyan " },
  {   0, 200, 200, "Teal      " },
  {   0, 255, 100, "Cool Green" },
  { 100, 255,   0, "Lime      " },
  { 200, 255,   0, "Yellow-Grn" },
  { 255, 255,   0, "Yellow    " },
  { 255, 180,   0, "Warm Amber" },
  { 255, 100,   0, "Orange    " },
  { 255,  40,   0, "Hot Orange" },
  { 255,   0,   0, "Hot Red   " },  // Fully hot
};

const int NUM_COLORS = sizeof(colorRamp) / sizeof(colorRamp[0]);

void setup() {
  pinMode(PIN_RED,    OUTPUT);
  pinMode(PIN_GREEN,  OUTPUT);
  pinMode(PIN_BLUE,   OUTPUT);
  pinMode(PIN_MOTOR,  OUTPUT);
  pinMode(PIN_POT,    INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  analogWrite(PIN_MOTOR, 0);

  // Initialize LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Color+Motor Ctrl");
  lcd.setCursor(0, 1);
  lcd.print("Testing LED...");

  Serial.begin(9600);
  Serial.println(F("Color + Motor Controller"));
  Serial.println(F("Running startup LED test..."));

  // === STARTUP SELF-TEST ===
  setColor(0, 0, 0);
  delay(500);

  Serial.println(F("  Testing RED..."));
  lcd.setCursor(0, 1);
  lcd.print("Test: RED       ");
  setColor(255, 0, 0);
  delay(1000);
  setColor(0, 0, 0);
  delay(300);

  Serial.println(F("  Testing GREEN..."));
  lcd.setCursor(0, 1);
  lcd.print("Test: GREEN     ");
  setColor(0, 255, 0);
  delay(1000);
  setColor(0, 0, 0);
  delay(300);

  Serial.println(F("  Testing BLUE..."));
  lcd.setCursor(0, 1);
  lcd.print("Test: BLUE      ");
  setColor(0, 0, 255);
  delay(1000);
  setColor(0, 0, 0);
  delay(300);

  Serial.println(F("  Testing WHITE..."));
  lcd.setCursor(0, 1);
  lcd.print("Test: WHITE     ");
  setColor(255, 255, 255);
  delay(1000);
  setColor(0, 0, 0);
  delay(500);

  Serial.println(F("Test done!"));

  millisAtStart = millis();
  startTimeSeconds = (unsigned long)START_HOUR * 3600UL
                   + (unsigned long)START_MINUTE * 60UL;

  lcd.clear();
}

void loop() {
  // === Check button to toggle LED ===
  bool buttonState = digitalRead(PIN_BUTTON);
  if (buttonState == LOW && lastButtonState == HIGH
      && (millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    lastDebounceTime = millis();
    ledOn = !ledOn;
    Serial.print(F("LED: "));
    Serial.println(ledOn ? F("ON") : F("OFF"));
    if (!ledOn) setColor(0, 0, 0);
  }
  lastButtonState = buttonState;

  // === Read potentiometer ===
  int potValue = analogRead(PIN_POT);  // 0-1023

  // Motor speed: map pot to 0-255
  motorSpeed = map(potValue, 0, 1023, 0, 255);
  analogWrite(PIN_MOTOR, motorSpeed);

  // LED color: interpolate along cold-to-hot ramp
  float pos = potValue / 1023.0 * (NUM_COLORS - 1);  // 0.0 to 10.0
  int idx = (int)pos;
  if (idx >= NUM_COLORS - 1) idx = NUM_COLORS - 2;
  float t = pos - idx;

  uint8_t r = lerp8(colorRamp[idx].r, colorRamp[idx + 1].r, t);
  uint8_t g = lerp8(colorRamp[idx].g, colorRamp[idx + 1].g, t);
  uint8_t b = lerp8(colorRamp[idx].b, colorRamp[idx + 1].b, t);

  if (ledOn) {
    setColor(r, g, b);
  }

  // Get label for the nearest keyframe
  int nearest = (int)(pos + 0.5);
  if (nearest >= NUM_COLORS) nearest = NUM_COLORS - 1;

  // === Calculate time ===
  unsigned long elapsedSec = (millis() - millisAtStart) / 1000UL;
  unsigned long currentSec = (startTimeSeconds + elapsedSec) % 86400UL;
  int h = currentSec / 3600;
  int m = (currentSec % 3600) / 60;
  int s = currentSec % 60;

  // === Update LCD ===
  // Line 1: HH:MM:SS + color label
  lcd.setCursor(0, 0);
  if (h < 10) lcd.print('0');
  lcd.print(h);
  lcd.print(':');
  if (m < 10) lcd.print('0');
  lcd.print(m);
  lcd.print(':');
  if (s < 10) lcd.print('0');
  lcd.print(s);
  lcd.print(' ');
  // Trim label to 7 chars to fit
  char buf[8];
  strncpy(buf, colorRamp[nearest].label, 7);
  buf[7] = '\0';
  lcd.print(buf);

  // Line 2: Motor % + LED state
  lcd.setCursor(0, 1);
  lcd.print("Mtr:");
  int pct = map(motorSpeed, 0, 255, 0, 100);
  if (pct < 100) lcd.print(' ');
  if (pct < 10)  lcd.print(' ');
  lcd.print(pct);
  lcd.print("% LED:");
  lcd.print(ledOn ? "ON " : "OFF");
  lcd.print(" ");

  // Serial output every 2 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 2000UL) {
    lastPrint = millis();
    Serial.print(F("R="));
    Serial.print(r);
    Serial.print(F(" G="));
    Serial.print(g);
    Serial.print(F(" B="));
    Serial.print(b);
    Serial.print(F("  Motor="));
    Serial.print(pct);
    Serial.print(F("%  LED="));
    Serial.println(ledOn ? F("ON") : F("OFF"));
  }

  delay(100);
}

uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  return (uint8_t)(a + (b - a) * t);
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  if (COMMON_ANODE) {
    analogWrite(PIN_RED,   255 - r);
    analogWrite(PIN_GREEN, 255 - g);
    analogWrite(PIN_BLUE,  255 - b);
  } else {
    analogWrite(PIN_RED,   r);
    analogWrite(PIN_GREEN, g);
    analogWrite(PIN_BLUE,  b);
  }
}
