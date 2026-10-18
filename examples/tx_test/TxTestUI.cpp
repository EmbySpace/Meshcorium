#include "TxTestUI.h"
#include "UIStyle.h"
#include <stdio.h>
#include <string.h>

using namespace UIStyle;

const uint8_t TX_DUTY_PCT[TX_DUTY_COUNT] = { 100, 10, 1 };
const TxProfile TX_PROFILES[TX_PROFILE_COUNT] = {
  { "SF7 BW250", 7, 250.0f },     // fast: most data per second
  { "SF8 BW62.5", 8, 62.5f },     // MeshCore default
};

#define W        480
#define X0       UI_GAP
#define ROW_H    ((UI_BOTTOM - UI_TOP - UI_GAP) / 2)    // 136
#define Y1       (UI_TOP + ROW_H + UI_GAP)
#define BIG_W    300
#define PWR_W    228
#define SMALL_W  ((W - 2 * UI_GAP - PWR_W - 2 * UI_GAP) / 2)
#define BTN      44

static int dutyX()   { return X0 + PWR_W + UI_GAP; }
static int profX()   { return dutyX() + SMALL_W + UI_GAP; }
static int minusX()  { return X0 + PWR_W - 2 * BTN - 18; }
static int plusX()   { return X0 + PWR_W - BTN - 10; }

static void fmtTime(char* d, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  snprintf(d, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

namespace TxTestUI {

static void statusBar(UIGfx& g, const TxView& v) {
  label(g, 10, 19, "TX ТЕСТ", C_WHITE);
  char t[48];
  snprintf(t, sizeof(t), "%.3f МГЦ", v.freq_mhz);
  label(g, 100, 19, t, C_GREY);
  if (v.batt_mv >= 1000) {
    snprintf(t, sizeof(t), "%u.%02u V", v.batt_mv / 1000, (v.batt_mv % 1000) / 10);
    g.text(UI_FONT_LABEL, W - 10, 19, t, C_WHITE, ALIGN_RIGHT, 1);
  }
}

void render(UIGfx& g, const TxView& v) {
  g.resetClip();
  g.clear(C_BG);
  statusBar(g, v);
  char buf[96], b2[24];

  // ---- start / stop ----
  Color bc = !v.radio_ok ? C_SLATE : v.running ? C_RED : C_BLUE;
  g.fillRect(X0, UI_TOP, BIG_W, ROW_H, bc);
  label(g, X0 + 16, UI_TOP + 26, !v.radio_ok ? "РАДИО НЕ ОТВЕЧАЕТ" : v.running ? "ИДЁТ ПЕРЕДАЧА" : "ПЕРЕДАЧА ВЫКЛ");
  g.icon(UI_ICON_M, v.running ? ICON_RSS : ICON_POWER, X0 + BIG_W - 26, UI_TOP + 26, C_WHITE);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)v.packets);
  int nw = g.text(UI_FONT_NUM_B, X0 + 12, UI_TOP + 92, buf, C_WHITE);
  g.text(UI_FONT_BODY_B, X0 + 12 + nw + 8, UI_TOP + 92, "пак.", soft(bc));
  uint32_t kb = v.bytes / 1024;
  snprintf(buf, sizeof(buf), "%lu кБ  \xE2\x80\xA2  %s", (unsigned long)kb,
           !v.radio_ok ? "проверьте E22" : v.running ? "коснитесь: стоп" : "коснитесь: старт");
  g.text(UI_FONT_SMALL, X0 + 16, UI_TOP + ROW_H - 16, buf, soft(bc));

  // ---- air time ----
  int ax = X0 + BIG_W + UI_GAP, aw = W - UI_GAP - ax;
  tile(g, ax, UI_TOP, aw, ROW_H, C_PURPLE, ICON_HOURGLASS, "В ЭФИРЕ");
  int pct = v.elapsed_ms ? (int)((uint64_t)v.air_ms * 100 / v.elapsed_ms) : 0;
  snprintf(buf, sizeof(buf), "%d%%", pct > 100 ? 100 : pct);
  g.text(UI_FONT_VALUE, ax + 12, UI_TOP + 70, buf, C_WHITE);
  fmtTime(b2, sizeof(b2), v.elapsed_ms);
  char lim[12];
  fmtTime(lim, sizeof(lim), v.limit_ms);
  snprintf(buf, sizeof(buf), "%s из %s", b2, lim);
  g.text(UI_FONT_SMALL, ax + 14, UI_TOP + 94, buf, soft(C_PURPLE));

  // ---- power ----
  g.fillRect(X0, Y1, PWR_W, ROW_H, C_ORANGE);
  label(g, X0 + 14, Y1 + 26, "МОЩНОСТЬ");
  snprintf(buf, sizeof(buf), "%d dBm", v.power_dbm);
  g.text(UI_FONT_VALUE, X0 + 12, Y1 + 92, buf, C_WHITE);
  int out = v.power_dbm + 10; if (out > 30) out = 30;     // E22-900M30S: ~10 dB gain, ~30 dBm max
  snprintf(buf, sizeof(buf), "\xE2\x89\x88 %d dBm на выходе E22", out);
  g.text(UI_FONT_SMALL, X0 + 14, Y1 + ROW_H - 16, buf, soft(C_ORANGE));
  for (int i = 0; i < 2; i++) {
    int bx = i ? plusX() : minusX();
    g.fillRect(bx, Y1 + 10, BTN, BTN, UIGfx::mix(C_ORANGE, C_BG, 70));
    g.icon(UI_ICON_S, i ? ICON_PLUS : ICON_MINUS, bx + BTN / 2, Y1 + 10 + BTN / 2, C_WHITE);
  }

  // ---- duty ----
  tile(g, dutyX(), Y1, SMALL_W, ROW_H, C_TEAL, 0, "СКВАЖН.");
  snprintf(buf, sizeof(buf), "%d%%", TX_DUTY_PCT[v.duty_idx]);
  g.text(UI_FONT_VALUE, dutyX() + 12, Y1 + 72, buf, C_WHITE);
  g.text(UI_FONT_SMALL, dutyX() + 14, Y1 + 96, "нажать", soft(C_TEAL));

  // ---- profile ----
  const TxProfile& p = TX_PROFILES[v.profile_idx];
  tile(g, profX(), Y1, SMALL_W, ROW_H, C_GREEN, 0, "ПРОФИЛЬ");
  snprintf(buf, sizeof(buf), "SF%d", p.sf);
  g.text(UI_FONT_VALUE, profX() + 12, Y1 + 72, buf, C_WHITE);
  int bw10 = (int)(p.bw * 10 + 0.5f);
  if (bw10 % 10) snprintf(buf, sizeof(buf), "BW %d.%d", bw10 / 10, bw10 % 10);
  else snprintf(buf, sizeof(buf), "BW %d", bw10 / 10);
  g.text(UI_FONT_SMALL, profX() + 14, Y1 + 96, buf, soft(C_GREEN));
}

static bool in(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

TxHit hitTest(const TxView& v, int x, int y) {
  if (in(x, y, X0, UI_TOP, BIG_W, ROW_H)) return TXH_START_STOP;
  if (in(x, y, minusX() - 8, Y1, BTN + 16, BTN + 24)) return TXH_POWER_DEC;
  if (in(x, y, plusX() - 8, Y1, BTN + 18, BTN + 24)) return TXH_POWER_INC;
  if (in(x, y, dutyX(), Y1, SMALL_W, ROW_H)) return TXH_DUTY;
  if (in(x, y, profX(), Y1, SMALL_W, ROW_H)) return TXH_PROFILE;
  return TXH_NONE;
}

}  // namespace TxTestUI
