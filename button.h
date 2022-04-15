#ifdef CORE_TEENSY
  // Use the Joystick library for Teensy
  void ButtonStart() {
    // Use Joystick.begin() for everything that's not Teensy 2.0.
    #ifndef __AVR_ATmega32U4__
      Joystick.begin();
    #endif
    Joystick.useManualSend(true);
  }
  void ButtonPress(uint8_t button_num) {
    Joystick.button(button_num, 1);
  }
  void ButtonRelease(uint8_t button_num) {
    Joystick.button(button_num, 0);
  }
#else
  #include <Keyboard.h>
  // And the Keyboard library for Arduino
  void ButtonStart() {
    Keyboard.begin();
  }
  void ButtonPress(uint8_t button_num) {
    Keyboard.press('a' + button_num - 1);
  }
  void ButtonRelease(uint8_t button_num) {
    Keyboard.release('a' + button_num - 1);
  }
#endif
