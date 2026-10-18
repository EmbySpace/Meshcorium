#pragma once

// Touch controller of the JC3248W535 (built into the AXS15231B, I2C 0x3B on
// the board I2C bus SDA=IO4 / SCL=IO8). Its reset is tied to the board reset
// and the interrupt line is not needed (polled), so no extra GPIOs are used.
// NOTE: IO11 / IO12, given as touch INT / RST in some example code, are the
// TF card's MOSI / SCK on this board - do not drive them here.
// Single touch point, coordinates in the landscape 480x320 frame used by
// AXS15231BDisplay (rotation 1).
//
// gesture() turns raw touches into taps and swipes; call it often
// (it rate-limits the I2C polling itself).

#include <Arduino.h>
#include <Wire.h>

#ifndef PIN_TOUCH_RST
  #define PIN_TOUCH_RST -1
#endif
#ifndef PIN_TOUCH_INT
  #define PIN_TOUCH_INT -1
#endif

class AXS15231BTouch {
public:
  enum Gesture : uint8_t { NONE, TAP, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN };

  AXS15231BTouch(TwoWire& wire = Wire) : _wire(&wire), _ready(false), _down(false),
      _x0(0), _y0(0), _x(0), _y(0), _next_poll(0) { }

  void begin();

  // raw read: true while a finger is on the screen
  bool read(int& x, int& y);

  // returns a finished gesture (on finger release); tap position in x, y
  Gesture gesture(int& x, int& y);

private:
  TwoWire* _wire;
  bool _ready;
  bool _down;
  int _x0, _y0, _x, _y;
  unsigned long _next_poll;
};
