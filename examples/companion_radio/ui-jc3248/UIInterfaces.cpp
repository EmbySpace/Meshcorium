#include "UIInterfaces.h"
#include "UIStyle.h"
#include <stdio.h>
#include <string.h>

using namespace UIStyle;

#define W         480
#define X0        UI_GAP
#define CW        (W - 2 * UI_GAP)

// main screen
#define LINK_H    ((UI_BOTTOM - UI_TOP - 2 * UI_GAP) / 3)   // 88
#define SW_W      60
#define SW_H      32
#define SW_X      (X0 + CW - 16 - SW_W)

// detail screen
#define HDR_Y     UI_TOP
#define HDR_H     44
#define ROWS_Y    (HDR_Y + HDR_H + UI_GAP)                  // 84
#define ROW_H     52
#define ROW_GAP   6
#define BAR_Y     266
#define BAR_H     46
#define CHOICE_H  34

namespace UIInterfaces {

static const char* const LINK_NAMES[IF_LINK_COUNT] = { "Bluetooth", "Wi-Fi", "USB" };
static const uint16_t LINK_ICONS[IF_LINK_COUNT] = { ICON_BLUETOOTH, ICON_WIFI, ICON_USB };
static const Color LINK_COLORS[IF_LINK_COUNT] = { C_BLUE, C_GREEN, C_ORANGE };

static const UIFont* const FIT_VALUE_LINE[2] = { &UI_FONT_BODY, &UI_FONT_SMALL };

static bool in(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int linkY(int i) { return UI_TOP + i * (LINK_H + UI_GAP); }

static void toggleSwitch(UIGfx& g, int x, int y, bool on, bool enabled) {
  Color track = !enabled ? C_BTN_OFF : on ? C_LIVE : C_BTN;
  g.fillRoundRect(x, y, SW_W, SW_H, SW_H / 2, track);
  int r = SW_H / 2 - 4;
  int cx = on ? x + SW_W - SW_H / 2 : x + SW_H / 2;
  g.fillCircle(cx, y + SW_H / 2, r, enabled ? C_WHITE : C_DARK);
}

// ------------------------------------------------------------------ main ----

static void renderMain(UIGfx& g, const UIIfView& v) {
  for (int i = 0; i < IF_LINK_COUNT; i++) {
    const UIIfLink& l = v.link[i];
    int y = linkY(i);
    g.fillRect(X0, y, CW, LINK_H, C_ROW);
    g.fillRect(X0, y, LINK_H, LINK_H, l.present && l.on ? LINK_COLORS[i] : C_SLATE);
    g.icon(UI_ICON_M, LINK_ICONS[i], X0 + LINK_H / 2, y + LINK_H / 2, C_WHITE);

    int tx = X0 + LINK_H + 16;
    int right = SW_X - (l.has_details ? 34 : 12);
    g.text(UI_FONT_BODY_B, tx, y + 30, LINK_NAMES[i], C_WHITE);
    if (!l.present) {
      g.text(UI_FONT_SMALL, tx, y + 54, "нет в этой прошивке", C_GREY);
      continue;
    }
    fitText(g, tx, y + 54, right - tx, l.status[0], l.on ? C_WHITE : C_GREY, FIT_SMALL, 1);
    fitText(g, tx, y + 74, right - tx, l.status[1], C_GREY, FIT_SMALL, 1);

    if (l.has_details) g.icon(UI_ICON_S, ICON_CHEVRON_RIGHT, SW_X - 20, y + LINK_H / 2, C_GREY);
    toggleSwitch(g, SW_X, y + (LINK_H - SW_H) / 2, l.on, true);
  }
}

// ---------------------------------------------------------------- detail ----

int visibleRows(const UIIfView& v) {
  int bottom = (v.button || v.note[0]) ? BAR_Y - UI_GAP : UI_BOTTOM;
  return (bottom - ROWS_Y + ROW_GAP) / (ROW_H + ROW_GAP);
}

static int choiceWidth(const char* s) {
  int w = UIGfx::textWidth(UI_FONT_SMALL, s) + 28;
  return w < 84 ? 84 : w;
}

// x of option `o` of a choice row (options are right-aligned)
static int choiceX(const UIIfRow& r, int o) {
  int w1 = choiceWidth(r.option[1]);
  int x1 = X0 + CW - 9 - w1;
  if (o == 1) return x1;
  return x1 - 6 - choiceWidth(r.option[0]);
}

static void renderRow(UIGfx& g, int y, const UIIfRow& r) {
  g.fillRect(X0, y, CW, ROW_H, C_ROW);
  int tx = X0 + 16;
  int right = X0 + CW - 16;

  switch (r.kind) {
    case IFR_CHOICE: {
      for (int o = 0; o < 2; o++) {
        int x = choiceX(r, o), w = choiceWidth(r.option[o]);
        g.fillRect(x, y + (ROW_H - CHOICE_H) / 2, w, CHOICE_H, o == r.selected ? C_BLUE : C_BTN);
        g.text(UI_FONT_SMALL, x + w / 2, y + ROW_H / 2 + 6, r.option[o], o == r.selected ? C_WHITE : C_GREY, ALIGN_CENTER);
      }
      right = choiceX(r, 0) - 10;
      break;
    }
    case IFR_EDIT: {
      g.icon(UI_ICON_S, ICON_CHEVRON_RIGHT, X0 + CW - 18, y + ROW_H / 2, C_GREY);
      int vw = UIGfx::textWidth(UI_FONT_BODY, r.value);
      int vmax = CW / 2 - 20;
      fitText(g, X0 + CW - 34, y + 32, vmax, r.value, C_GREY, FIT_VALUE_LINE, 2, ALIGN_RIGHT);
      right = X0 + CW - 34 - (vw < vmax ? vw : vmax) - 12;
      break;
    }
    case IFR_INFO: {
      int vw = fitText(g, X0 + CW - 16, y + 32, CW / 2, r.value, C_GREY, FIT_BODY, 2, ALIGN_RIGHT);
      right = X0 + CW - 16 - vw - 12;
      break;
    }
  }
  fitText(g, tx, y + 32, right - tx, r.title, C_WHITE, FIT_BODY, 2);
}

static void renderDetail(UIGfx& g, const UIIfView& v) {
  // header
  g.fillRect(X0, HDR_Y, CW, HDR_H, C_ROW);
  g.icon(UI_ICON_S, ICON_CHEVRON_LEFT, X0 + 22, HDR_Y + HDR_H / 2, C_WHITE);
  g.text(UI_FONT_BODY_B, X0 + 44, HDR_Y + 29, v.title, C_WHITE);

  int vis = visibleRows(v);
  for (int i = 0; i < vis && v.scroll + i < v.row_count; i++) {
    renderRow(g, ROWS_Y + i * (ROW_H + ROW_GAP), v.rows[v.scroll + i]);
  }
  if (v.row_count > vis) {
    int h = vis * ROW_H + (vis - 1) * ROW_GAP;
    g.fillRect(W - 4, ROWS_Y, 3, h, C_ROW);
    int th = h * vis / v.row_count;
    int ty = ROWS_Y + (h - th) * v.scroll / (v.row_count - vis);
    g.fillRect(W - 4, ty, 3, th, C_GREY);
  }

  if (v.button || v.note[0]) {
    int bw = v.button ? UIGfx::textWidth(UI_FONT_BODY_B, v.button) + 40 : 0;
    if (v.note[0]) fitText(g, X0 + 4, BAR_Y + 29, CW - bw - 16, v.note, C_GREY, FIT_SMALL, 1);
    if (v.button) {
      int bx = X0 + CW - bw;
      g.fillRect(bx, BAR_Y, bw, BAR_H, v.button_active ? C_BLUE : C_BTN);
      g.text(UI_FONT_BODY_B, bx + bw / 2, BAR_Y + 30, v.button, v.button_active ? C_WHITE : C_GREY, ALIGN_CENTER);
    }
  }
}

// --------------------------------------------------------------------------

void render(UIGfx& g, const UIIfView& v) {
  switch (v.screen) {
    case IF_MAIN:     renderMain(g, v); break;
    case IF_DETAIL:   renderDetail(g, v); break;
    case IF_KEYBOARD: UIChats::renderKeyboard(g, v.kb); break;
  }
  if (v.toast[0]) {
    g.fillRect(0, UI_BOTTOM - 40, W, 48, C_BLUE);
    fitText(g, W / 2, UI_BOTTOM - 13, W - 32, v.toast, C_WHITE, FIT_BODY, 2, ALIGN_CENTER);
  }
}

UIIfHit hitTest(const UIIfView& v, int x, int y) {
  UIIfHit h;
  memset(&h, 0, sizeof(h));
  h.type = IFH_NONE;

  switch (v.screen) {
    case IF_MAIN:
      for (int i = 0; i < IF_LINK_COUNT; i++) {
        int ly = linkY(i);
        if (!in(x, y, X0, ly, CW, LINK_H) || !v.link[i].present) continue;
        h.index = i;
        // generous area around the switch
        if (x >= SW_X - 24) h.type = IFH_TOGGLE;
        else if (v.link[i].has_details) h.type = IFH_OPEN;
        else h.type = IFH_TOGGLE;
      }
      break;

    case IF_DETAIL: {
      if (in(x, y, 0, HDR_Y, 140, HDR_H)) { h.type = IFH_BACK; break; }
      if (v.button && in(x, y, X0 + CW / 2, BAR_Y - 4, CW / 2, BAR_H + 8)) { h.type = IFH_BUTTON; break; }
      int vis = visibleRows(v);
      for (int i = 0; i < vis && v.scroll + i < v.row_count; i++) {
        int ry = ROWS_Y + i * (ROW_H + ROW_GAP);
        if (!in(x, y, X0, ry - ROW_GAP / 2, CW, ROW_H + ROW_GAP)) continue;
        const UIIfRow& r = v.rows[v.scroll + i];
        h.index = v.scroll + i;
        if (r.kind == IFR_CHOICE) {
          int x0 = choiceX(r, 0), x1 = choiceX(r, 1);
          if (x >= x1 - 3) { h.type = IFH_CHOICE; h.option = 1; }
          else if (x >= x0 - 20) { h.type = IFH_CHOICE; h.option = 0; }
        } else if (r.kind == IFR_EDIT) {
          h.type = IFH_EDIT;
        }
      }
      break;
    }

    case IF_KEYBOARD: {
      UIChatHit k = UIChats::hitTestKeyboard(v.kb, x, y);
      if (k.type == HIT_BACK) h.type = IFH_BACK;
      else if (k.type == HIT_KEY) { h.type = IFH_KEY; h.key = k.key; }
      break;
    }
  }
  return h;
}

}  // namespace UIInterfaces
