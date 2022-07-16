#include <ADC.h>
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

ADC *adc = new ADC();

SensorState kStates[] = { SensorState(1), SensorState(2), SensorState(4), SensorState(3) };
Sensor kSensors[] = {
  Sensor(adc, A0, &kStates[0]),
  Sensor(adc, A1, &kStates[0]),
  Sensor(adc, A2, &kStates[1]),
  Sensor(adc, A3, &kStates[1]),
  Sensor(adc, A4, &kStates[2]),
  Sensor(adc, A5, &kStates[2]),
  Sensor(adc, A6, &kStates[3]),
  Sensor(adc, A7, &kStates[3])
};
const size_t kNumSensors = sizeof(kSensors)/sizeof(Sensor);

#include "LedPanel.h"
LedPanel panel(kStates);

#include "SerialProcessor.h"
SerialProcessor serialProcessor;
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

  adc->adc0->setAveraging(16);
  adc->adc1->setAveraging(16);
  
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
  static int count;
  // Separate out the initialization and the update steps for willSend.
  // Since willSend is static, we want to make sure we update the variable
  // every time we loop.
  willSend = (loopTime == -1 || startMicros - lastSend + loopTime >= 1000);

  serialProcessor.CheckAndMaybeProcessData();

  for (size_t i = 0; i < kNumSensors; ++i) {
    kSensors[i].EvaluateSensor(willSend);
  }
  count++;


  if (willSend) {
    //Serial.println(startMicros - lastSend + loopTime);
    //Serial.println(count);
    count = 0;
    lastSend = startMicros;
    #ifdef CORE_TEENSY
        Joystick.send_now();
    #endif
  }
  
  panel.Update();

  if (loopTime == -1) {
    loopTime = micros() - startMicros;
  }
}
