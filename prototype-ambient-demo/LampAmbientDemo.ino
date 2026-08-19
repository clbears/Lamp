#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <math.h>

Adafruit_MCP4725 dac;

const int shutdownPin = 4;
const int buttonPin = 2;
const int lightPin = A0;

const int dacMax = 2457;
const int steps = 1000;
const float gamma = 2.2;

// Ambient light sensing bounds
const int ambientLow = 100;
const int ambientHigh = 300;
const float hysteresisMargin = 5.0;  // in ambient units (e.g. A0 reading)

// DAC control range where the driver reacts
const int dacMin = 2300;
const int dacMaxEffective = 2700;

bool ledIsOff = false;
bool buttonPressed = false;
bool lastButtonState = HIGH;

unsigned long lastReadTime = 0;
int avgSensor = 512;
int currentOutput = 0;
float smoothedNorm = 0.0;

void setup() {
  pinMode(shutdownPin, OUTPUT);
  digitalWrite(shutdownPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  Serial.begin(9600);
  dac.begin(0x62);

  dac.setVoltage(dacMin, false); // Start ON
  Serial.println("STATE: MANUAL ON (startup)");
}

void loop() {
  handleButton();

  if (!ledIsOff) {
    if (millis() - lastReadTime > 30) {
      lastReadTime = millis();

      int raw = analogRead(lightPin);
      avgSensor = (avgSensor * 7 + raw) / 8;

      // Normalized ambient reading (0.0 to 1.0)
      float norm = (float)(avgSensor - ambientLow) / (ambientHigh - ambientLow);
      norm = constrain(norm, 0.0, 1.0);

      // Apply smoothing to norm
      smoothedNorm = (smoothedNorm * 0.9) + (norm * 0.1);  // smoothing factor

      // Apply easing for perceptual dimming
      float eased = pow(smoothedNorm, gamma);

      // Map to DAC range
      int target = map(eased * 1000, 0, 1000, dacMin, dacMaxEffective);

      // Apply hysteresis clamp around edge
      if (avgSensor > ambientHigh + hysteresisMargin) {
        target = dacMaxEffective; // fully off
      } else if (avgSensor < ambientLow - hysteresisMargin) {
        target = dacMin; // fully on
      }

      if (abs(target - currentOutput) > 2) {
        currentOutput = target;
        dac.setVoltage(currentOutput, false);
      }

      Serial.print("STATE: ON\t");
      Serial.print("raw: "); Serial.print(raw);
      Serial.print("\tavg: "); Serial.print(avgSensor);
      Serial.print("\tnorm: "); Serial.print(smoothedNorm, 2);
      Serial.print("\tdac: "); Serial.println(currentOutput);
    }
  } else {
    Serial.println("STATE: OFF");
    delay(300);
  }
}

void handleButton() {
  bool currentButtonState = digitalRead(buttonPin);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    buttonPressed = true;
  }
  lastButtonState = currentButtonState;

  if (buttonPressed) {
    buttonPressed = false;

    if (ledIsOff) {
      fadeOn();
      ledIsOff = false;
      Serial.println("STATE: MANUAL ON (button)");
    } else {
      fadeOff();
      ledIsOff = true;
      Serial.println("STATE: OFF (button)");
    }
  }
}

void fadeOn() {
  for (int i = dacMaxEffective; i >= dacMin; i--) {
    dac.setVoltage(i, false);
    delay(3);
  }
  currentOutput = dacMin;
}

void fadeOff() {
  for (int i = dacMin; i <= dacMaxEffective; i++) {
    dac.setVoltage(i, false);
    delay(3);
  }
  currentOutput = dacMaxEffective;
}
