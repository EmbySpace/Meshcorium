#include "UIChats.h"
#include "UIStyle.h"
#include <stdio.h>
#include <string.h>

using namespace UIStyle;

#define W           480
#define X0          UI_GAP
#define CW          (W - 2 * UI_GAP)          // content width

// header of sub screens (chat, new chat, contacts): back chevron, title, optional button
#define HDR_Y       UI_TOP
#define HDR_H       44
#define BODY_Y      (HDR_Y + HDR_H + UI_GAP)   // 84

// chat list
#define LIST_ROW_H  70
#define LIST_ROWS   3
#define LIST_BAR_Y  266
#define LIST_BAR_H  46

// contact picker
#define CT_ROW_H    50
#define CT_GAP      6
#define CT_ROWS     4

// chat view
#define MSG_TOP     BODY_Y
#define MSG_BOTTOM  262
#define INPUT_Y     268
#define INPUT_H     44
#define BUBBLE_MAX  320
#define BUBBLE_PAD  10
#define LINE_H      22

// keyboard
#define KB_FIELD_Y  UI_TOP
#define KB_FIELD_H  76
#define KB_Y        116
#define KB_KEY_H    44
#define KB_ROW_GAP  6
#define KB_KEY_GAP  4

namespace UIChats {

// ------------------------------------------------------------ helpers ----

static Color rowColor(uint8_t kind) {
  switch (kind) {
    case ROW_DM:               return C_ORANGE;
    case ROW_CHANNEL_PUBLIC:   return C_TEAL;
    case ROW_CHANNEL_PRIVATE:  return C_PURPLE;
    case ROW_CONTACT_CHAT:     return C_ORANGE;
    case ROW_CONTACT_REPEATER: return C_SLATE;
    case ROW_CONTACT_ROOM:     return C_PURPLE;
    case ROW_CONTACT_SENSOR:   return C_GREEN;
    default:                   return C_SLATE;
  }
}

static Color senderColor(const char* name) {
  static const Color pal[] = { C_ORANGE, C_TEAL, RGB(0xB98CFF), C_LIVE, RGB(0xFF6FAE), RGB(0x5FB3FF) };
  uint32_t h = 5381;
  for (const char* p = name; p && *p; p++) h = h * 33 + (uint8_t)*p;
  return pal[h % (sizeof(pal) / sizeof(pal[0]))];
}

// first letter of a name, upper case (UTF-8)
static void initial(const char* name, char* dest, size_t size) {
  const char* p = name;
  while (*p == ' ' || *p == '#') p++;
  if (!*p) { strncpy(dest, "?", size); return; }
  uint32_t cp;
  int n = UIGfx::decodeUTF8(p, cp);
  char one[5] = { 0 };
  memcpy(one, p, n < 4 ? n : 4);
  upper(one, dest, size);
}

void upper(const char* ch, char* dest, size_t size) {
  uint32_t cp;
  int n = UIGfx::decodeUTF8(ch, cp);
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  else if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  else if (cp == 0x451) cp = 0x401;
  else { size_t k = (size_t)n < size - 1 ? (size_t)n : size - 1; memcpy(dest, ch, k); dest[k] = 0; return; }
  if (cp < 0x80) { dest[0] = (char)cp; dest[1] = 0; }
  else { dest[0] = (char)(0xC0 | (cp >> 6)); dest[1] = (char)(0x80 | (cp & 0x3F)); dest[2] = 0; }
}

static void header(UIGfx& g, const char* title, uint16_t right_icon) {
  g.fillRect(X0, HDR_Y, CW, HDR_H, C_ROW);
  g.icon(UI_ICON_S, ICON_CHEVRON_LEFT, X0 + 22, HDR_Y + HDR_H / 2, C_WHITE);
  int right = right_icon ? 44 + 8 : 0;
  fitText(g, X0 + 44, HDR_Y + 29, CW - 44 - right - 12, title, C_WHITE, FIT_BODY, 2);
  if (right_icon) {
    g.fillRect(X0 + CW - 44, HDR_Y, 44, HDR_H, C_BTN);
    g.icon(UI_ICON_S, right_icon, X0 + CW - 22, HDR_Y + HDR_H / 2, C_WHITE);
  }
}

static void scrollbar(UIGfx& g, int y, int h, int first, int visible, int total) {
  if (total <= visible) return;
  g.fillRect(W - 4, y, 3, h, C_ROW);
  int th = h * visible / total; if (th < 16) th = 16;
  int ty = y + (h - th) * first / (total - visible);
  g.fillRect(W - 4, ty, 3, th, C_GREY);
}

static void toast(UIGfx& g, const char* text) {
  if (!text || !text[0]) return;
  g.fillRect(0, UI_BOTTOM - 40, W, 48, C_BLUE);
  fitText(g, W / 2, UI_BOTTOM - 13, W - 32, text, C_WHITE, FIT_BODY, 2, ALIGN_CENTER);
}

// ---------------------------------------------------------- list rows ----

static void rowSquare(UIGfx& g, int x, int y, int s, const UIChatRow& r) {
  Color c = rowColor(r.kind);
  g.fillRect(x, y, s, s, c);
  uint16_t icon = 0;
  switch (r.kind) {
    case ROW_CHANNEL_PUBLIC:   icon = ICON_HASHTAG; break;
    case ROW_CHANNEL_PRIVATE:  icon = ICON_LOCK; break;
    case ROW_CONTACT_REPEATER: icon = ICON_SERVER; break;
    case ROW_CONTACT_ROOM:     icon = ICON_COMMENTS; break;
    case ROW_CONTACT_SENSOR:   icon = ICON_TACHOMETER; break;
  }
  if (icon) {
    g.icon(UI_ICON_M, icon, x + s / 2, y + s / 2, C_WHITE);
  } else {
    char ini[8];
    initial(r.title, ini, sizeof(ini));
    g.text(UI_FONT_TITLE, x + s / 2, y + s / 2 + 8, ini, C_WHITE, ALIGN_CENTER);
  }
}

static void chatRow(UIGfx& g, int y, const UIChatRow& r) {
  g.fillRect(X0, y, CW, LIST_ROW_H, C_ROW);
  rowSquare(g, X0, y, LIST_ROW_H, r);
  int tx = X0 + LIST_ROW_H + 14, right = X0 + CW - 12;

  int tw = r.time[0] ? g.text(UI_FONT_LABEL, right, y + 26, r.time, C_GREY, ALIGN_RIGHT, 1) : 0;
  fitText(g, tx, y + 30, right - tx - tw - 10, r.title, C_WHITE, FIT_BODY, 1);

  int bw = 0;
  if (r.unread > 0) {
    char n[8];
    snprintf(n, sizeof(n), "%d", r.unread > 99 ? 99 : r.unread);
    bw = UIGfx::textWidth(UI_FONT_LABEL, n, 1) + 14;
    if (bw < 22) bw = 22;
    g.fillRect(right - bw, y + 40, bw, 20, C_BLUE);
    g.text(UI_FONT_LABEL, right - bw / 2, y + 55, n, C_WHITE, ALIGN_CENTER, 1);
    bw += 10;
  }
  fitText(g, tx, y + 55, right - tx - bw, r.preview, r.unread ? C_WHITE : C_GREY, FIT_SMALL, 1);
}

static void renderList(UIGfx& g, const UIChatsView& v) {
  if (v.row_count == 0) {
    g.fillRect(X0, UI_TOP, CW, LIST_BAR_Y - UI_GAP - UI_TOP, C_ROW);
    g.icon(UI_ICON_L, ICON_COMMENTS, W / 2, 110, C_DARK);
    g.text(UI_FONT_BODY_B, W / 2, 178, "Чатов пока нет", C_WHITE, ALIGN_CENTER);
    g.text(UI_FONT_SMALL, W / 2, 204, "создайте канал или личку с контактом", C_GREY, ALIGN_CENTER);
  }
  for (int i = 0; i < LIST_ROWS && v.row_scroll + i < v.row_count; i++) {
    chatRow(g, UI_TOP + i * (LIST_ROW_H + UI_GAP), v.rows[v.row_scroll + i]);
  }
  scrollbar(g, UI_TOP, LIST_ROWS * LIST_ROW_H + (LIST_ROWS - 1) * UI_GAP, v.row_scroll, LIST_ROWS, v.row_count);

  // actions: new chat (wide) + QR of this node
  int qw = LIST_BAR_H;
  int nw = CW - qw - UI_GAP;
  g.fillRect(X0, LIST_BAR_Y, nw, LIST_BAR_H, C_BLUE);
  int lw = UIGfx::textWidth(UI_FONT_BODY_B, "Новый чат");
  g.icon(UI_ICON_S, ICON_PLUS, X0 + nw / 2 - lw / 2 - 16, LIST_BAR_Y + LIST_BAR_H / 2, C_WHITE);
  g.text(UI_FONT_BODY_B, X0 + nw / 2 + 8, LIST_BAR_Y + 30, "Новый чат", C_WHITE, ALIGN_CENTER);
  g.fillRect(X0 + nw + UI_GAP, LIST_BAR_Y, qw, LIST_BAR_H, C_BTN);
  g.icon(UI_ICON_M, ICON_QRCODE, X0 + nw + UI_GAP + qw / 2, LIST_BAR_Y + LIST_BAR_H / 2, C_WHITE);
}

static void renderContacts(UIGfx& g, const UIChatsView& v) {
  header(g, "Личка: выберите контакт", 0);
  if (v.row_count == 0) {
    g.text(UI_FONT_BODY_B, W / 2, 180, "Контактов нет", C_WHITE, ALIGN_CENTER);
    g.text(UI_FONT_SMALL, W / 2, 206, "они появятся, когда ноды пришлют адверт", C_GREY, ALIGN_CENTER);
    return;
  }
  for (int i = 0; i < CT_ROWS && v.row_scroll + i < v.row_count; i++) {
    const UIChatRow& r = v.rows[v.row_scroll + i];
    int y = BODY_Y + i * (CT_ROW_H + CT_GAP);
    g.fillRect(X0, y, CW, CT_ROW_H, C_ROW);
    rowSquare(g, X0, y, CT_ROW_H, r);
    int tx = X0 + CT_ROW_H + 14;
    fitText(g, tx, y + 22, CW - CT_ROW_H - 40, r.title, C_WHITE, FIT_BODY, 1);
    fitText(g, tx, y + 42, CW - CT_ROW_H - 40, r.preview, C_GREY, FIT_SMALL, 1);
  }
  scrollbar(g, BODY_Y, CT_ROWS * CT_ROW_H + (CT_ROWS - 1) * CT_GAP, v.row_scroll, CT_ROWS, v.row_count);
}

int visibleRows(const UIChatsView& v) {
  return v.screen == CHATS_CONTACTS ? CT_ROWS : LIST_ROWS;
}

// ------------------------------------------------------------- chat view ----

// word-wraps text into up to max_lines lines of at most max_w pixels;
// returns the number of lines, line i = text[start[i] .. start[i] + len[i])
static int wrap(const UIFont& f, const char* text, int max_w, int* start, int* len, int max_lines) {
  int n = 0, pos = 0, total = strlen(text);
  while (pos < total && n < max_lines) {
    while (text[pos] == ' ') pos++;
    if (pos >= total) break;
    int line_end = pos, last_space = -1, w = 0, p = pos;
    while (p < total && text[p] != '\n') {
      uint32_t cp;
      int k = UIGfx::decodeUTF8(&text[p], cp);
      const UIGlyph* gl = UIGfx::findGlyph(f, cp);
      int gw = gl ? gl->adv : 8;
      if (w + gw > max_w) break;
      if (text[p] == ' ') last_space = p;
      w += gw; p += k;
    }
    if (p >= total || text[p] == '\n') line_end = p;
    else if (last_space > pos) line_end = last_space;
    else line_end = p > pos ? p : pos + 1;       // one long word: hard break
    start[n] = pos; len[n] = line_end - pos; n++;
    pos = line_end;
    if (pos < total && text[pos] == '\n') pos++;
  }
  return n;
}

#define BUBBLE_LINES 10

static int bubbleHeight(const UIBubble& b, int* lines_out, int* start, int* len) {
  int n = wrap(UI_FONT_BODY, b.text, BUBBLE_MAX - 2 * BUBBLE_PAD, start, len, BUBBLE_LINES);
  if (n == 0) n = 1, start[0] = 0, len[0] = 0;
  *lines_out = n;
  int h = BUBBLE_PAD + n * LINE_H + 18 + 4;
  if (b.status == BUBBLE_IN && b.sender && b.sender[0]) h += 18;
  return h;
}

static void bubble(UIGfx& g, int y, const UIBubble& b, int lines, const int* start, const int* len, int h) {
  bool out = b.status != BUBBLE_IN;
  // width = widest line
  int w = 0;
  char buf[220];
  for (int i = 0; i < lines; i++) {
    int n = len[i] < (int)sizeof(buf) - 1 ? len[i] : (int)sizeof(buf) - 1;
    memcpy(buf, b.text + start[i], n); buf[n] = 0;
    int lw = UIGfx::textWidth(UI_FONT_BODY, buf);
    if (lw > w) w = lw;
  }
  const char* st = "";
  switch (b.status) {
    case BUBBLE_SENDING:   st = "  \xE2\x80\xA2 отправка"; break;
    case BUBBLE_SENT:      st = "  \xE2\x80\xA2 отправлено"; break;
    case BUBBLE_DELIVERED: st = "  \xE2\x80\xA2 доставлено"; break;
    case BUBBLE_FAILED:    st = "  \xE2\x80\xA2 не доставлено"; break;
  }
  char meta[48];
  snprintf(meta, sizeof(meta), "%s%s", b.time, st);
  int mw = UIGfx::textWidth(UI_FONT_LABEL, meta, 1);
  bool has_sender = !out && b.sender && b.sender[0];
  int sw = has_sender ? UIGfx::textWidth(UI_FONT_LABEL, b.sender, 1) : 0;
  if (mw > w) w = mw;
  if (sw > w) w = sw;
  int bw = w + 2 * BUBBLE_PAD;
  if (bw > BUBBLE_MAX) bw = BUBBLE_MAX;

  Color bg = out ? (b.status == BUBBLE_FAILED ? C_RED : C_BLUE) : C_SLATE;
  int x = out ? X0 + CW - bw - 6 : X0;
  g.fillRect(x, y, bw, h, bg);

  int ty = y + BUBBLE_PAD;
  if (has_sender) {
    g.text(UI_FONT_LABEL, x + BUBBLE_PAD, ty + 11, b.sender, senderColor(b.sender), ALIGN_LEFT, 1);
    ty += 18;
  }
  for (int i = 0; i < lines; i++) {
    int n = len[i] < (int)sizeof(buf) - 1 ? len[i] : (int)sizeof(buf) - 1;
    memcpy(buf, b.text + start[i], n); buf[n] = 0;
    g.text(UI_FONT_BODY, x + BUBBLE_PAD, ty + 16, buf, C_WHITE);
    ty += LINE_H;
  }
  g.text(UI_FONT_LABEL, x + bw - BUBBLE_PAD, ty + 14, meta, soft(bg), ALIGN_RIGHT, 1);
}

static void renderChat(UIGfx& g, const UIChatsView& v) {
  bool channel = v.chat_kind == ROW_CHANNEL_PUBLIC || v.chat_kind == ROW_CHANNEL_PRIVATE;
  header(g, v.chat_title, ICON_QRCODE);

  g.setClip(0, MSG_TOP, W, MSG_BOTTOM - MSG_TOP);
  if (v.bubble_count == 0) {
    g.icon(UI_ICON_L, channel ? ICON_HASHTAG : ICON_COMMENT, W / 2, 150, C_DARK);
    g.text(UI_FONT_SMALL, W / 2, 212, "сообщений пока нет", C_GREY, ALIGN_CENTER);
  }
  int y = MSG_BOTTOM;
  int start[BUBBLE_LINES], len[BUBBLE_LINES], lines;
  for (int i = v.bubble_count - 1 - v.bubble_scroll; i >= 0 && y > MSG_TOP; i--) {
    const UIBubble& b = v.bubbles[i];
    int h = bubbleHeight(b, &lines, start, len);
    y -= h;
    bubble(g, y, b, lines, start, len, h);
    y -= 6;
  }
  g.resetClip();
  scrollbar(g, MSG_TOP, MSG_BOTTOM - MSG_TOP, v.bubble_count - 1 - v.bubble_scroll, 1, v.bubble_count);

  // input bar
  g.fillRect(X0, INPUT_Y, CW, INPUT_H, C_ROW);
  g.text(UI_FONT_SMALL, X0 + 16, INPUT_Y + 28, channel ? "Написать в канал..." : "Написать сообщение...", C_GREY);
  g.fillRect(X0 + CW - INPUT_H, INPUT_Y, INPUT_H, INPUT_H, C_BLUE);
  g.icon(UI_ICON_S, ICON_PAPER_PLANE, X0 + CW - INPUT_H / 2, INPUT_Y + INPUT_H / 2, C_WHITE);
}

// -------------------------------------------------------------- new chat ----

static void newTile(UIGfx& g, int x, int y, int w, int h, Color c, uint16_t icon, const char* title, const char* sub) {
  g.fillRect(x, y, w, h, c);
  g.icon(UI_ICON_M, icon, x + w - 26, y + 26, C_WHITE);
  fitText(g, x + 14, y + h - 36, w - 28, title, C_WHITE, FIT_TITLE, 3);
  g.text(UI_FONT_SMALL, x + 14, y + h - 14, sub, soft(c));
}

#define NEW_H  ((UI_BOTTOM - BODY_Y - UI_GAP) / 2)
#define NEW_W  ((CW - UI_GAP) / 2)

static void renderNew(UIGfx& g, const UIChatsView& v) {
  header(g, "Новый чат", 0);
  int y1 = BODY_Y + NEW_H + UI_GAP, x1 = X0 + NEW_W + UI_GAP;
  newTile(g, X0, BODY_Y, NEW_W, NEW_H, C_ORANGE, ICON_USER, "Личка", "с контактом из списка");
  newTile(g, x1, BODY_Y, NEW_W, NEW_H, C_PURPLE, ICON_LOCK, "Приватный канал", "вход только по QR");
  newTile(g, X0, y1, NEW_W, NEW_H, C_TEAL, ICON_HASHTAG, "Публичный #канал", "открыт по названию");
  newTile(g, x1, y1, NEW_W, NEW_H, C_BLUE, ICON_QRCODE, "Мой QR-код", "добавить эту ноду");
}

// -------------------------------------------------------------- keyboard ----

struct KeyDef { UIKeyType type; const char* text; uint8_t units2; };   // units2 = width in half units

#define K(c)   { KEY_CHAR, c, 2 }
static const KeyDef RU0[] = { K("й"),K("ц"),K("у"),K("к"),K("е"),K("н"),K("г"),K("ш"),K("щ"),K("з"),K("х"),K("ъ") };
static const KeyDef RU1[] = { K("ф"),K("ы"),K("в"),K("а"),K("п"),K("р"),K("о"),K("л"),K("д"),K("ж"),K("э") };
static const KeyDef RU2[] = { {KEY_SHIFT,"",3}, K("я"),K("ч"),K("с"),K("м"),K("и"),K("т"),K("ь"),K("б"),K("ю"), {KEY_BACKSPACE,"",3} };
static const KeyDef EN0[] = { K("q"),K("w"),K("e"),K("r"),K("t"),K("y"),K("u"),K("i"),K("o"),K("p") };
static const KeyDef EN1[] = { K("a"),K("s"),K("d"),K("f"),K("g"),K("h"),K("j"),K("k"),K("l") };
static const KeyDef EN2[] = { {KEY_SHIFT,"",3}, K("z"),K("x"),K("c"),K("v"),K("b"),K("n"),K("m"), {KEY_BACKSPACE,"",3} };
static const KeyDef SY0[] = { K("1"),K("2"),K("3"),K("4"),K("5"),K("6"),K("7"),K("8"),K("9"),K("0") };
static const KeyDef SY1[] = { K("-"),K("/"),K(":"),K(";"),K("("),K(")"),K("\""),K("@"),K("&"),K("!") };
static const KeyDef SY2[] = { K("?"),K(","),K("'"),K("#"),K("%"),K("*"),K("+"),K("="),K("ё"), {KEY_BACKSPACE,"",2} };
static const KeyDef RU3[] = { {KEY_SYM,"123",3}, {KEY_LANG,"EN",3}, {KEY_SPACE,"",10}, K("."), {KEY_ACTION,"",6} };
static const KeyDef EN3[] = { {KEY_SYM,"123",3}, {KEY_LANG,"RU",3}, {KEY_SPACE,"",6}, K("."), {KEY_ACTION,"",6} };
static const KeyDef SY3[] = { {KEY_SYM,"АБВ",3}, {KEY_LANG,"",0}, {KEY_SPACE,"",9}, K("."), {KEY_ACTION,"",6} };
#undef K

struct KbRow { const KeyDef* keys; int n; };
#define ROW(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
static const KbRow LAYOUTS[3][4] = {
  { ROW(RU0), ROW(RU1), ROW(RU2), ROW(RU3) },
  { ROW(EN0), ROW(EN1), ROW(EN2), ROW(EN3) },
  { ROW(SY0), ROW(SY1), ROW(SY2), ROW(SY3) },
};
#undef ROW

// half-unit width of the widest row of a layout -> pixel size of one half unit
static float halfUnit(int layout) {
  int max_units = 0, max_keys = 0;
  for (int r = 0; r < 4; r++) {
    int u = 0, k = 0;
    for (int i = 0; i < LAYOUTS[layout][r].n; i++) {
      if (LAYOUTS[layout][r].keys[i].units2) { u += LAYOUTS[layout][r].keys[i].units2; k++; }
    }
    if (u > max_units) { max_units = u; max_keys = k; }
  }
  return (float)(CW - (max_keys - 1) * KB_KEY_GAP) / max_units;
}

// pixel rect of key i in row r
static bool keyRect(int layout, int r, int i, int& x, int& y, int& w) {
  const KbRow& row = LAYOUTS[layout][r];
  if (!row.keys[i].units2) return false;
  float hu = halfUnit(layout);
  int total = 0, keys = 0;
  for (int k = 0; k < row.n; k++) if (row.keys[k].units2) { total += row.keys[k].units2; keys++; }
  int row_w = (int)(total * hu + (keys - 1) * KB_KEY_GAP);
  int px = X0 + (CW - row_w) / 2;
  for (int k = 0; k < i; k++) if (row.keys[k].units2) px += (int)(row.keys[k].units2 * hu) + KB_KEY_GAP;
  x = px; y = KB_Y + r * (KB_KEY_H + KB_ROW_GAP); w = (int)(row.keys[i].units2 * hu);
  return true;
}

void renderKeyboard(UIGfx& g, const UIKeyboardView& k) {
  // input field
  g.fillRect(X0, KB_FIELD_Y, CW, KB_FIELD_H, C_ROW);
  g.icon(UI_ICON_S, ICON_CHEVRON_LEFT, X0 + 18, KB_FIELD_Y + 16, C_GREY);
  label(g, X0 + 34, KB_FIELD_Y + 21, k.title, C_GREY);
  char cnt[16];
  snprintf(cnt, sizeof(cnt), "%d/%d", k.bytes, k.max_bytes);
  g.text(UI_FONT_LABEL, X0 + CW - 12, KB_FIELD_Y + 21, cnt, k.bytes >= k.max_bytes ? C_ORANGE : C_GREY, ALIGN_RIGHT, 1);

  // text: prefix + typed text, last two lines
  char full[240];
  snprintf(full, sizeof(full), "%s%s", k.prefix ? k.prefix : "", k.text ? k.text : "");
  int start[12], len[12];
  int n = wrap(UI_FONT_BODY_B, full, CW - 40, start, len, 12);
  int first = n > 2 ? n - 2 : 0;
  int cx = X0 + 16, cy = KB_FIELD_Y + 46;
  char buf[240];
  for (int i = first; i < n; i++) {
    memcpy(buf, full + start[i], len[i]); buf[len[i]] = 0;
    int lw = g.text(UI_FONT_BODY_B, X0 + 16, KB_FIELD_Y + 46 + (i - first) * 22, buf, C_WHITE);
    cx = X0 + 16 + lw; cy = KB_FIELD_Y + 46 + (i - first) * 22;
  }
  g.fillRect(cx + 2, cy - 16, 2, 20, C_BLUE);   // cursor

  // keys
  for (int r = 0; r < 4; r++) {
    const KbRow& row = LAYOUTS[k.layout][r];
    for (int i = 0; i < row.n; i++) {
      int x, y, w;
      if (!keyRect(k.layout, r, i, x, y, w)) continue;
      const KeyDef& kd = row.keys[i];
      Color bg = C_BTN;
      switch (kd.type) {
        case KEY_CHAR: case KEY_SPACE: bg = RGB(0x2B2F36); break;
        case KEY_ACTION: bg = C_BLUE; break;
        case KEY_SHIFT: bg = k.shift ? C_WHITE : C_BTN; break;
        default: bg = C_BTN; break;
      }
      g.fillRect(x, y, w, KB_KEY_H, bg);
      int mx = x + w / 2, my = y + KB_KEY_H / 2;
      switch (kd.type) {
        case KEY_CHAR: {
          char up[8];
          if (k.shift) upper(kd.text, up, sizeof(up)); else { strncpy(up, kd.text, sizeof(up)); up[7] = 0; }
          g.text(UI_FONT_TITLE, mx, my + 8, up, C_WHITE, ALIGN_CENTER);
          break;
        }
        case KEY_SHIFT:     g.icon(UI_ICON_S, ICON_ARROW_UP, mx, my, k.shift ? C_BG : C_WHITE); break;
        case KEY_BACKSPACE: g.icon(UI_ICON_S, ICON_BACKSPACE, mx, my, C_WHITE); break;
        case KEY_SPACE:     g.fillRect(mx - 20, my + 6, 40, 2, C_GREY); break;
        case KEY_LANG: case KEY_SYM:
          g.text(UI_FONT_BODY_B, mx, my + 6, kd.text, C_WHITE, ALIGN_CENTER); break;
        case KEY_ACTION:
          fitText(g, mx, my + 6, w - 8, k.action ? k.action : "OK", C_WHITE, FIT_BODY, 2, ALIGN_CENTER); break;
      }
    }
  }
}

// ------------------------------------------------------------------- QR ----

static void renderQR(UIGfx& g, const UIChatsView& v) {
  const int px = X0, py = UI_TOP, ps = UI_BOTTOM - UI_TOP;      // white square panel
  g.fillRect(px, py, ps, ps, C_WHITE);
  if (v.qr && v.qr_size > 0) {
    int quiet = 12;
    int m = (ps - 2 * quiet) / v.qr_size;
    int size = m * v.qr_size;
    int ox = px + (ps - size) / 2, oy = py + (ps - size) / 2;
    for (int yy = 0; yy < v.qr_size; yy++)
      for (int xx = 0; xx < v.qr_size; xx++)
        if (v.qr[yy * v.qr_size + xx]) g.fillRect(ox + xx * m, oy + yy * m, m, m, C_BG);
  } else {
    g.text(UI_FONT_BODY_B, px + ps / 2, py + ps / 2, "QR не получился", C_BG, ALIGN_CENTER);
  }

  int tx = px + ps + 16, tw = W - UI_GAP - tx;
  label(g, tx, UI_TOP + 22, "QR-КОД", C_GREY);
  fitText(g, tx, UI_TOP + 54, tw, v.qr_title, C_WHITE, FIT_TITLE, 3);
  for (int i = 0; i < 3; i++) {
    if (v.qr_lines[i][0]) fitText(g, tx, UI_TOP + 88 + i * 22, tw, v.qr_lines[i], C_GREY, FIT_SMALL, 1);
  }
  g.text(UI_FONT_SMALL, tx, UI_BOTTOM - 30, "Коснитесь экрана,", C_GREY);
  g.text(UI_FONT_SMALL, tx, UI_BOTTOM - 8, "чтобы закрыть", C_GREY);
}

// ---------------------------------------------------------------------------

void render(UIGfx& g, const UIChatsView& v) {
  switch (v.screen) {
    case CHATS_LIST:     renderList(g, v); break;
    case CHATS_VIEW:     renderChat(g, v); break;
    case CHATS_NEW:      renderNew(g, v); break;
    case CHATS_CONTACTS: renderContacts(g, v); break;
    case CHATS_KEYBOARD: renderKeyboard(g, v.kb); break;
    case CHATS_QR:       renderQR(g, v); break;
  }
  toast(g, v.toast);
}

static bool in(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

UIChatHit hitTestKeyboard(const UIKeyboardView& k, int x, int y) {
  UIChatHit h;
  memset(&h, 0, sizeof(h));
  h.type = HIT_NONE;
  if (in(x, y, 0, KB_FIELD_Y, 160, 30)) { h.type = HIT_BACK; return h; }
  for (int r = 0; r < 4; r++) {
    const KbRow& row = LAYOUTS[k.layout][r];
    for (int i = 0; i < row.n; i++) {
      int kx, ky, kw;
      if (!keyRect(k.layout, r, i, kx, ky, kw)) continue;
      // the key gaps belong to the nearest key, so no tap is lost
      if (in(x, y, kx - KB_KEY_GAP / 2, ky - KB_ROW_GAP / 2, kw + KB_KEY_GAP, KB_KEY_H + KB_ROW_GAP)) {
        h.type = HIT_KEY;
        h.key.type = row.keys[i].type;
        h.key.text = row.keys[i].text;
        return h;
      }
    }
  }
  return h;
}

UIChatHit hitTest(const UIChatsView& v, int x, int y) {
  UIChatHit h;
  memset(&h, 0, sizeof(h));
  h.type = HIT_NONE;

  switch (v.screen) {
    case CHATS_LIST: {
      if (in(x, y, X0, LIST_BAR_Y, CW - LIST_BAR_H - UI_GAP, LIST_BAR_H)) { h.type = HIT_NEW_CHAT; break; }
      if (in(x, y, X0 + CW - LIST_BAR_H, LIST_BAR_Y, LIST_BAR_H, LIST_BAR_H)) { h.type = HIT_MY_QR; break; }
      for (int i = 0; i < LIST_ROWS && v.row_scroll + i < v.row_count; i++) {
        if (in(x, y, X0, UI_TOP + i * (LIST_ROW_H + UI_GAP), CW, LIST_ROW_H)) { h.type = HIT_ROW; h.index = v.row_scroll + i; }
      }
      break;
    }
    case CHATS_CONTACTS:
      if (in(x, y, 0, HDR_Y, 120, HDR_H)) { h.type = HIT_BACK; break; }
      for (int i = 0; i < CT_ROWS && v.row_scroll + i < v.row_count; i++) {
        if (in(x, y, X0, BODY_Y + i * (CT_ROW_H + CT_GAP), CW, CT_ROW_H)) { h.type = HIT_ROW; h.index = v.row_scroll + i; }
      }
      break;
    case CHATS_VIEW:
      if (in(x, y, 0, HDR_Y, 120, HDR_H)) h.type = HIT_BACK;
      else if (in(x, y, X0 + CW - 60, HDR_Y, 60, HDR_H)) h.type = HIT_CHAT_QR;
      else if (in(x, y, X0, INPUT_Y - 4, CW, INPUT_H + 8)) h.type = HIT_COMPOSE;
      break;
    case CHATS_NEW: {
      if (in(x, y, 0, HDR_Y, 120, HDR_H)) { h.type = HIT_BACK; break; }
      int y1 = BODY_Y + NEW_H + UI_GAP, x1 = X0 + NEW_W + UI_GAP;
      if (in(x, y, X0, BODY_Y, NEW_W, NEW_H)) h.type = HIT_NEW_DM;
      else if (in(x, y, x1, BODY_Y, NEW_W, NEW_H)) h.type = HIT_NEW_PRIVATE;
      else if (in(x, y, X0, y1, NEW_W, NEW_H)) h.type = HIT_NEW_PUBLIC;
      else if (in(x, y, x1, y1, NEW_W, NEW_H)) h.type = HIT_MY_QR;
      break;
    }
    case CHATS_KEYBOARD:
      return hitTestKeyboard(v.kb, x, y);
    case CHATS_QR:
      h.type = HIT_CLOSE;
      break;
  }
  return h;
}

}  // namespace UIChats
