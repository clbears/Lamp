# Luxora
2024
An encoder-controlled dimmable lamp driver. Rotating the encoder adjusts brightness with perceptual (gamma-corrected) easing and rotation-speed-based acceleration; clicking toggles power with smooth fade in/out. The last brightness level is remembered across power cycles via EEPROM.

## Features
- MCP4725 DAC drives an external dimmer/driver stage
- Auto-detects the DAC's I2C address and calibrates against measured supply voltage (`readVcc`)
- Gamma-corrected brightness curve for even perceived dimming
- Rotation-speed-based acceleration (fast spins move brightness further per detent)
- Smooth fade on power on/off
- Brightness position persisted to EEPROM and restored on boot
- Minimum-on threshold so a "power on" click never starts at near-zero brightness

## Hardware
- Adafruit MCP4725 12-bit DAC (I2C)
- Rotary encoder with push button on pins 3 (CLK), 4 (DT), 5 (SW)

## Libraries
- [Adafruit_MCP4725](https://github.com/adafruit/Adafruit_MCP4725)
