#include <inttypes.h>

#if !defined(__AVR_ATmega32U4__) && !defined(__AVR_ATmega328P__) && \
    !defined(__AVR_ATmega1280__) && !defined(__AVR_ATmega2560__)
  #define CAN_AVERAGE
#endif

#if defined(_SFR_BYTE) && defined(_BV) && defined(ADCSRA)
  #define CLEAR_BIT(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
  #define SET_BIT(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif



// Default threshold value for each of the sensors.
const int16_t kDefaultThreshold = 1000;
// Max window size for both of the moving averages classes.
const size_t kWindowSize = 50;
// Baud rate used for Serial communication. Technically ignored by Teensys.
const long kBaudRate = 115200;
// Max number of sensors per panel.
// NOTE(teejusb): This is arbitrary, if you need to support more sensors
// per panel then just change the following number.
const size_t kMaxSharedSensors = 2;
// Button numbers should start with 1 (Button0 is not a valid Joystick input).
// Automatically incremented when creating a new SensorState.
uint8_t curButtonNum = 1;

// Defines the sensor collections and sets the pins for them appropriately.
//
// If you want to use multiple sensors in one panel, you will want to share
// state across them. In the following example, the first and second sensors
// share state. The maximum number of sensors that can be shared for one panel
// is controlled by the kMaxSharedSensors constant at the top of this file, but
// can be modified as needed.
//
// SensorState state1;
// Sensor kSensors[] = {
//   Sensor(A0, &state1),
//   Sensor(A1, &state1),
//   Sensor(A2),
//   Sensor(A3),
//   Sensor(A4),
// };

#include "button.h"
#include "MovingAverage.h"
#include "SensorState.h"
#include "Sensor.h"

SensorState kStates[] = { SensorState(), SensorState(), SensorState(), SensorState() };
Sensor kSensors[] = {
  Sensor(A0, &kStates[0]),
  Sensor(A1, &kStates[0]),
  Sensor(A2, &kStates[1]),
  Sensor(A3, &kStates[1]),
  Sensor(A4, &kStates[2]),
  Sensor(A5, &kStates[2]),
  Sensor(A6, &kStates[3]),
  Sensor(A7, &kStates[3])
};
const size_t kNumSensors = sizeof(kSensors)/sizeof(Sensor);

#include "SerialProcessor.h"
#include "LedPanel.h"

SerialProcessor serialProcessor;
LedPanel panel(kStates);
// Timestamps are always "unsigned long" regardless of board type So don't need
// to explicitly worry about the widths.
unsigned long lastSend = 0;
// loopTime is used to estimate how long it takes to run one iteration of
// loop().
long loopTime = -1;

void setup() {
  serialProcessor.Init(kBaudRate);
  ButtonStart();
  for (size_t i = 0; i < kNumSensors; ++i) {
    // Button numbers should start with 1.
    kSensors[i].Init(i + 1);
  }

  panel.Init();
  
  #if defined(CLEAR_BIT) && defined(SET_BIT)
	  // Set the ADC prescaler to 16 for boards that support it,
	  // which is a good balance between speed and accuracy.
	  // More information can be found here: http://www.gammon.com.au/adc
	  SET_BIT(ADCSRA, ADPS2);
	  CLEAR_BIT(ADCSRA, ADPS1);
	  CLEAR_BIT(ADCSRA, ADPS0);
  #endif
}

void loop() {
  unsigned long startMicros = micros();
  // We only want to send over USB every millisecond, but we still want to
  // read the analog values as fast as we can to have the most up to date
  // values for the average.
  static bool willSend;
  // Separate out the initialization and the update steps for willSend.
  // Since willSend is static, we want to make sure we update the variable
  // every time we loop.
  willSend = (loopTime == -1 || startMicros - lastSend + loopTime >= 1000);

  serialProcessor.CheckAndMaybeProcessData();

  for (size_t i = 0; i < kNumSensors; ++i) {
    kSensors[i].EvaluateSensor(willSend);
  }

  panel.Update();

  if (willSend) {
    lastSend = startMicros;
    #ifdef CORE_TEENSY
        Joystick.send_now();
    #endif
  }

  if (loopTime == -1) {
    loopTime = micros() - startMicros;
  }
}
