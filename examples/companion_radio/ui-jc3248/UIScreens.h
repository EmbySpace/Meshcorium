#pragma once

// Page layouts of the JC3248W535 UI (480x320), "flat tiles" style.
//
// Everything a page shows comes from UIModel, filled by UITask from the
// firmware state. Rendering and touch hit-testing live here, free of any
// firmware / Arduino dependency, so tools/preview.cpp can render the exact
// same pages to PNG on a PC.

#include "UIGfx.h"

struct UIChatsView;
struct UIIfView;
struct UIRepView;

#define UI_W 480
#define UI_H 320

enum UILink : uint8_t { UI_LINK_ABSENT, UI_LINK_OFF, UI_LINK_READY, UI_LINK_CLIENT };
enum UIBattTrend : uint8_t { UI_BATT_UNKNOWN, UI_BATT_CHARGING, UI_BATT_DISCHARGING, UI_BATT_FULL, UI_BATT_NONE };
enum UISdState : uint8_t { UI_SD_NONE, UI_SD_OK };

enum UIPage : uint8_t { UI_PAGE_HOME, UI_PAGE_CHATS, UI_PAGE_LINKS, UI_PAGE_POWER, UI_PAGE_RADIO, UI_PAGE_SETTINGS, UI_PAGE_REPEATER, UI_PAGE_COUNT };

// touch actions on the settings page
enum UIAction : uint8_t { UI_ACT_NONE, UI_ACT_BRIGHT_DEC, UI_ACT_BRIGHT_INC, UI_ACT_TIMEOUT_DEC, UI_ACT_TIMEOUT_INC,
                          UI_ACT_SD_REFRESH };

struct UIModel {
  uint8_t page;

  // home
  char node_name[32];
  int contacts, max_contacts;
  bool time_valid;
  uint8_t hour, minute, day, month, weekday;   // weekday 0 = Sunday
  uint16_t year;
  UILink ble, wifi, usb;
  uint32_t ble_pin;
  char wifi_ssid[33];       // empty = built without Wi-Fi
  uint32_t wifi_ip;         // 0 = no IP

  // power
  uint16_t batt_mv;         // < 1000 = no battery reading
  UIBattTrend trend;
  uint32_t uptime_s;

  // radio
  float freq_mhz, bw_khz;
  uint8_t sf, cr;
  int8_t tx_power;
  char region[32];
  int16_t noise_dbm;

  // settings
  uint8_t brightness_pct;
  const char* timeout_label;
  bool bright_can_dec, bright_can_inc, timeout_can_dec, timeout_can_inc;
  UISdState sd;
  uint32_t sd_total_mb, sd_used_mb;
  const char* sd_type;      // "SDHC", "SDSC", ...
  const char* fw_version;

  // chats page (drawn by UIChats) and the unread count shown in the status bar
  const UIChatsView* chats;
  int chats_unread;

  // interfaces page (drawn by UIInterfaces)
  const UIIfView* links;

  // repeater page (drawn by UIRepeater)
  const UIRepView* rep;
};

namespace UIScreens {
  void render(UIGfx& g, const UIModel& m);
  UIAction hitTest(const UIModel& m, int x, int y);
  // boot screen: "Meshcorium | V1", "Загрузка..." (core_version e.g. "v1.17.1")
  void renderSplash(UIGfx& g, const char* core_version);
  // full-screen error: the LoRa module did not answer at boot
  void renderRadioError(UIGfx& g, const char* core_version);
  int battPercent(uint16_t mv);
  extern const char* const PAGE_TITLES[UI_PAGE_COUNT];
}
