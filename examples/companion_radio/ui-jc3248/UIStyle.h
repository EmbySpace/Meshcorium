#pragma once

// Shared look of the JC3248W535 UI ("flat tiles"): palette, layout constants
// and small drawing helpers used by UIScreens.cpp and UIChats.cpp.

#include "UIGfx.h"
#include "ui_assets.h"

// ---- palette: flat tiles on black ----
#define C_BG        RGB(0x0B0B0B)
#define C_WHITE     RGB(0xFFFFFF)
#define C_GREY      RGB(0x8C8C8C)
#define C_DARK      RGB(0x3C3C3C)
#define C_LIVE      RGB(0x6FD64A)     // "app connected" on black
#define C_TEAL      RGB(0x00A3A0)
#define C_BLUE      RGB(0x1B7FE0)
#define C_PURPLE    RGB(0x7A3FD6)
#define C_ORANGE    RGB(0xF08C00)
#define C_GREEN     RGB(0x5FA82A)
#define C_MAGENTA   RGB(0xC8146E)
#define C_RED       RGB(0xD8342C)
#define C_SLATE     RGB(0x2B2F36)     // tile for "nothing here / off"
#define C_ROW       RGB(0x1C1E22)     // list rows, settings rows
#define C_BTN       RGB(0x34373D)
#define C_BTN_OFF   RGB(0x24262A)

// ---- layout ----
#define UI_GAP      8
#define UI_BAR_H    28                // status bar
#define UI_TOP      32                // first pixel below the status bar
#define UI_BOTTOM   312               // bottom of the content area

namespace UIStyle {

extern const UIFont* const FIT_VALUE[3];
extern const UIFont* const FIT_TITLE[3];
extern const UIFont* const FIT_BODY[2];
extern const UIFont* const FIT_SMALL[1];

// largest of the given fonts that fits; ellipsizes with the last one otherwise
int fitText(UIGfx& g, int x, int y, int max_w, const char* s, Color c,
            const UIFont* const* fonts, int n, UIAlign align = ALIGN_LEFT);

// small spaced-out caps text
inline void label(UIGfx& g, int x, int y, const char* s, Color c = C_WHITE, UIAlign a = ALIGN_LEFT) {
  g.text(UI_FONT_LABEL, x, y, s, c, a, 2);
}

// faded white for secondary text on coloured tiles
inline Color soft(Color bg) { return UIGfx::mix(bg, C_WHITE, 200); }

// flat tile: icon top-right, caps caption bottom-left
inline void tile(UIGfx& g, int x, int y, int w, int h, Color bg, uint16_t icon, const char* caption) {
  g.fillRect(x, y, w, h, bg);
  if (icon) g.icon(UI_ICON_M, icon, x + w - 26, y + 26, C_WHITE);
  if (caption) label(g, x + 14, y + h - 14, caption);
}

// on/off switch, 60x32
#define UI_SWITCH_W 60
#define UI_SWITCH_H 32
void toggleSwitch(UIGfx& g, int x, int y, bool on);

}  // namespace UIStyle
