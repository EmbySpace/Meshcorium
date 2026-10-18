#pragma once

// UI task for the JC3248W535 (480x320 landscape, touch).
//
// Collects the firmware state into a UIModel and lets UIScreens draw it with
// the anti-aliased UIGfx renderer into a PSRAM frame, which is sent to the
// panel only when the model changed. Pages are switched by a horizontal swipe
// or by tapping the left / right half of the header; the screen switches off
// after the configured timeout and wakes on a tap.
//
// Layout and look: UIScreens.cpp. PC preview: tools/run_preview.sh

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "UIScreens.h"
#include "ChatsController.h"
#include "InterfacesController.h"
#include "WifiManager.h"
#include "RepeaterController.h"

#ifndef UI_TZ_OFFSET_MIN
  #define UI_TZ_OFFSET_MIN 180      // local time = UTC + this (default Moscow, UTC+3)
#endif

class AXS15231BDisplay;

class UITask : public AbstractUITask {
  #define UI_BATT_SAMPLES 5         // battery history, one sample per BATT_MS

  AXS15231BDisplay* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;

  uint16_t* _frame;                 // 480x320 RGB565 in PSRAM
  UIGfx* _gfx;
  UIModel _shown;
  bool _dirty;

  uint8_t _page;
  int _unread;
  uint16_t _batt_mv;
  uint16_t _batt_hist[UI_BATT_SAMPLES];
  uint8_t _batt_hist_len;
  unsigned long _next_status, _next_batt;

  // settings (persisted in NVS)
  uint8_t _timeout_idx;
  uint8_t _brightness;              // percent
  unsigned long _last_activity;
  bool _wake_request;

  // TF card (SDMMC, 1-bit), checked at boot and on demand from the settings page
  UISdState _sd;
  uint32_t _sd_total_mb, _sd_used_mb;
  const char* _sd_type;
  void checkSdCard();

  ChatsController _chats;           // page 2
  WifiManager _wifi;
  InterfacesController _links;      // page 3
  RepeaterController _rep;          // page 7

  void fillModel(UIModel& m);
  UIBattTrend battTrend() const;
  void sampleBattery();
  void render();
  void handleTouch();
  void applyAction(UIAction a);
  void loadSettings();
  void saveSettings();
  void wake();
  void checkScreenTimeout();

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial)
      : AbstractUITask(board, serial), _display(NULL), _sensors(NULL), _node_prefs(NULL),
        _frame(NULL), _gfx(NULL), _dirty(false), _page(UI_PAGE_HOME), _unread(0), _batt_mv(0),
        _batt_hist_len(0), _next_status(0), _next_batt(0), _timeout_idx(0), _brightness(100),
        _last_activity(0), _wake_request(false), _sd(UI_SD_NONE), _sd_total_mb(0), _sd_used_mb(0),
        _sd_type(NULL) {
    memset(&_shown, 0, sizeof(_shown));
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // from AbstractUITask
  void msgRead(int msgcount) override { _unread = msgcount; }
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override {
    _unread = msgcount;
    _wake_request = true;    // light the screen up for a new message
  }
  void notify(UIEventType t = UIEventType::none) override { }
  void onContactMessage(const ContactInfo& from, uint32_t sender_timestamp, const char* text) override {
    _chats.onContactMessage(from, sender_timestamp, text);
  }
  void onChannelMessage(int channel_idx, uint32_t timestamp, const char* text) override {
    _chats.onChannelMessage(channel_idx, timestamp, text);
  }
  void onAckReceived(uint32_t ack) override { _chats.onAck(ack); }
  void loop() override;
};
