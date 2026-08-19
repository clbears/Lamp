#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <EEPROM.h>
#include <math.h>

// —— Pin Definitions ——
const int ENCODER_CLK = 3;
const int ENCODER_DT  = 4;
const int ENCODER_SW  = 5;

// —— DAC & Control Voltages ——
Adafruit_MCP4725 dac;
const int   DAC_RES   = 4095;       // 12-bit resolution
const float VCTRL_MIN = 0.7f;      // voltage → full ON
const float VCTRL_MAX = 2.75f;      // voltage → full OFF

// —— Encoder & Acceleration Settings ——
const int   DETENTS_PER_REV         = 40;    // detents per rotation
const int   ROTATIONS_PER_FULL_BAND = 2;     // rotations → full scale
const float GAMMA                   = 2.2f;  // perceptual exponent
const float ACCEL_K                 = 500000.0f; // accel constant (tweakable)
const float ACCEL_MIN               = 0.5f;  // min multiplier
const float ACCEL_MAX               = 9.0f;  // max multiplier

// —— Button Settings ——
const float MIN_ON_THRESHOLD        = 0.50f; // 50% minimum when clicking on

// —— Timing & Smoothing Settings ——
const unsigned long SWITCH_DEBOUNCE_MS   = 50;   // ms button debounce
const unsigned long ROTATE_DEBOUNCE_MS   = 20;   // ms between detents
const unsigned long FADE_ON_MS           = 500;  // ms fade-on duration
const unsigned long FADE_OFF_MS          = 300;  // ms fade-off duration
const float         SMOOTH_ROTATE_ALPHA  = 0.5f; // smoothing factor for rotation

// —— EEPROM Address ——
const int EEPROM_ADDR_IDX = 0;

// —— Runtime State ——
float         brightnessPos     = 0.5f;   // target brightness 0.0–1.0
float         brightnessCurrent = 0.5f;   // smoothed brightness
bool          isPowered = true;

bool          buttonWasDown     = false;
unsigned long lastSwitchTime    = 0;
unsigned long lastRotateTime    = 0;
unsigned long lastDetentMicros  = 0;
int           lastClkState;
int           rawMin, rawMax;
float         supplyVcc;

// fade state
bool          fading           = false;
unsigned long fadeStartTime    = 0;
unsigned long fadeDuration     = 0;
float         fadeStartLevel   = 0.0f;
float         fadeTargetLevel  = 0.0f;

// serial spam guard
int           lastPrintedRaw   = -1;

// —— Internal Helpers ——
long readVcc() {
  ADMUX  = _BV(REFS0)|_BV(MUX3)|_BV(MUX2)|_BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC));
  return (1100L * 1023L) / ADC;
}

uint8_t scanI2C() {
  Wire.begin();
  for (uint8_t addr=1; addr<127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0;
}

int calcDAC(float pos) {
  float t = constrain(pos, 0.0f, 1.0f);
  float eased = pow(t, GAMMA);
  return rawMax - (rawMax - rawMin) * eased + 0.5f;
}

float rawToVolt(int raw) {
  return raw * (supplyVcc / DAC_RES);
}

void savePosition() {
  EEPROM.update(EEPROM_ADDR_IDX, uint8_t(round(brightnessPos * 255)));
}

void loadPosition() {
  uint8_t v = EEPROM.read(EEPROM_ADDR_IDX);
  brightnessPos     = constrain(v / 255.0f, 0.0f, 1.0f);
  brightnessCurrent = brightnessPos;
}

// —— Fade Routines ——
void startFade(bool powerOn) {
  // enforce minimum on when turning on
  if (powerOn) {
    if (brightnessPos < MIN_ON_THRESHOLD) brightnessPos = MIN_ON_THRESHOLD;
  }
  fadeStartTime  = millis();
  fadeStartLevel = brightnessCurrent;
  if (powerOn) {
    fadeDuration    = FADE_ON_MS;
    fadeTargetLevel = brightnessPos;
    isPowered       = true;
  } else {
    fadeDuration    = FADE_OFF_MS;
    fadeTargetLevel = 0.0f;
  }
  fading = true;
}

void updateFade() {
  if (!fading) return;
  unsigned long now = millis();
  float t = float(now - fadeStartTime) / fadeDuration;
  if (t >= 1.0f) {
    brightnessCurrent = fadeTargetLevel;
    fading            = false;
    if (fadeTargetLevel == 0.0f) isPowered = false;
  } else {
    float e = (fadeTargetLevel > fadeStartLevel) ? (t * t) : (t * (2 - t));
    brightnessCurrent = fadeStartLevel + (fadeTargetLevel - fadeStartLevel) * e;
  }
}

// —— Apply to DAC ——
void applyDAC() {
  int code = isPowered ? calcDAC(brightnessCurrent) : rawMax;
  if (code != lastPrintedRaw) {
    dac.setVoltage(code, false);
    float v = rawToVolt(code);
    Serial.print(isPowered ? "ON  " : "OFF ");
    Serial.print("B="); Serial.print(brightnessCurrent * 100, 1);
    Serial.print("% V="); Serial.println(v, 3);
    lastPrintedRaw = code;
  }
}

// —— Setup ——
void setup() {
  Serial.begin(115200);
  Wire.begin();
  uint8_t addr = scanI2C();
  if (!addr) { Serial.println("ERROR: no I2C device!"); while(1); }
  dac.begin(addr);
  supplyVcc = readVcc() / 1000.0f;
  rawMin    = int((VCTRL_MIN / supplyVcc) * DAC_RES + 0.5f);
  rawMax    = int((VCTRL_MAX / supplyVcc) * DAC_RES + 0.5f);
  Serial.print("Vcc="); Serial.print(supplyVcc); Serial.println(" V");
  Serial.print("rawMin="); Serial.print(rawMin);
  Serial.print(" rawMax="); Serial.println(rawMax);
  loadPosition();
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT,  INPUT_PULLUP);
  pinMode(ENCODER_SW,  INPUT_PULLUP);
  lastClkState     = digitalRead(ENCODER_CLK);
  lastDetentMicros = micros();
  isPowered        = false;
  fading           = false;
  applyDAC();
  Serial.println("--- Luxora Ready ---");
}

// —— Main Loop ——
void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  // Encoder rotation (debounced + accel)
  int clk = digitalRead(ENCODER_CLK);
  if (clk != lastClkState && clk == LOW && nowMs - lastRotateTime > ROTATE_DEBOUNCE_MS) {
    lastRotateTime   = nowMs;
    unsigned long dt = nowUs - lastDetentMicros;
    lastDetentMicros = nowUs;
    float speed      = constrain(ACCEL_K / dt, ACCEL_MIN, ACCEL_MAX);
    bool cw          = (digitalRead(ENCODER_DT) != clk);
    float base       = 1.0f / (DETENTS_PER_REV * ROTATIONS_PER_FULL_BAND);
    brightnessPos    = constrain(brightnessPos + (cw ? base * speed : -base * speed), 0.0f, 1.0f);
    savePosition();
    if (!isPowered) {
      // if turning from off by scroll, fade on
      startFade(true);
    } else {
      // instant apply while on
      brightnessCurrent = brightnessPos;
      int code = calcDAC(brightnessCurrent);
      Serial.print("Adjust → "); Serial.print(brightnessPos * 100, 1);
      Serial.print("% x"); Serial.print(speed, 2);
      Serial.print(" V="); Serial.println(rawToVolt(code), 3);
      dac.setVoltage(code, false);
      lastPrintedRaw = code;
    }
  }
  lastClkState = clk;

  // Switch toggle → on/off (with threshold)
  bool down = digitalRead(ENCODER_SW) == LOW;
  if (down && !buttonWasDown && nowMs - lastSwitchTime > SWITCH_DEBOUNCE_MS) buttonWasDown = true;
  if (!down && buttonWasDown) {
    buttonWasDown   = false;
    lastSwitchTime  = nowMs;
    startFade(!isPowered);
    Serial.println(isPowered ? "Click→OFF" : "Click→ON");
  }

  // update fade & apply
  updateFade();
  applyDAC();
}
