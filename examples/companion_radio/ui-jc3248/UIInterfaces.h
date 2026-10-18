#pragma once

// Interfaces page (page 3) of the JC3248W535 UI: Bluetooth / Wi-Fi / USB
// on-off switches, and their detail settings. Pure drawing + hit-testing;
// the logic is in InterfacesController.
//
//   main: [Bluetooth  status  (o )] ──tap row──> Bluetooth settings
//         [Wi-Fi      status  ( o)] ──tap row──> Wi-Fi settings
//         [USB        status  ( o)]
//   settings: rows of choices (two options) and editable values (keyboard),
//             a note and an action button at the bottom ("Применить").

#include "UIGfx.h"
#include "UIChats.h"      // UIKeyboardView, UIKey

enum UIIfScreen : uint8_t { IF_MAIN, IF_DETAIL, IF_KEYBOARD };

enum UIIfLinkIdx : uint8_t { IF_LINK_BLE, IF_LINK_WIFI, IF_LINK_USB, IF_LINK_COUNT };

struct UIIfLink {
  bool present;            // built into the firmware
  bool on;
  bool has_details;        // row opens a settings screen
  char status[2][48];      // two lines under the name
};

enum UIIfRowKind : uint8_t { IFR_CHOICE, IFR_EDIT, IFR_INFO };

struct UIIfRow {
  uint8_t kind;            // UIIfRowKind
  uint8_t id;              // the controller's field id
  char title[32];
  char value[48];          // IFR_EDIT / IFR_INFO
  const char* option[2];   // IFR_CHOICE
  uint8_t selected;        // IFR_CHOICE
};

#define UI_IF_MAX_ROWS 10

struct UIIfView {
  uint8_t screen;          // UIIfScreen
  UIIfLink link[IF_LINK_COUNT];

  // detail screen
  uint8_t detail;          // UIIfLinkIdx shown in IF_DETAIL
  char title[24];
  UIIfRow rows[UI_IF_MAX_ROWS];
  int row_count;
  int scroll;
  const char* button;      // bottom button, NULL = none
  bool button_active;
  char note[64];           // grey text next to the button

  UIKeyboardView kb;
  char toast[48];
};

enum UIIfHitType : uint8_t {
  IFH_NONE, IFH_BACK, IFH_TOGGLE, IFH_OPEN, IFH_CHOICE, IFH_EDIT, IFH_BUTTON, IFH_KEY
};

struct UIIfHit {
  UIIfHitType type;
  int index;               // IFH_TOGGLE / IFH_OPEN: link; IFH_CHOICE / IFH_EDIT: row
  int option;              // IFH_CHOICE: 0 / 1
  UIKey key;               // IFH_KEY
};

namespace UIInterfaces {
  void render(UIGfx& g, const UIIfView& v);
  UIIfHit hitTest(const UIIfView& v, int x, int y);
  int visibleRows(const UIIfView& v);
}
