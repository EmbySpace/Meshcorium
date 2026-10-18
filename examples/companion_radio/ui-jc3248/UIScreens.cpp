#include "UIScreens.h"
#include "UIStyle.h"
#include "Version.h"
#include "UIChats.h"
#include "UIInterfaces.h"
#include "UIRepeater.h"
#include <stdio.h>
#include <string.h>

#define GAP         UI_GAP
#define TILES_Y     UI_TOP
#define TILES_B     UI_BOTTOM
#define ROW_H       ((TILES_B - TILES_Y - GAP) / 2)   // two tile rows: 136

using namespace UIStyle;

namespace UIScreens {

const char* const PAGE_TITLES[UI_PAGE_COUNT] = {
  "ГЛАВНАЯ", "ЧАТЫ", "ИНТЕРФЕЙСЫ", "ПИТАНИЕ", "РАДИО", "НАСТРОЙКИ", "РЕТРАНСЛЯТОР"
};

int battPercent(uint16_t mv) {
  // single Li-ion cell, resting voltage -> charge (approximate)
  static const uint16_t pts[][2] = {
    {4200, 100}, {4100, 90}, {4000, 79}, {3900, 66}, {3800, 52},
    {3750, 42}, {3700, 32}, {3650, 22}, {3600, 14}, {3500, 6}, {3300, 0}
  };
  if (mv < 1000) return -1;
  if (mv >= pts[0][0]) return 100;
  for (size_t i = 1; i < sizeof(pts) / sizeof(pts[0]); i++) {
    if (mv >= pts[i][0]) {
      int v0 = pts[i][0], v1 = pts[i - 1][0], p0 = pts[i][1], p1 = pts[i - 1][1];
      return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
    }
  }
  return 0;
}

static Color levelColor(int pct) {
  return pct < 0 ? C_SLATE : pct <= 15 ? C_RED : pct <= 35 ? C_ORANGE : C_GREEN;
}

// status bar icons: dark = absent / off, white = up (cable in, advertising,
// on the network), green = an app is connected
static Color linkColor(UILink l) {
  return l == UI_LINK_CLIENT ? C_LIVE : l == UI_LINK_READY ? C_WHITE : C_DARK;
}

// ------------------------------------------------------------- helpers ----

static void formatIP(char* d, size_t n, uint32_t ip) {
  snprintf(d, n, "%u.%u.%u.%u", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)(ip >> 24));
}

static void formatUptime(char* d, size_t n, uint32_t s) {
  uint32_t m = s / 60, h = m / 60, days = h / 24;
  if (days > 0) snprintf(d, n, "%luд %02luч %02luм", (unsigned long)days, (unsigned long)(h % 24), (unsigned long)(m % 60));
  else if (h > 0) snprintf(d, n, "%luч %02luм", (unsigned long)h, (unsigned long)(m % 60));
  else snprintf(d, n, "%luм %02luс", (unsigned long)m, (unsigned long)(s % 60));
}

static void formatKHz(char* d, size_t n, float khz) {
  int t = (int)(khz * 10 + 0.5f);
  if (t % 10 == 0) snprintf(d, n, "%d", t / 10);
  else snprintf(d, n, "%d.%d", t / 10, t % 10);
}

static void formatSize(char* d, size_t n, uint32_t mb) {
  if (mb >= 1024) snprintf(d, n, "%lu.%lu ГБ", (unsigned long)(mb / 1024), (unsigned long)((mb % 1024) * 10 / 1024));
  else snprintf(d, n, "%lu МБ", (unsigned long)mb);
}

// --------------------------------------------------------- status bar ----

static void statusBar(UIGfx& g, const UIModel& m) {
  char t[16];
  snprintf(t, sizeof(t), "%d/%d", m.page + 1, UI_PAGE_COUNT);
  int w = g.text(UI_FONT_LABEL, 10, 19, t, C_WHITE, ALIGN_LEFT, 1);
  int tx = 10 + w + 10;
  tx += g.text(UI_FONT_LABEL, tx, 19, PAGE_TITLES[m.page], C_GREY, ALIGN_LEFT, 2);

  // unread chat messages, on every page
  if (m.chats_unread > 0) {
    snprintf(t, sizeof(t), "%d", m.chats_unread > 99 ? 99 : m.chats_unread);
    int cw = UIGfx::textWidth(UI_FONT_LABEL, t, 1);
    int px = tx + 12, pw = 30 + cw;
    g.fillRect(px, 5, pw, 18, C_BLUE);
    g.icon(UI_ICON_S, ICON_COMMENT, px + 12, 14, C_WHITE);
    g.text(UI_FONT_LABEL, px + 24, 19, t, C_WHITE, ALIGN_LEFT, 1);
  }

  // battery
  int pct = battPercent(m.batt_mv);
  int bx = UI_W - 10 - 24, by = 9;
  g.fillRect(bx, by, 22, 11, C_WHITE);
  g.fillRect(bx + 1, by + 1, 20, 9, C_BG);
  g.fillRect(bx + 22, by + 3, 2, 5, C_WHITE);
  if (pct > 0) g.fillRect(bx + 2, by + 2, (18 * pct + 99) / 100, 7, pct <= 15 ? C_RED : C_WHITE);
  if (pct >= 0) snprintf(t, sizeof(t), "%d%%", pct); else strcpy(t, "--");
  int tw = g.text(UI_FONT_LABEL, bx - 6, 19, t, C_WHITE, ALIGN_RIGHT, 1);

  int x = bx - 6 - tw - 18;
  // always all three, so their places never move
  g.icon(UI_ICON_S, ICON_USB, x, 14, linkColor(m.usb));        x -= 28;
  g.icon(UI_ICON_S, ICON_BLUETOOTH, x, 14, linkColor(m.ble));  x -= 26;
  g.icon(UI_ICON_S, ICON_WIFI, x, 14, linkColor(m.wifi));
}

// ---------------------------------------------------------------- home ----

static void renderHome(UIGfx& g, const UIModel& m) {
  char buf[48];
  static const char* const WEEKDAYS[7] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
  static const char* const MONTHS[12] = { "января", "февраля", "марта", "апреля", "мая", "июня",
                                          "июля", "августа", "сентября", "октября", "ноября", "декабря" };

  // ---- clock tile with the node name ----
  const int x = GAP, y = TILES_Y, w = UI_W - 2 * GAP, h = ROW_H;
  g.fillRect(x, y, w, h, C_TEAL);
  fitText(g, x + 16, y + 30, w / 2, m.node_name[0] ? m.node_name : "Без имени", C_WHITE, FIT_TITLE, 3);

  if (m.time_valid) {
    snprintf(buf, sizeof(buf), "%02d:%02d", m.hour, m.minute);
    g.text(UI_FONT_HUGE_B, x + 10, y + h - 22, buf, C_WHITE);
    g.text(UI_FONT_SMALL, x + w - 16, y + 62, WEEKDAYS[m.weekday % 7], soft(C_TEAL), ALIGN_RIGHT);
    snprintf(buf, sizeof(buf), "%d %s", m.day, MONTHS[(m.month + 11) % 12]);
    g.text(UI_FONT_TITLE, x + w - 16, y + 90, buf, C_WHITE, ALIGN_RIGHT);
    snprintf(buf, sizeof(buf), "%d", m.year);
    g.text(UI_FONT_SMALL, x + w - 16, y + 114, buf, soft(C_TEAL), ALIGN_RIGHT);
  } else {
    g.icon(UI_ICON_L, ICON_CLOCK, x + 40, y + 86, C_WHITE);
    g.text(UI_FONT_TITLE, x + 78, y + 86, "Время не задано", C_WHITE);
    g.text(UI_FONT_SMALL, x + 78, y + 110, "подключите приложение или RTC", soft(C_TEAL));
  }

  // ---- contacts / PIN / Wi-Fi ----
  const int ty = TILES_Y + ROW_H + GAP, th = ROW_H;
  const int tw = (UI_W - 4 * GAP) / 3;
  int tx = GAP;

  tile(g, tx, ty, tw, th, C_ORANGE, ICON_USERS, "КОНТАКТЫ");
  snprintf(buf, sizeof(buf), "%d", m.contacts);
  int nw = g.text(UI_FONT_NUM_B, tx + 10, ty + th - 38, buf, C_WHITE);
  snprintf(buf, sizeof(buf), "/%d", m.max_contacts);
  g.text(UI_FONT_BODY_B, tx + 10 + nw + 4, ty + th - 38, buf, soft(C_ORANGE));
  tx += tw + GAP;

  bool ble_on = m.ble != UI_LINK_ABSENT;
  Color bc = ble_on ? C_BLUE : C_SLATE;
  tile(g, tx, ty, tw, th, bc, ICON_BLUETOOTH, "PIN");
  if (ble_on && m.ble_pin) snprintf(buf, sizeof(buf), "%06lu", (unsigned long)m.ble_pin);
  else strcpy(buf, "\xE2\x80\x94");
  fitText(g, tx + 12, ty + th - 48, tw - 24, buf, C_WHITE, FIT_VALUE, 3);
  const char* st = !ble_on ? "нет в прошивке" : m.ble == UI_LINK_CLIENT ? "подключён"
                 : m.ble == UI_LINK_READY ? "ожидание" : "выключен";
  g.text(UI_FONT_SMALL, tx + 14, ty + th - 30, st, soft(bc));
  tx += tw + GAP;

  int ww = UI_W - GAP - tx;
  bool wifi_cfg = m.wifi != UI_LINK_ABSENT && m.wifi_ssid[0];
  Color wc = !wifi_cfg ? C_SLATE : m.wifi == UI_LINK_OFF ? C_SLATE : C_GREEN;
  tile(g, tx, ty, ww, th, wc, ICON_WIFI, "WI-FI");
  if (!wifi_cfg) {
    fitText(g, tx + 14, ty + th - 48, ww - 28, "\xE2\x80\x94", C_WHITE, FIT_VALUE, 3);
    g.text(UI_FONT_SMALL, tx + 14, ty + th - 30, "не настроен", soft(wc));
  } else {
    fitText(g, tx + 14, ty + th - 48, ww - 28, m.wifi_ssid, C_WHITE, FIT_TITLE, 3);
    if (m.wifi_ip) formatIP(buf, sizeof(buf), m.wifi_ip);
    else strcpy(buf, "нет сети");
    if (m.wifi == UI_LINK_CLIENT) strncat(buf, " \xE2\x80\xA2", sizeof(buf) - strlen(buf) - 1);
    fitText(g, tx + 14, ty + th - 30, ww - 28, buf, soft(wc), FIT_SMALL, 1);
  }
}

// --------------------------------------------------------------- power ----

static void renderPower(UIGfx& g, const UIModel& m) {
  char buf[32];
  int pct = battPercent(m.batt_mv);

  // ---- charge ----
  const int x = GAP, y = TILES_Y, w = 300, h = ROW_H;
  Color lc = levelColor(pct);
  g.fillRect(x, y, w, h, lc);
  label(g, x + 16, y + 26, "ЗАРЯД");
  if (m.trend == UI_BATT_CHARGING) g.icon(UI_ICON_M, ICON_BOLT, x + w - 26, y + 26, C_WHITE);
  if (pct >= 0) snprintf(buf, sizeof(buf), "%d%%", pct); else strcpy(buf, "\xE2\x80\x94");
  g.text(UI_FONT_HUGE_B, x + 10, y + 100, buf, C_WHITE);
  const int bw = w - 32;
  g.fillRect(x + 16, y + h - 22, bw, 8, UIGfx::mix(lc, C_WHITE, 70));
  if (pct > 0) g.fillRect(x + 16, y + h - 22, bw * pct / 100, 8, C_WHITE);

  // ---- voltage ----
  const int vx = x + w + GAP, vw = UI_W - GAP - vx;
  tile(g, vx, y, vw, h, C_BLUE, ICON_TACHOMETER, "НАПРЯЖЕНИЕ");
  if (m.batt_mv >= 1000) snprintf(buf, sizeof(buf), "%u.%02u V", m.batt_mv / 1000, (m.batt_mv % 1000) / 10);
  else strcpy(buf, "\xE2\x80\x94");
  fitText(g, vx + 12, y + h - 40, vw - 24, buf, C_WHITE, FIT_VALUE, 3);

  // ---- state / uptime ----
  const int ry = y + h + GAP, rw = (UI_W - 3 * GAP) / 2;
  const char* st; Color sc;
  switch (m.trend) {
    case UI_BATT_CHARGING:    st = "Заряжается";     sc = C_GREEN;  break;
    case UI_BATT_DISCHARGING: st = "Разряжается";    sc = C_ORANGE; break;
    case UI_BATT_FULL:        st = "Заряжена";       sc = C_GREEN;  break;
    case UI_BATT_NONE:        st = "Нет батареи";    sc = C_SLATE;  break;
    default:                  st = "Определяется..."; sc = C_SLATE; break;
  }
  tile(g, GAP, ry, rw, h, sc, ICON_BOLT, "СОСТОЯНИЕ");
  fitText(g, GAP + 14, ry + h - 42, rw - 28, st, C_WHITE, FIT_VALUE, 3);

  const int ux = GAP + rw + GAP, uw = UI_W - GAP - ux;
  tile(g, ux, ry, uw, h, C_PURPLE, ICON_HOURGLASS, "ВРЕМЯ РАБОТЫ");
  formatUptime(buf, sizeof(buf), m.uptime_s);
  fitText(g, ux + 14, ry + h - 42, uw - 28, buf, C_WHITE, FIT_VALUE, 3);
}

// --------------------------------------------------------------- radio ----

static void radioTile(UIGfx& g, int x, int y, int w, int h, Color bg, uint16_t icon,
                      const char* caption, const char* value, const char* sub) {
  tile(g, x, y, w, h, bg, icon, caption);
  fitText(g, x + 12, y + h - 48, w - 24, value, C_WHITE, FIT_VALUE, 3);
  if (sub) fitText(g, x + 14, y + h - 30, w - 28, sub, soft(bg), FIT_SMALL, 1);
}

static void renderRadio(UIGfx& g, const UIModel& m) {
  char v[32], s[32], k[12];
  const int tw = (UI_W - 4 * GAP) / 3, th = ROW_H;
  const int y0 = TILES_Y, y1 = TILES_Y + th + GAP;
  const int x0 = GAP, x1 = x0 + tw + GAP, x2 = x1 + tw + GAP, w2 = UI_W - GAP - x2;

  snprintf(v, sizeof(v), "%.3f", m.freq_mhz);
  formatKHz(k, sizeof(k), m.bw_khz);
  snprintf(s, sizeof(s), "BW %s кГц", k);
  radioTile(g, x0, y0, tw, th, C_TEAL, ICON_RSS, "ЧАСТОТА, МГЦ", v, s);

  snprintf(v, sizeof(v), "SF%d", m.sf);
  radioTile(g, x1, y0, tw, th, C_PURPLE, ICON_SLIDERS, "SF", v, "расширение");

  snprintf(v, sizeof(v), "4/%d", m.cr);
  radioTile(g, x2, y0, w2, th, C_MAGENTA, ICON_HASHTAG, "CR", v, "кодирование");

  snprintf(v, sizeof(v), "%d dBm", m.tx_power);
  radioTile(g, x0, y1, tw, th, C_ORANGE, ICON_BOLT, "МОЩНОСТЬ", v, "передатчик");

  radioTile(g, x1, y1, tw, th, m.region[0] ? C_BLUE : C_SLATE, ICON_GLOBE, "РЕГИОН",
            m.region[0] ? m.region : "\xE2\x80\x94", m.region[0] ? "рассылка" : "не задан");

  snprintf(v, sizeof(v), "%d", m.noise_dbm);
  radioTile(g, x2, y1, w2, th, m.noise_dbm > -95 ? C_ORANGE : C_GREEN, ICON_BAR_CHART, "ШУМ", v, "dBm");
}

// ------------------------------------------------------------ settings ----

#define SET_ROW_H   64
#define SET_BTN     44
#define SET_VAL_W   80

static int setRowY(int i) { return TILES_Y + i * (SET_ROW_H + GAP); }
static int setPlusX()  { return UI_W - GAP - 10 - SET_BTN; }
static int setMinusX() { return setPlusX() - SET_VAL_W - SET_BTN; }
static int setBtnY(int i) { return setRowY(i) + (SET_ROW_H - SET_BTN) / 2; }

static void button(UIGfx& g, int x, int y, uint16_t icon, bool enabled) {
  g.fillRect(x, y, SET_BTN, SET_BTN, enabled ? C_BTN : C_BTN_OFF);
  g.icon(UI_ICON_S, icon, x + SET_BTN / 2, y + SET_BTN / 2, enabled ? C_WHITE : C_DARK);
}

static void settingRow(UIGfx& g, int i, Color c, uint16_t icon, const char* title, const char* sub, int sub_right = -1) {
  int y = setRowY(i);
  g.fillRect(GAP, y, UI_W - 2 * GAP, SET_ROW_H, C_ROW);
  g.fillRect(GAP, y, SET_ROW_H, SET_ROW_H, c);
  g.icon(UI_ICON_M, icon, GAP + SET_ROW_H / 2, y + SET_ROW_H / 2, C_WHITE);
  g.text(UI_FONT_BODY_B, GAP + SET_ROW_H + 14, y + 28, title, C_WHITE);
  if (sub) fitText(g, GAP + SET_ROW_H + 14, y + 50, (sub_right < 0 ? setMinusX() : sub_right) - GAP - SET_ROW_H - 24, sub, C_GREY, FIT_SMALL, 1);
}

static void stepper(UIGfx& g, int i, const char* value, bool can_dec, bool can_inc) {
  int y = setBtnY(i);
  button(g, setMinusX(), y, ICON_MINUS, can_dec);
  button(g, setPlusX(), y, ICON_PLUS, can_inc);
  g.text(UI_FONT_TITLE, setMinusX() + SET_BTN + SET_VAL_W / 2, y + 30, value, C_WHITE, ALIGN_CENTER);
}

static void renderSettings(UIGfx& g, const UIModel& m) {
  char buf[48], a[16], b[16];

  settingRow(g, 0, C_ORANGE, ICON_SUN, "Яркость", "подсветка экрана");
  snprintf(buf, sizeof(buf), "%d%%", m.brightness_pct);
  stepper(g, 0, buf, m.bright_can_dec, m.bright_can_inc);

  settingRow(g, 1, C_PURPLE, ICON_MOON, "Таймаут экрана", "гаснет без касаний");
  stepper(g, 1, m.timeout_label ? m.timeout_label : "", m.timeout_can_dec, m.timeout_can_inc);

  if (m.sd == UI_SD_OK) {
    formatSize(a, sizeof(a), m.sd_total_mb > m.sd_used_mb ? m.sd_total_mb - m.sd_used_mb : 0);
    formatSize(b, sizeof(b), m.sd_total_mb);
    snprintf(buf, sizeof(buf), "%s, %s из %s свободно", m.sd_type ? m.sd_type : "SD", a, b);
  } else {
    strcpy(buf, "не вставлена");
  }
  settingRow(g, 2, m.sd == UI_SD_OK ? C_BLUE : C_SLATE, ICON_HDD, "SD-карта", buf, setPlusX() - 50);
  g.text(UI_FONT_LABEL, setPlusX() - 14, setRowY(2) + 38, m.sd == UI_SD_OK ? "ОК" : "НЕТ",
         m.sd == UI_SD_OK ? C_GREEN : C_GREY, ALIGN_RIGHT, 2);
  button(g, setPlusX(), setBtnY(2), ICON_REFRESH, true);

  // "Meshcorium v1 (Meshcore 1.17.1)"
  const char* core = m.fw_version && m.fw_version[0] == 'v' ? m.fw_version + 1 : m.fw_version;
  snprintf(buf, sizeof(buf), "%s %s (Meshcore %s)", FW_NAME, FW_VERSION, core ? core : "");
  settingRow(g, 3, C_TEAL, ICON_INFO, "Прошивка", buf, UI_W - GAP - 16);
}

UIAction hitTest(const UIModel& m, int x, int y) {
  if (m.page != UI_PAGE_SETTINGS) return UI_ACT_NONE;
  const int pad = 10;
  for (int i = 0; i < 3; i++) {
    int by = setBtnY(i);
    if (y < by - pad || y > by + SET_BTN + pad) continue;
    bool minus = x >= setMinusX() - pad && x <= setMinusX() + SET_BTN + pad;
    bool plus = x >= setPlusX() - pad && x <= setPlusX() + SET_BTN + pad;
    if (i == 0 && minus) return UI_ACT_BRIGHT_DEC;
    if (i == 0 && plus)  return UI_ACT_BRIGHT_INC;
    if (i == 1 && minus) return UI_ACT_TIMEOUT_DEC;
    if (i == 1 && plus)  return UI_ACT_TIMEOUT_INC;
    if (i == 2 && plus)  return UI_ACT_SD_REFRESH;
  }
  return UI_ACT_NONE;
}

// --------------------------------------------------------------- empty ----

static void renderEmpty(UIGfx& g, const UIModel& m) {
  g.fillRect(GAP, TILES_Y, UI_W - 2 * GAP, TILES_B - TILES_Y, C_SLATE);
  g.icon(UI_ICON_L, ICON_CUBE, UI_W / 2, 140, C_WHITE);
  label(g, UI_W / 2, 210, "ПОКА ПУСТО", C_WHITE, ALIGN_CENTER);
  g.text(UI_FONT_SMALL, UI_W / 2, 236, "место для будущих функций", C_GREY, ALIGN_CENTER);
}

// ---------------------------------------------------------------- fatal ----

void renderSplash(UIGfx& g, const char* core_version) {
  g.resetClip();
  g.clear(RGB(0x000000));

  (void)core_version;

  // "Meshcorium | V1" in one light line, the bar dimmed
  char ver[8];
  snprintf(ver, sizeof(ver), "%s", FW_VERSION);
  for (char* p = ver; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  const char* sep = "  |  ";
  int w1 = UIGfx::textWidth(UI_FONT_LOGO, FW_NAME, 1);
  int w2 = UIGfx::textWidth(UI_FONT_LOGO, sep, 1);
  int w3 = UIGfx::textWidth(UI_FONT_LOGO, ver, 1);
  int x = (UI_W - (w1 + w2 + w3)) / 2;
  const int y = 166;
  x += g.text(UI_FONT_LOGO, x, y, FW_NAME, C_WHITE, ALIGN_LEFT, 1);
  x += g.text(UI_FONT_LOGO, x, y, sep, C_DARK, ALIGN_LEFT, 1);
  g.text(UI_FONT_LOGO, x, y, ver, C_WHITE, ALIGN_LEFT, 1);

  g.text(UI_FONT_SMALL, UI_W / 2, y + 44, "Загрузка...", C_GREY, ALIGN_CENTER);
}

void renderRadioError(UIGfx& g, const char* core_version) {
  g.resetClip();
  g.clear(C_RED);
  const int x = 28;
  g.icon(UI_ICON_L, ICON_WARNING, x + 24, 56, C_WHITE);
  fitText(g, x, 124, UI_W - 2 * x, "LoRa module error!", C_WHITE, FIT_VALUE, 3);
  fitText(g, x, 162, UI_W - 2 * x, "Радиомодуль ebyte e22 не ответил.", C_WHITE, FIT_BODY, 2);
  fitText(g, x, 206, UI_W - 2 * x, "3 попытки запуска были неудачными.", C_WHITE, FIT_BODY, 2);

  char ver[48];
  const char* core = core_version && core_version[0] == 'v' ? core_version + 1 : core_version;
  snprintf(ver, sizeof(ver), "%s %s (%s)", FW_NAME, FW_VERSION, core ? core : "?");
  g.text(UI_FONT_SMALL, x, 270, ver, soft(C_RED));
  g.text(UI_FONT_SMALL, x, 294, "JC3248W535 Board.", soft(C_RED));
}

// --------------------------------------------------------------------------

void render(UIGfx& g, const UIModel& m) {
  g.resetClip();
  g.clear(C_BG);
  statusBar(g, m);
  switch (m.page) {
    case UI_PAGE_HOME:     renderHome(g, m); break;
    case UI_PAGE_POWER:    renderPower(g, m); break;
    case UI_PAGE_RADIO:    renderRadio(g, m); break;
    case UI_PAGE_SETTINGS: renderSettings(g, m); break;
    case UI_PAGE_CHATS:    if (m.chats) UIChats::render(g, *m.chats); break;
    case UI_PAGE_LINKS:    if (m.links) UIInterfaces::render(g, *m.links); break;
    case UI_PAGE_REPEATER: if (m.rep) UIRepeater::render(g, *m.rep); break;
    default:               renderEmpty(g, m); break;
  }
}

}  // namespace UIScreens
