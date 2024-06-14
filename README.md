# Teejusb's FSR Guide
A complete software package for FSR dance pads - forked by ThereGoesMySanity to add support for LED matrixes.

## Features
- React web UI to customize sensitivity 
- Profiles & persistence
- Light support


## Requirements
- A [Teensy](https://www.pjrc.com/store/index.html) or Arduino
  - uses native keyboard library for Arduino and Joystick library for Teensy

## Hardware setup
Follow a guide like [fsr-pad-guide](https://github.com/Sereni/fsr-pad-guide) or [fsr](https://github.com/vlnguyen/itg-fsr/tree/master/fsr) to setup your Arduino/Teensy with FSRs.

## Firmware setup
1. Install [Arduino IDE](https://www.arduino.cc/en/software) (skip this if you're using OSX as it's included in Teensyduino)
1. Install [Teensyduino](https://www.pjrc.com/teensy/td_download.html) and get it connected to your Teensy and able to push firmware via Arduino IDE
1. In Arduino IDE, set the `Tools` > `USB Type` to `Serial + Keyboard + Mouse + Joystick` (or `Serial + Keyboard + Mouse`)
1. In Arduino IDE, set the `Tools` > `Board` to your microcontroller (e.g. `Teensy 4.0`)
1. In Arduino IDE, set the `Tools` > `Port` to select the serial port for the plugged in microcontroller (e.g. `COM5` or `/dev/something`)
1. Load [fsr.ino](./fsr.ino) in Arduino IDE.
1. By default, [A0-A3 are the pins](https://forum.pjrc.com/teensy40_pinout1.png) used for the FSR sensors in this software. If you aren't using these pins [alter the SensorState array](./fsr.ino#L437-L442)
1. Push the code to the board

### Testing and using the serial monitor
1. Open `Tools` > `Serial Monitor` to open the Serial Monitor
1. Within the serial monitor, enter `t` to show current thresholds.
1. You can change a sensor threshold by entering numbers, where the first number is the sensor (0-indexed) followed by the threshold value. For example, `3 180` would set the 4th sensor to a threshold of 180.  You can change these more easily in the UI later.
1. Enter `v` to get the current sensor values.
1. Putting pressure on an FSR, you should notice the values change if you enter `v` again while maintaining pressure.


## [UI has been moved to a separate repository](https://github.com/ThereGoesMySanity/FsrNet)
