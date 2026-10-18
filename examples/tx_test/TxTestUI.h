#pragma once

// Screen of the JC3248W535 transmitter test firmware (env JC3248W535_tx_test).
// Pure drawing + hit-testing, previewed on a PC like the main UI.
//
//   [ TX start/stop, packets, kB ][ air time %, elapsed  ]
//   [ power  -  20 dBm  +        ][ duty     ][ profile   ]

#include "UIGfx.h"

#define TX_DUTY_COUNT 3
extern const uint8_t TX_DUTY_PCT[TX_DUTY_COUNT];   // 100, 10, 1

struct TxProfile {
  const char* name;      // "SF7 BW250"
  uint8_t sf;
  float bw;              // kHz
};
#define TX_PROFILE_COUNT 2
extern const TxProfile TX_PROFILES[TX_PROFILE_COUNT];

struct TxView {
  bool running;
  bool radio_ok;
  uint32_t packets;
  uint32_t bytes;
  uint32_t air_ms;           // total time on air
  uint32_t elapsed_ms;       // since start
  uint32_t limit_ms;         // auto stop
  uint32_t last_packet_ms;   // air time of one packet
  int8_t power_dbm;          // SX1262 output (E22 PA input)
  uint8_t duty_idx;
  uint8_t profile_idx;
  float freq_mhz;
  uint16_t batt_mv;
};

enum TxHit : uint8_t { TXH_NONE, TXH_START_STOP, TXH_POWER_DEC, TXH_POWER_INC, TXH_DUTY, TXH_PROFILE };

namespace TxTestUI {
  void render(UIGfx& g, const TxView& v);
  TxHit hitTest(const TxView& v, int x, int y);
}
