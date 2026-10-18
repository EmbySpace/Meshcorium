// Renders every page of the JC3248W535 UI with sample data to BMP files,
// using exactly the firmware's drawing code (UIGfx + UIScreens).
//
//   python -m ziglang c++ -O2 -std=c++17 -I.. preview.cpp ../UIGfx.cpp ../UIScreens.cpp -o preview.exe
//   preview.exe <output dir>

#include "../UIScreens.h"
#include "../UIChats.h"
#include "../UIInterfaces.h"
#include "TxTestUI.h"
#include "../UIRepeater.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static void writeBMP(const char* path, const uint16_t* px, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  int row = w * 3, size = 54 + row * h;
  uint8_t hdr[54] = { 'B', 'M' };
  auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) hdr[o + i] = (v >> (8 * i)) & 0xFF; };
  put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, (uint32_t)-h);
  hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
  fwrite(hdr, 1, 54, f);
  std::vector<uint8_t> line(row);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint16_t c = px[y * w + x];
      uint8_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      line[x * 3 + 0] = (b << 3) | (b >> 2);
      line[x * 3 + 1] = (g << 2) | (g >> 4);
      line[x * 3 + 2] = (r << 3) | (r >> 2);
    }
    fwrite(line.data(), 1, row, f);
  }
  fclose(f);
}

static UIModel sample() {
  UIModel m;
  memset(&m, 0, sizeof(m));
  strcpy(m.node_name, "Тихон База");
  m.contacts = 27; m.max_contacts = 350;
  m.time_valid = true; m.hour = 14; m.minute = 5; m.day = 17; m.month = 10; m.year = 2026; m.weekday = 6;
  m.ble = UI_LINK_CLIENT; m.wifi = UI_LINK_READY; m.usb = UI_LINK_READY;
  m.ble_pin = 123456;
  strcpy(m.wifi_ssid, "HomeNet_5G");
  m.wifi_ip = 192 | (168 << 8) | (1 << 16) | (57u << 24);
  m.batt_mv = 3980; m.trend = UI_BATT_DISCHARGING; m.uptime_s = 2 * 86400 + 3 * 3600 + 15 * 60;
  m.freq_mhz = 869.525f; m.bw_khz = 62.5f; m.sf = 8; m.cr = 5; m.tx_power = 20;
  strcpy(m.region, "Москва");
  m.noise_dbm = -112;
  m.brightness_pct = 80; m.timeout_label = "1 мин";
  m.bright_can_dec = true; m.bright_can_inc = true; m.timeout_can_dec = true; m.timeout_can_inc = true;
  m.fw_version = "v1.17.1";
  m.sd = UI_SD_OK; m.sd_total_mb = 30436; m.sd_used_mb = 2150; m.sd_type = "SDHC";
  return m;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  std::vector<uint16_t> buf(UI_W * UI_H);
  UIGfx g(buf.data(), UI_W, UI_H);
  char path[512];

  for (int p = 0; p < UI_PAGE_COUNT; p++) {
    UIModel m = sample();
    m.page = p;
    UIScreens::render(g, m);
    snprintf(path, sizeof(path), "%s/page%d.bmp", dir, p + 1);
    writeBMP(path, buf.data(), UI_W, UI_H);
  }

  // variants: no clock / low battery / charging / no Wi-Fi
  UIModel m = sample();
  m.page = UI_PAGE_HOME; m.time_valid = false; m.ble = UI_LINK_READY; m.wifi = UI_LINK_ABSENT; m.wifi_ssid[0] = 0;
  strcpy(m.node_name, "Длинное имя ноды 1");   // node_name holds 31 bytes of UTF-8
  m.batt_mv = 3620;
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/variant_home.bmp", dir);
  writeBMP(path, buf.data(), UI_W, UI_H);

  m = sample();
  m.page = UI_PAGE_POWER; m.batt_mv = 3560; m.trend = UI_BATT_CHARGING; m.uptime_s = 754;
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/variant_power.bmp", dir);
  writeBMP(path, buf.data(), UI_W, UI_H);

  m = sample();
  m.page = UI_PAGE_SETTINGS; m.sd = UI_SD_NONE; m.brightness_pct = 100; m.bright_can_inc = false;
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/variant_settings.bmp", dir);
  writeBMP(path, buf.data(), UI_W, UI_H);

  UIScreens::renderRadioError(g, "v1.17.1");
  snprintf(path, sizeof(path), "%s/fatal.bmp", dir);
  writeBMP(path, buf.data(), UI_W, UI_H);

  // ---- chats ----
  static UIChatRow rows[] = {
    { ROW_CHANNEL_PUBLIC, "Public", "Олег: кто на связи в районе Химок?", "14:02", 3 },
    { ROW_DM, "Анна", "Буду через 10 минут", "13:47", 1 },
    { ROW_CHANNEL_PRIVATE, "Семья", "Вы: Доехал, всё ок", "вчера", 0 },
    { ROW_CHANNEL_PUBLIC, "#moscow", "", "", 0 },
  };
  static UIChatRow contacts[] = {
    { ROW_CONTACT_CHAT, "Анна", "компаньон", "", 0 },
    { ROW_CONTACT_REPEATER, "Химки-RPT", "ретранслятор", "", 0 },
    { ROW_CONTACT_ROOM, "Клуб радио", "комната", "", 0 },
    { ROW_CONTACT_CHAT, "Олег Петров", "компаньон", "", 0 },
  };
  static UIBubble bubbles[] = {
    { BUBBLE_IN, "Олег", "Всем привет! Кто на связи в районе Химок?", "13:58" },
    { BUBBLE_IN, "Марина", "Я тут, сигнал хороший, через ретранслятор на Сходне", "14:00" },
    { BUBBLE_SENT, NULL, "На связи, Тихон База. Слышу всех отлично", "14:01" },
    { BUBBLE_IN, "Олег", "Отлично, спасибо!", "14:02" },
  };
  static UIChatsView cv;
  static uint8_t qr[29 * 29];
  for (int y = 0; y < 29; y++) for (int x = 0; x < 29; x++) {
    bool finder = (x < 7 && y < 7) || (x > 21 && y < 7) || (x < 7 && y > 21);
    if (finder) {
      int fx = x > 21 ? x - 22 : x, fy = y > 21 ? y - 22 : y;
      qr[y * 29 + x] = (fx == 0 || fx == 6 || fy == 0 || fy == 6 || (fx >= 2 && fx <= 4 && fy >= 2 && fy <= 4));
    } else {
      qr[y * 29 + x] = ((x * 7 + y * 13 + x * y) % 5) < 2;
    }
  }
  struct Shot { UIChatScreen s; const char* name; } shots[] = {
    { CHATS_LIST, "chats_list" }, { CHATS_VIEW, "chats_view" }, { CHATS_NEW, "chats_new" },
    { CHATS_CONTACTS, "chats_contacts" }, { CHATS_KEYBOARD, "chats_kb_ru" }, { CHATS_QR, "chats_qr" },
  };
  for (auto& sh : shots) {
    memset(&cv, 0, sizeof(cv));
    cv.screen = sh.s;
    cv.rows = sh.s == CHATS_CONTACTS ? contacts : rows; cv.row_count = 4;
    strcpy(cv.chat_title, "Public"); cv.chat_kind = ROW_CHANNEL_PUBLIC;
    cv.bubbles = bubbles; cv.bubble_count = 4;
    strcpy(cv.kb.title, "СООБЩЕНИЕ В PUBLIC"); cv.kb.text = "Всем привет, я на связи из Химок";
    cv.kb.bytes = 58; cv.kb.max_bytes = 150; cv.kb.layout = KB_RU; cv.kb.action = "Отпр.";
    cv.qr = qr; cv.qr_size = 29; strcpy(cv.qr_title, "Тихон База");
    strcpy(cv.qr_lines[0], "Эта нода как контакт.");
    strcpy(cv.qr_lines[1], "В приложении: Контакты,");
    strcpy(cv.qr_lines[2], "+ и сканировать QR");
    m = sample();
    m.page = UI_PAGE_CHATS; m.chats = &cv; m.chats_unread = 4;
    UIScreens::render(g, m);
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, sh.name);
    writeBMP(path, buf.data(), UI_W, UI_H);
  }
  memset(&cv, 0, sizeof(cv));
  cv.screen = CHATS_KEYBOARD; strcpy(cv.kb.title, "ПУБЛИЧНЫЙ КАНАЛ"); cv.kb.prefix = "#"; cv.kb.text = "moscow";
  cv.kb.bytes = 7; cv.kb.max_bytes = 31; cv.kb.layout = KB_EN; cv.kb.action = "Создать";
  m = sample(); m.page = UI_PAGE_CHATS; m.chats = &cv;
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/chats_kb_en.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);
  cv.kb.layout = KB_SYM; cv.kb.prefix = NULL; cv.kb.text = "Встреча в 18:30"; strcpy(cv.toast, "Канал #moscow создан");
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/chats_kb_sym.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);

  UIScreens::renderSplash(g, "v1.17.1");
  snprintf(path, sizeof(path), "%s/splash.bmp", dir);
  writeBMP(path, buf.data(), UI_W, UI_H);

  // ---- interfaces ----
  static UIIfView iv;
  auto ifBase = [&]() {
    memset(&iv, 0, sizeof(iv));
    iv.link[0] = { true, true, true, { "PIN 123456 (постоянный)", "подключён телефон" } };
    iv.link[1] = { true, true, true, { "Сеть HomeNet_5G", "192.168.1.57 : 5000" } };
    iv.link[2] = { true, false, false, { "выключен", "кабель подключён" } };
  };
  auto ifShot = [&](const char* name) {
    m = sample(); m.page = UI_PAGE_LINKS; m.links = &iv;
    UIScreens::render(g, m);
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    writeBMP(path, buf.data(), UI_W, UI_H);
  };
  auto row = [&](int i, uint8_t kind, const char* title, const char* value, const char* o0 = NULL, const char* o1 = NULL, int sel = 0) {
    UIIfRow& r = iv.rows[i];
    r.kind = kind; strcpy(r.title, title); strcpy(r.value, value ? value : "");
    r.option[0] = o0; r.option[1] = o1; r.selected = sel;
  };
  ifBase(); iv.screen = IF_MAIN; ifShot("links_main");

  ifBase(); iv.screen = IF_DETAIL; strcpy(iv.title, "Bluetooth");
  row(0, IFR_CHOICE, "PIN-код", NULL, "Постоянный", "Случайный", 0);
  row(1, IFR_EDIT, "Постоянный PIN", "123456");
  row(2, IFR_INFO, "Действует сейчас", "654321");
  iv.row_count = 3; iv.button = "Перезагрузить"; iv.button_active = true;
  strcpy(iv.note, "Новый PIN - после перезагрузки");
  ifShot("links_ble");

  ifBase(); iv.screen = IF_DETAIL; strcpy(iv.title, "Wi-Fi");
  row(0, IFR_CHOICE, "Режим", NULL, "Сеть", "Точка доступа", 0);
  row(1, IFR_EDIT, "Сеть (SSID)", "HomeNet_5G");
  row(2, IFR_EDIT, "Пароль", "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
  row(3, IFR_CHOICE, "IP-адрес", NULL, "DHCP", "Статический", 1);
  row(4, IFR_EDIT, "IP", "192.168.1.50");
  row(5, IFR_EDIT, "Шлюз", "192.168.1.1");
  row(6, IFR_EDIT, "Маска", "255.255.255.0");
  row(7, IFR_EDIT, "Порт TCP", "5000");
  iv.row_count = 8; iv.button = "Применить"; iv.button_active = true; strcpy(iv.note, "Есть несохранённые изменения");
  ifShot("links_wifi");
  iv.scroll = 3; iv.button_active = false; iv.note[0] = 0; strcpy(iv.note, "Подключено: 192.168.1.57");
  ifShot("links_wifi_scrolled");

  ifBase(); iv.screen = IF_DETAIL; strcpy(iv.title, "Wi-Fi");
  row(0, IFR_CHOICE, "Режим", NULL, "Сеть", "Точка доступа", 1);
  row(1, IFR_EDIT, "Имя точки", "MeshCore-3F2A");
  row(2, IFR_EDIT, "Пароль", "meshcore");
  row(3, IFR_INFO, "IP-адрес", "192.168.4.1");
  row(4, IFR_EDIT, "Порт TCP", "5000");
  iv.row_count = 5; iv.button = "Применить"; iv.button_active = false; strcpy(iv.note, "Точка доступа работает");
  ifShot("links_ap");

  ifBase(); iv.screen = IF_KEYBOARD;
  strcpy(iv.kb.title, "IP-АДРЕС"); iv.kb.text = "192.168.1.5"; iv.kb.bytes = 11; iv.kb.max_bytes = 15;
  iv.kb.layout = KB_SYM; iv.kb.action = "Готово";
  ifShot("links_kb");

  // ---- repeater ----
  static UIRepView rv;
  memset(&rv, 0, sizeof(rv));
  rv.repeat_on = true; rv.as_repeater = true; rv.freq_mhz = 869.618f;
  rv.local_label = "15 мин"; rv.flood_label = "12 ч";
  rv.local_dec = rv.local_inc = rv.flood_dec = rv.flood_inc = true;
  strcpy(rv.local_note, "последний: 4 мин назад");
  strcpy(rv.flood_note, "ещё не отправлялся");
  m = sample(); m.page = UI_PAGE_REPEATER; m.rep = &rv;
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/repeater.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);
  rv.repeat_on = false; rv.as_repeater = false; rv.flood_dec = false; rv.flood_label = "выкл";
  strcpy(rv.toast, "Анонс отправлен рядом");
  UIScreens::render(g, m);
  snprintf(path, sizeof(path), "%s/repeater_off.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);

  // ---- transmitter test ----
  TxView tv;
  memset(&tv, 0, sizeof(tv));
  tv.radio_ok = true; tv.power_dbm = 20; tv.freq_mhz = 869.618f; tv.limit_ms = 180000; tv.batt_mv = 3910;
  TxTestUI::render(g, tv);
  snprintf(path, sizeof(path), "%s/tx_idle.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);
  tv.running = true; tv.packets = 412; tv.bytes = 412 * 255; tv.elapsed_ms = 95000; tv.air_ms = 93800;
  TxTestUI::render(g, tv);
  snprintf(path, sizeof(path), "%s/tx_run.bmp", dir); writeBMP(path, buf.data(), UI_W, UI_H);

  printf("done\n");
  return 0;
}
