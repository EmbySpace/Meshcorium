#include "UIRepeater.h"
#include "UIStyle.h"
#include <stdio.h>
#include <string.h>

using namespace UIStyle;

#define W        480
#define X0       UI_GAP
#define CW       (W - 2 * UI_GAP)
#define ROW_H    52
#define ROW_GAP  6
#define BTN_Y    (UI_TOP + 4 * (ROW_H + ROW_GAP))       // 264
#define BTN_H    (UI_BOTTOM - BTN_Y)                    // 48
#define STEP_BTN 40
#define STEP_VAL 76
#define SW_X     (X0 + CW - 12 - UI_SWITCH_W)
#define PLUS_X   (X0 + CW - 6 - STEP_BTN)
#define MINUS_X  (PLUS_X - STEP_VAL - STEP_BTN)

namespace UIRepeater {

static int rowY(int i) { return UI_TOP + i * (ROW_H + ROW_GAP); }

static void row(UIGfx& g, int i, Color c, uint16_t icon, const char* title, const char* sub, int right) {
  int y = rowY(i);
  g.fillRect(X0, y, CW, ROW_H, C_ROW);
  g.fillRect(X0, y, ROW_H, ROW_H, c);
  g.icon(UI_ICON_M, icon, X0 + ROW_H / 2, y + ROW_H / 2, C_WHITE);
  int tx = X0 + ROW_H + 14;
  fitText(g, tx, y + 23, right - tx - 10, title, C_WHITE, FIT_BODY, 2);
  fitText(g, tx, y + 43, right - tx - 10, sub, C_GREY, FIT_SMALL, 1);
}

static void stepper(UIGfx& g, int i, const char* value, bool dec, bool inc) {
  int y = rowY(i) + (ROW_H - STEP_BTN) / 2;
  const int bx[2] = { MINUS_X, PLUS_X };
  const bool en[2] = { dec, inc };
  for (int k = 0; k < 2; k++) {
    g.fillRect(bx[k], y, STEP_BTN, STEP_BTN, en[k] ? C_BTN : C_BTN_OFF);
    g.icon(UI_ICON_S, k ? ICON_PLUS : ICON_MINUS, bx[k] + STEP_BTN / 2, y + STEP_BTN / 2, en[k] ? C_WHITE : C_DARK);
  }
  fitText(g, MINUS_X + STEP_BTN + STEP_VAL / 2, y + 27, STEP_VAL - 6, value, C_WHITE, FIT_BODY, 2, ALIGN_CENTER);
}

void render(UIGfx& g, const UIRepView& v) {
  char sub[64];
  int fq = (int)(v.freq_mhz * 1000 + 0.5f);
  snprintf(sub, sizeof(sub), "пересылать пакеты на %d.%03d МГц", fq / 1000, fq % 1000);
  row(g, 0, v.repeat_on ? C_TEAL : C_SLATE, ICON_RSS, "Ретрансляция", sub, SW_X);
  toggleSwitch(g, SW_X, rowY(0) + (ROW_H - UI_SWITCH_H) / 2, v.repeat_on);

  row(g, 1, v.as_repeater ? C_PURPLE : C_SLATE, ICON_SERVER, "Анонс как ретранслятор",
      v.as_repeater ? "у других в списке ретрансляторов" : "у других в списке как компаньон", SW_X);
  toggleSwitch(g, SW_X, rowY(1) + (ROW_H - UI_SWITCH_H) / 2, v.as_repeater);

  row(g, 2, C_ORANGE, ICON_SIGNAL, "Авто-анонс рядом", v.local_note, MINUS_X);
  stepper(g, 2, v.local_label, v.local_dec, v.local_inc);

  row(g, 3, C_BLUE, ICON_GLOBE, "Авто-анонс по сети", v.flood_note, MINUS_X);
  stepper(g, 3, v.flood_label, v.flood_dec, v.flood_inc);

  // advert now
  int bw = (CW - UI_GAP) / 2;
  const char* names[2] = { "Анонс рядом", "Анонс по сети" };
  const Color cols[2] = { C_ORANGE, C_BLUE };
  const uint16_t icons[2] = { ICON_PAPER_PLANE, ICON_GLOBE };
  for (int k = 0; k < 2; k++) {
    int bx = X0 + k * (bw + UI_GAP);
    g.fillRect(bx, BTN_Y, bw, BTN_H, cols[k]);
    int tw = UIGfx::textWidth(UI_FONT_BODY_B, names[k]);
    int ix = bx + (bw - tw) / 2 - 14;
    g.icon(UI_ICON_S, icons[k], ix, BTN_Y + BTN_H / 2, C_WHITE);
    g.text(UI_FONT_BODY_B, ix + 16, BTN_Y + BTN_H / 2 + 7, names[k], C_WHITE);
  }

  if (v.toast[0]) {
    g.fillRect(0, UI_BOTTOM - 40, W, 48, C_BLUE);
    fitText(g, W / 2, UI_BOTTOM - 13, W - 32, v.toast, C_WHITE, FIT_BODY, 2, ALIGN_CENTER);
  }
}

static bool in(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

UIRepHit hitTest(const UIRepView& v, int x, int y) {
  // whole first two rows toggle (big targets)
  if (in(x, y, X0, rowY(0), CW, ROW_H + ROW_GAP / 2)) return REPH_REPEAT;
  if (in(x, y, X0, rowY(1) - ROW_GAP / 2, CW, ROW_H + ROW_GAP)) return REPH_AS_REPEATER;
  for (int i = 2; i < 4; i++) {
    int by = rowY(i) - ROW_GAP / 2;
    if (in(x, y, MINUS_X - 10, by, STEP_BTN + 20, ROW_H + ROW_GAP)) return i == 2 ? REPH_LOCAL_DEC : REPH_FLOOD_DEC;
    if (in(x, y, PLUS_X - 10, by, STEP_BTN + 16, ROW_H + ROW_GAP)) return i == 2 ? REPH_LOCAL_INC : REPH_FLOOD_INC;
  }
  if (in(x, y, X0, BTN_Y - ROW_GAP / 2, CW / 2, BTN_H + ROW_GAP)) return REPH_ADVERT_LOCAL;
  if (in(x, y, X0 + CW / 2, BTN_Y - ROW_GAP / 2, CW / 2, BTN_H + ROW_GAP)) return REPH_ADVERT_FLOOD;
  return REPH_NONE;
}

}  // namespace UIRepeater
