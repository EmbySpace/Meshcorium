#pragma once

#include <helpers/ESP32Board.h>

// Battery voltage divider on the JC3248W535 (battery connector P5 -> IO5):
//   BAT+ --- R26 33k ---+--- R27 100k --- GND
//                       +--- R25 0R --- IO5
// V(IO5) = Vbat * 100 / 133  ->  Vbat = V(IO5) * 133 / 100
// (values from the board schematic, see done.land; verify against a multimeter
//  and adjust VBAT_DIV_NUM / VBAT_DIV_DEN in platformio.ini if needed)
#ifndef VBAT_DIV_NUM
  #define VBAT_DIV_NUM 133
#endif
#ifndef VBAT_DIV_DEN
  #define VBAT_DIV_DEN 100
#endif

class JC3248W535Board : public ESP32Board {
public:
  // ESP32Board::getBattMilliVolts() assumes a 1:2 divider; this board uses 100:133.
  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) {
      mv += analogReadMilliVolts(PIN_VBAT_READ);   // calibrated, default 11 dB attenuation
    }
    mv /= 8;
    return (uint16_t)(mv * VBAT_DIV_NUM / VBAT_DIV_DEN);
  #else
    return 0;
  #endif
  }

  const char* getManufacturerName() const override {
    return "Guition JC3248W535";
  }
};
