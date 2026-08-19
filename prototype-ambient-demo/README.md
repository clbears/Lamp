# Prototype: Ambient Light Demo

An early exploration of automatic ambient-light-sensing dimming, built on the same DAC-driven lamp hardware as the main [Luxora](../README.md) project. A photoresistor continuously measures room brightness and the lamp automatically dims or brightens to compensate, with smoothing and hysteresis to avoid flicker.

This was superseded by the manual encoder-control approach in the main project — kept here as a reference for the automatic-dimming idea.

## Features
- Photoresistor (A0) reading, exponentially smoothed
- Gamma-corrected perceptual dimming curve
- Hysteresis band around ambient thresholds to prevent oscillation at the edges
- Manual button toggle with smooth fade in/out
- Shutdown pin to fully cut the driver when off

## Hardware
- Adafruit MCP4725 12-bit DAC (I2C) driving an external lamp driver stage
- Photoresistor / light sensor on A0
- Push button on pin 2, driver shutdown on pin 4
