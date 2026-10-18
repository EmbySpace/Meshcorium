#include "UIStyle.h"

namespace UIStyle {

const UIFont* const FIT_VALUE[3] = { &UI_FONT_VALUE, &UI_FONT_TITLE, &UI_FONT_BODY_B };
const UIFont* const FIT_TITLE[3] = { &UI_FONT_TITLE, &UI_FONT_BODY_B, &UI_FONT_SMALL };
const UIFont* const FIT_BODY[2]  = { &UI_FONT_BODY_B, &UI_FONT_SMALL };
const UIFont* const FIT_SMALL[1] = { &UI_FONT_SMALL };

int fitText(UIGfx& g, int x, int y, int max_w, const char* s, Color c,
            const UIFont* const* fonts, int n, UIAlign align) {
  for (int i = 0; i < n; i++) {
    if (UIGfx::textWidth(*fonts[i], s) <= max_w) return g.text(*fonts[i], x, y, s, c, align);
  }
  char buf[96];
  UIGfx::ellipsize(*fonts[n - 1], s, max_w, buf, sizeof(buf));
  return g.text(*fonts[n - 1], x, y, buf, c, align);
}

void toggleSwitch(UIGfx& g, int x, int y, bool on) {
  g.fillRoundRect(x, y, UI_SWITCH_W, UI_SWITCH_H, UI_SWITCH_H / 2, on ? C_LIVE : C_BTN);
  int cx = on ? x + UI_SWITCH_W - UI_SWITCH_H / 2 : x + UI_SWITCH_H / 2;
  g.fillCircle(cx, y + UI_SWITCH_H / 2, UI_SWITCH_H / 2 - 4, C_WHITE);
}

}  // namespace UIStyle
