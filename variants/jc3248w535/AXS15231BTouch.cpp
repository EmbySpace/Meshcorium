#include "AXS15231BTouch.h"

#define TOUCH_ADDR       0x3B
#define POLL_MS          25
#define SWIPE_MIN_PX     60      // horizontal travel for a swipe
#define VSWIPE_MIN_PX    40      // vertical travel for a swipe
#define TAP_MAX_PX       20      // max travel for a tap

void AXS15231BTouch::begin() {
  // Wire is already started on PIN_BOARD_SDA / PIN_BOARD_SCL by the board
#if PIN_TOUCH_INT >= 0
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
#endif
#if PIN_TOUCH_RST >= 0
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(50);
#endif
  _ready = true;
}

bool AXS15231BTouch::read(int& x, int& y) {
  if (!_ready) return false;

  static const uint8_t read_cmd[11] = {
    0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00   // read 8 bytes (1 point)
  };
  uint8_t data[8] = {0};

  _wire->beginTransmission(TOUCH_ADDR);
  _wire->write(read_cmd, sizeof(read_cmd));
  if (_wire->endTransmission() != 0) return false;
  if (_wire->requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)sizeof(data)) != sizeof(data)) return false;
  for (size_t i = 0; i < sizeof(data); i++) data[i] = _wire->read();

  if (data[1] == 0 || data[1] > 1) return false;   // no touch / garbage

  int raw_x = ((data[2] & 0x0F) << 8) | data[3];
  int raw_y = ((data[4] & 0x0F) << 8) | data[5];
  if (raw_x > 500 || raw_y > 500) return false;

  // panel is natively 320x480 portrait; the display uses rotation 1 (landscape)
  x = raw_y;
  y = 320 - raw_x;
  if (x < 0) x = 0; if (x > 479) x = 479;
  if (y < 0) y = 0; if (y > 319) y = 319;
  return true;
}

AXS15231BTouch::Gesture AXS15231BTouch::gesture(int& x, int& y) {
  if ((long)(millis() - _next_poll) < 0) return NONE;
  _next_poll = millis() + POLL_MS;

  int tx, ty;
  if (read(tx, ty)) {
    if (!_down) { _down = true; _x0 = tx; _y0 = ty; }
    _x = tx; _y = ty;
    return NONE;
  }
  if (!_down) return NONE;

  // finger released: classify
  _down = false;
  int dx = _x - _x0, dy = _y - _y0;
  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > 2 * abs(dy)) {
    return dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
  }
  if (abs(dy) >= VSWIPE_MIN_PX && abs(dy) > 2 * abs(dx)) {
    return dy < 0 ? SWIPE_UP : SWIPE_DOWN;
  }
  if (abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX) {
    x = _x0; y = _y0;
    return TAP;
  }
  return NONE;
}
