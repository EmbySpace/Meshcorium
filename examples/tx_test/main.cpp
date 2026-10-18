// Transmitter test for the JC3248W535 + EBYTE E22-900M30S (env JC3248W535_tx_test).
//
// Sends 255-byte LoRa packets back to back (or at 10 % / 1 % duty) at a
// chosen power, to look at the PA output, current draw, heat and battery sag.
// NOT a MeshCore node: no mesh, no app interfaces.
//
// Safety / legal:
//  - never transmit without an antenna, the PA can be damaged
//  - continuous transmission at ~1 W exceeds the duty-cycle limits of the
//    868 MHz band in most countries; use it briefly, into a dummy load if possible
//  - packets use their own LoRa sync word, so MeshCore nodes do not receive or
//    repeat them; transmission stops by itself after TX_AUTO_STOP_MS

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "target.h"
#include "AXS15231BTouch.h"
#include "TxTestUI.h"
#include "UIScreens.h"

#ifndef TX_AUTO_STOP_MS
  #define TX_AUTO_STOP_MS (3UL * 60 * 1000)
#endif
#define TX_SYNC_WORD   0x2B          // not MeshCore's (0x12)
#define TX_PACKET_LEN  255
#define TX_POWER_MIN   -9
#define TX_POWER_MAX   22            // SX1262 limit; ~20 is the cleanest input for the E22 PA
#define REDRAW_MS      250

extern RADIO_CLASS radio;            // target.cpp

static AXS15231BTouch touch;
static uint16_t* frame;
static UIGfx* gfx;
static TxView view;
static bool dirty = true;
static bool radio_dirty = false;      // settings changed, apply before the next packet

static volatile bool tx_done = false;
static bool tx_busy = false;
static unsigned long tx_started, next_tx, run_started, last_redraw;
static uint8_t packet[TX_PACKET_LEN];

static void IRAM_ATTR onDio1() { tx_done = true; }

static void applyRadio() {
  const TxProfile& p = TX_PROFILES[view.profile_idx];
  radio.standby();
  radio.setSpreadingFactor(p.sf);
  radio.setBandwidth(p.bw);
  radio.setOutputPower(view.power_dbm);
  radio.setSyncWord(TX_SYNC_WORD);
}

static void startTx() {
  // payload: "TXTEST", counter, the rest pseudo-random so the spectrum looks realistic
  memcpy(packet, "TXTEST", 6);
  memcpy(packet + 6, &view.packets, 4);
  for (int i = 10; i < TX_PACKET_LEN; i++) packet[i] = (uint8_t)esp_random();
  tx_done = false;
  tx_started = millis();
  if (radio.startTransmit(packet, TX_PACKET_LEN) == RADIOLIB_ERR_NONE) tx_busy = true;
}

static void stop() {
  view.running = false;
  if (tx_busy) { radio.finishTransmit(); tx_busy = false; }
  radio.standby();
  dirty = true;
}

static void start() {
  view.packets = view.bytes = view.air_ms = view.elapsed_ms = 0;
  view.running = true;
  run_started = millis();
  next_tx = run_started;
  applyRadio();
  radio_dirty = false;
  dirty = true;
}

static void handleTouch() {
  int x, y;
  if (touch.gesture(x, y) != AXS15231BTouch::TAP) return;
  switch (TxTestUI::hitTest(view, x, y)) {
    case TXH_START_STOP:
      if (!view.radio_ok) break;
      if (view.running) stop(); else start();
      break;
    case TXH_POWER_DEC: if (view.power_dbm > TX_POWER_MIN) view.power_dbm--; break;
    case TXH_POWER_INC: if (view.power_dbm < TX_POWER_MAX) view.power_dbm++; break;
    case TXH_DUTY:      view.duty_idx = (view.duty_idx + 1) % TX_DUTY_COUNT; break;
    case TXH_PROFILE:
      view.profile_idx = (view.profile_idx + 1) % TX_PROFILE_COUNT;
      break;
    default: return;
  }
  radio_dirty = true;                // applied before the next packet
  dirty = true;
}

void setup() {
  Serial.begin(115200);
  board.begin();
  if (display.begin()) display.showSplash("tx-test");

  view.radio_ok = radio_init();      // shows "LoRa module error!" itself on failure
  view.power_dbm = 20;
  view.duty_idx = 0;
  view.profile_idx = 0;
  view.freq_mhz = LORA_FREQ;
  view.limit_ms = TX_AUTO_STOP_MS;
  view.batt_mv = board.getBattMilliVolts();

  if (view.radio_ok) {
    radio.setDio1Action(onDio1);
    applyRadio();
  } else {
    delay(4000);                     // leave the error on screen for a moment
  }

  frame = (uint16_t*)heap_caps_malloc(UI_W * UI_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (frame) gfx = new UIGfx(frame, UI_W, UI_H);
  touch.begin();
}

void loop() {
  handleTouch();
  unsigned long now = millis();

  if (view.running) {
    view.elapsed_ms = now - run_started;

    if (tx_busy && tx_done) {
      radio.finishTransmit();
      tx_busy = false;
      uint32_t t = now - tx_started;
      view.last_packet_ms = t;
      view.air_ms += t;
      view.packets++;
      view.bytes += TX_PACKET_LEN;
      // duty: wait so that air time / total time = duty
      uint8_t duty = TX_DUTY_PCT[view.duty_idx];
      next_tx = now + (duty >= 100 ? 0 : t * (100 - duty) / duty);
      dirty = true;
    }
    if (view.elapsed_ms >= view.limit_ms) stop();
    else if (!tx_busy && (long)(now - next_tx) >= 0) {
      if (radio_dirty) { applyRadio(); radio_dirty = false; }
      startTx();
    }
  }

  if (now - last_redraw >= REDRAW_MS) {
    last_redraw = now;
    uint16_t mv = board.getBattMilliVolts();
    if (mv != view.batt_mv) { view.batt_mv = mv; dirty = true; }
    if (view.running) dirty = true;  // elapsed time ticks
  }
  if (dirty && gfx) {
    dirty = false;
    TxTestUI::render(*gfx, view);
    display.present(frame);
  }
}
