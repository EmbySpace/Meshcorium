#include "InterfacesController.h"
#include "../MyMesh.h"
#include <Preferences.h>

#define NVS_NS      "jc3248if"
#define TOAST_MS    2500
#define AP_IP_TEXT  "192.168.4.1"

static void copyStr(char* dest, size_t size, const char* src) {
  strncpy(dest, src ? src : "", size - 1);
  dest[size - 1] = 0;
}

static void masked(const char* pass, char* dest, size_t size) {
  if (!pass[0]) { copyStr(dest, size, "нет"); return; }
  size_t n = strlen(pass), o = 0;
  if (n > 10) n = 10;
  for (size_t i = 0; i < n && o + 4 < size; i++) { memcpy(dest + o, "\xE2\x80\xA2", 3); o += 3; }
  dest[o] = 0;
}

void InterfacesController::begin(MultiSerialInterface* ifm, WifiManager* wifi) {
  _ifm = ifm;
  _wifi = wifi;
  Preferences p;
  p.begin(NVS_NS, true);
  _ble_on = p.getBool("ble", true);
  _usb_on = p.getBool("usb", true);
  p.end();
  if (!_ble_on) _ifm->disableBluetooth();
  if (!_usb_on) _ifm->setInterfaceEnabled(InterfaceType::USB, false);
  _boot_ble_pin = the_mesh.getNodePrefs()->ble_pin;
  if (_wifi) _w = _wifi->settings();
  _view.screen = IF_MAIN;
  buildMain();
}

void InterfacesController::saveSwitches() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putBool("ble", _ble_on);
  p.putBool("usb", _usb_on);
  p.end();
}

void InterfacesController::showToast(const char* t) {
  copyStr(_view.toast, sizeof(_view.toast), t);
  _toast_until = millis() + TOAST_MS;
  _dirty = true;
}

void InterfacesController::setVisible(bool v) {
  _visible = v;
  if (v) { buildMain(); if (_view.screen == IF_DETAIL) buildDetail(); }
  _dirty = true;
}

// ------------------------------------------------------------------ main ----

void InterfacesController::buildMain() {
  UIIfLink l[IF_LINK_COUNT];
  memset(l, 0, sizeof(l));
  char ip[20];

  // Bluetooth
  uint32_t pin = the_mesh.getBLEPin();
  l[IF_LINK_BLE].present = _ifm->hasInterface(InterfaceType::Bluetooth);
  l[IF_LINK_BLE].on = _ble_on;
  l[IF_LINK_BLE].has_details = true;
  if (_ble_on) {
    snprintf(l[IF_LINK_BLE].status[0], 48, "PIN %06lu (%s)", (unsigned long)pin,
             _boot_ble_pin ? "постоянный" : "случайный");
    copyStr(l[IF_LINK_BLE].status[1], 48, _ifm->isInterfaceConnected(InterfaceType::Bluetooth)
            ? "подключён телефон" : "ожидание подключения");
  } else {
    copyStr(l[IF_LINK_BLE].status[0], 48, "выключен");
  }

  // Wi-Fi
  l[IF_LINK_WIFI].present = _wifi != NULL;
  l[IF_LINK_WIFI].has_details = true;
  if (_wifi) {
    const WifiSettings& s = _wifi->settings();
    l[IF_LINK_WIFI].on = s.enabled;
    if (!s.enabled) {
      copyStr(l[IF_LINK_WIFI].status[0], 48, "выключен");
      copyStr(l[IF_LINK_WIFI].status[1], 48, s.mode == JCWIFI_NETWORK && !s.ssid[0] ? "сеть не настроена" : "");
    } else {
      snprintf(l[IF_LINK_WIFI].status[0], 48, "%s %s", s.mode == JCWIFI_AP ? "Точка" : "Сеть", _wifi->currentSsid());
      if (_wifi->isUp()) {
        WifiManager::formatIP(_wifi->ip(), ip, sizeof(ip));
        if (_wifi->clientConnected()) {
          snprintf(l[IF_LINK_WIFI].status[1], 48, "%s : %u  \xE2\x80\xA2 приложение", ip, s.port);
        } else if (s.mode == JCWIFI_AP) {
          snprintf(l[IF_LINK_WIFI].status[1], 48, "%s : %u  \xE2\x80\xA2 устройств: %d", ip, s.port, _wifi->stations());
        } else {
          snprintf(l[IF_LINK_WIFI].status[1], 48, "%s : %u", ip, s.port);
        }
      } else {
        copyStr(l[IF_LINK_WIFI].status[1], 48, "подключение...");
      }
    }
  }

  // USB
  l[IF_LINK_USB].present = _ifm->hasInterface(InterfaceType::USB);
  l[IF_LINK_USB].on = _usb_on;
  copyStr(l[IF_LINK_USB].status[0], 48, _usb_on ? "включён" : "выключен");
  copyStr(l[IF_LINK_USB].status[1], 48, Serial.isPlugged() ? "кабель подключён к ПК" : "кабель не подключён");

  if (memcmp(l, _view.link, sizeof(l)) != 0) {
    memcpy(_view.link, l, sizeof(l));
    if (_view.screen == IF_MAIN) _dirty = true;
  }
}

void InterfacesController::toggle(int link) {
  switch (link) {
    case IF_LINK_BLE:
      _ble_on = !_ble_on;
      if (_ble_on) _ifm->enableBluetooth(); else _ifm->disableBluetooth();
      saveSwitches();
      break;
    case IF_LINK_USB:
      _usb_on = !_usb_on;
      _ifm->setInterfaceEnabled(InterfaceType::USB, _usb_on);
      saveSwitches();
      break;
    case IF_LINK_WIFI: {
      if (!_wifi) break;
      const WifiSettings& s = _wifi->settings();
      if (!s.enabled && s.mode == JCWIFI_NETWORK && !s.ssid[0]) {
        openDetail(IF_LINK_WIFI);
        showToast("Сначала укажите сеть Wi-Fi");
        return;
      }
      _wifi->setEnabled(!s.enabled);
      _w = _wifi->settings();
      break;
    }
  }
  buildMain();
  _dirty = true;
}

// ---------------------------------------------------------------- detail ----

void InterfacesController::openDetail(int link) {
  _view.detail = link;
  _view.scroll = 0;
  if (link == IF_LINK_WIFI && _wifi) _w = _wifi->settings();
  _view.screen = IF_DETAIL;
  buildDetail();
}

bool InterfacesController::wifiChanged() const {
  if (!_wifi) return false;
  WifiSettings a = _w, b = _wifi->settings();
  a.enabled = b.enabled = false;
  return memcmp(&a, &b, sizeof(a)) != 0;
}

static UIIfRow& addRow(UIIfView& v, uint8_t kind, uint8_t id, const char* title) {
  UIIfRow& r = v.rows[v.row_count++];
  memset(&r, 0, sizeof(r));
  r.kind = kind;
  r.id = id;
  copyStr(r.title, sizeof(r.title), title);
  return r;
}

void InterfacesController::buildDetail() {
  UIIfView& v = _view;
  v.row_count = 0;
  v.button = NULL;
  v.button_active = false;
  v.note[0] = 0;

  if (v.detail == IF_LINK_BLE) {
    copyStr(v.title, sizeof(v.title), "Bluetooth");
    NodePrefs* p = the_mesh.getNodePrefs();
    UIIfRow& m = addRow(v, IFR_CHOICE, F_PIN_MODE, "PIN-код");
    m.option[0] = "Постоянный"; m.option[1] = "Случайный"; m.selected = p->ble_pin ? 0 : 1;
    if (p->ble_pin) snprintf(addRow(v, IFR_EDIT, F_PIN, "Постоянный PIN").value, 48, "%06lu", (unsigned long)p->ble_pin);
    snprintf(addRow(v, IFR_INFO, F_PIN_ACTIVE, "Действует сейчас").value, 48, "%06lu", (unsigned long)the_mesh.getBLEPin());
    if (p->ble_pin != _boot_ble_pin) {
      v.button = "Перезагрузить";
      v.button_active = true;
      copyStr(v.note, sizeof(v.note), "Новый PIN - после перезагрузки");
    } else if (!p->ble_pin) {
      copyStr(v.note, sizeof(v.note), "Случайный PIN меняется при каждом запуске");
    }
  } else if (v.detail == IF_LINK_WIFI && _wifi) {
    copyStr(v.title, sizeof(v.title), "Wi-Fi");
    UIIfRow& m = addRow(v, IFR_CHOICE, F_MODE, "Режим");
    m.option[0] = "Сеть"; m.option[1] = "Точка доступа"; m.selected = _w.mode == JCWIFI_AP ? 1 : 0;
    if (_w.mode == JCWIFI_NETWORK) {
      copyStr(addRow(v, IFR_EDIT, F_SSID, "Сеть (SSID)").value, 48, _w.ssid[0] ? _w.ssid : "не задана");
      masked(_w.pass, addRow(v, IFR_EDIT, F_PASS, "Пароль").value, 48);
      UIIfRow& d = addRow(v, IFR_CHOICE, F_DHCP, "IP-адрес");
      d.option[0] = "DHCP"; d.option[1] = "Статический"; d.selected = _w.dhcp ? 0 : 1;
      if (!_w.dhcp) {
        WifiManager::formatIP(_w.ip, addRow(v, IFR_EDIT, F_IP, "IP").value, 48);
        WifiManager::formatIP(_w.gateway, addRow(v, IFR_EDIT, F_GW, "Шлюз").value, 48);
        WifiManager::formatIP(_w.mask, addRow(v, IFR_EDIT, F_MASK, "Маска").value, 48);
      }
    } else {
      copyStr(addRow(v, IFR_EDIT, F_AP_SSID, "Имя точки").value, 48, _w.ap_ssid);
      copyStr(addRow(v, IFR_EDIT, F_AP_PASS, "Пароль").value, 48, _w.ap_pass[0] ? _w.ap_pass : "нет (открытая)");
      copyStr(addRow(v, IFR_INFO, F_AP_IP, "IP-адрес").value, 48, AP_IP_TEXT);
    }
    snprintf(addRow(v, IFR_EDIT, F_PORT, "Порт TCP").value, 48, "%u", _w.port);

    bool changed = wifiChanged();
    v.button = "Применить";
    v.button_active = changed || !_wifi->settings().enabled;
    char ip[20];
    if (changed) copyStr(v.note, sizeof(v.note), "Есть несохранённые изменения");
    else if (!_wifi->settings().enabled) copyStr(v.note, sizeof(v.note), "Wi-Fi выключен");
    else if (_wifi->isUp()) {
      WifiManager::formatIP(_wifi->ip(), ip, sizeof(ip));
      if (_w.mode == JCWIFI_AP) {
        snprintf(v.note, sizeof(v.note), "%s:%u, устройств %d%s", ip, _w.port, _wifi->stations(),
                 _wifi->clientConnected() ? ", приложение" : "");
      } else {
        snprintf(v.note, sizeof(v.note), "Подключено: %s%s", ip, _wifi->clientConnected() ? ", приложение" : "");
      }
    } else copyStr(v.note, sizeof(v.note), "Подключение к сети...");
  }

  int vis = UIInterfaces::visibleRows(v);
  if (v.scroll > v.row_count - vis) v.scroll = v.row_count - vis > 0 ? v.row_count - vis : 0;

  _dirty = true;
}

void InterfacesController::choose(uint8_t field, int option) {
  NodePrefs* p = the_mesh.getNodePrefs();
  switch (field) {
    case F_PIN_MODE:
      if (option == 0 && !p->ble_pin) {
        // keep the PIN that is shown now, so nothing changes until it is edited
        uint32_t pin = the_mesh.getBLEPin();
        p->ble_pin = (pin >= 100000 && pin <= 999999) ? pin : 123456;
        the_mesh.savePrefs();
      } else if (option == 1 && p->ble_pin) {
        p->ble_pin = 0;
        the_mesh.savePrefs();
      }
      break;
    case F_MODE: _w.mode = option ? JCWIFI_AP : JCWIFI_NETWORK; _view.scroll = 0; break;
    case F_DHCP: _w.dhcp = option == 0; break;
  }
  buildDetail();
}

void InterfacesController::editField(uint8_t field) {
  char val[72] = "";
  _edit_field = field;
  switch (field) {
    case F_PIN:
      snprintf(val, sizeof(val), "%06lu", (unsigned long)the_mesh.getNodePrefs()->ble_pin);
      _input.open(TextInput::DIGITS, "ПОСТОЯННЫЙ PIN, 6 ЦИФР", "Готово", val, 6);
      break;
    case F_SSID:    _input.open(TextInput::SECRET, "ИМЯ СЕТИ (SSID)", "Готово", _w.ssid, 32); break;
    case F_PASS:    _input.open(TextInput::SECRET, "ПАРОЛЬ СЕТИ", "Готово", _w.pass, 63); break;
    case F_AP_SSID: _input.open(TextInput::SECRET, "ИМЯ ТОЧКИ ДОСТУПА", "Готово", _w.ap_ssid, 32); break;
    case F_AP_PASS: _input.open(TextInput::SECRET, "ПАРОЛЬ ТОЧКИ: 8+ ИЛИ ПУСТО", "Готово", _w.ap_pass, 63); break;
    case F_IP:   WifiManager::formatIP(_w.ip, val, sizeof(val));      _input.open(TextInput::IP, "IP-АДРЕС", "Готово", val, 15); break;
    case F_GW:   WifiManager::formatIP(_w.gateway, val, sizeof(val)); _input.open(TextInput::IP, "ШЛЮЗ", "Готово", val, 15); break;
    case F_MASK: WifiManager::formatIP(_w.mask, val, sizeof(val));    _input.open(TextInput::IP, "МАСКА ПОДСЕТИ", "Готово", val, 15); break;
    case F_PORT:
      snprintf(val, sizeof(val), "%u", _w.port);
      _input.open(TextInput::DIGITS, "ПОРТ TCP", "Готово", val, 5);
      break;
    default: return;
  }
  _view.kb = _input.view;
  _view.screen = IF_KEYBOARD;
  _dirty = true;
}

void InterfacesController::fieldEntered() {
  const char* t = _input.view.text;   // untrimmed: spaces may matter in passwords
  uint32_t ip;
  switch (_edit_field) {
    case F_PIN: {
      long pin = atol(t);
      if (strlen(t) != 6 || pin < 100000) { showToast("PIN: 6 цифр, первая не 0"); return; }
      NodePrefs* p = the_mesh.getNodePrefs();
      p->ble_pin = (uint32_t)pin;
      the_mesh.savePrefs();
      break;
    }
    case F_SSID:    copyStr(_w.ssid, sizeof(_w.ssid), t); break;
    case F_PASS:    copyStr(_w.pass, sizeof(_w.pass), t); break;
    case F_AP_SSID:
      if (!t[0]) { showToast("Имя точки не может быть пустым"); return; }
      copyStr(_w.ap_ssid, sizeof(_w.ap_ssid), t);
      break;
    case F_AP_PASS:
      if (t[0] && strlen(t) < 8) { showToast("Пароль: от 8 символов или пусто"); return; }
      copyStr(_w.ap_pass, sizeof(_w.ap_pass), t);
      break;
    case F_IP: case F_GW: case F_MASK:
      if (!WifiManager::parseIP(t, ip)) { showToast("Неверный адрес"); return; }
      if (_edit_field == F_IP) _w.ip = ip; else if (_edit_field == F_GW) _w.gateway = ip; else _w.mask = ip;
      break;
    case F_PORT: {
      long port = atol(t);
      if (port < 1 || port > 65535) { showToast("Порт: от 1 до 65535"); return; }
      _w.port = (uint16_t)port;
      break;
    }
  }
  _view.screen = IF_DETAIL;
  buildDetail();
}

void InterfacesController::button() {
  if (_view.detail == IF_LINK_BLE) {
    _restart = true;
    return;
  }
  if (_view.detail == IF_LINK_WIFI && _wifi) {
    if (_w.mode == JCWIFI_NETWORK && !_w.ssid[0]) { showToast("Укажите сеть (SSID)"); return; }
    _w.enabled = true;
    _wifi->apply(_w);
    _w = _wifi->settings();
    showToast("Wi-Fi перезапущен");
    buildMain();
    buildDetail();
  }
}

// ----------------------------------------------------------------- input ----

void InterfacesController::tap(int x, int y) {
  UIIfHit h = UIInterfaces::hitTest(_view, x, y);
  switch (_view.screen) {
    case IF_MAIN:
      if (h.type == IFH_TOGGLE) toggle(h.index);
      else if (h.type == IFH_OPEN) openDetail(h.index);
      break;
    case IF_DETAIL:
      if (h.type == IFH_BACK) {
        if (_view.detail == IF_LINK_WIFI && wifiChanged()) showToast("Изменения не применены");
        _view.screen = IF_MAIN;
        buildMain();
        _dirty = true;
      } else if (h.type == IFH_CHOICE) {
        choose(_view.rows[h.index].id, h.option);
      } else if (h.type == IFH_EDIT) {
        editField(_view.rows[h.index].id);
      } else if (h.type == IFH_BUTTON) {
        button();
      }
      break;
    case IF_KEYBOARD:
      if (h.type == IFH_BACK) {
        _view.screen = IF_DETAIL;
        buildDetail();
      } else if (h.type == IFH_KEY) {
        if (_input.key(h.key)) fieldEntered();
        else { _view.kb = _input.view; _dirty = true; }
      }
      break;
  }
}

bool InterfacesController::swipe(int dx, int dy) {
  if (abs(dx) > abs(dy)) {
    if (_view.screen == IF_MAIN) return false;
    if (dx > 0) {
      if (_view.screen == IF_KEYBOARD) { _view.screen = IF_DETAIL; buildDetail(); }
      else {
        if (_view.detail == IF_LINK_WIFI && wifiChanged()) showToast("Изменения не применены");
        _view.screen = IF_MAIN; buildMain(); _dirty = true;
      }
    }
    return true;
  }
  if (_view.screen == IF_DETAIL) {
    int vis = UIInterfaces::visibleRows(_view);
    int s = _view.scroll + (dy < 0 ? 2 : -2);
    if (s > _view.row_count - vis) s = _view.row_count - vis;
    if (s < 0) s = 0;
    if (s != _view.scroll) { _view.scroll = s; _dirty = true; }
  }
  return true;
}

const UIIfView* InterfacesController::view() { return &_view; }

void InterfacesController::loop() {
  if (_view.toast[0] && (long)(millis() - _toast_until) >= 0) { _view.toast[0] = 0; _dirty = true; }
  if (!_visible) return;
  // connection states change on their own: refresh the visible screen
  static unsigned long next = 0;
  if ((long)(millis() - next) < 0) return;
  next = millis() + 1000;
  buildMain();
  if (_view.screen == IF_DETAIL && _view.detail == IF_LINK_WIFI) {
    char note[sizeof(_view.note)];
    memcpy(note, _view.note, sizeof(note));
    bool d = _dirty;
    buildDetail();
    _dirty = d || memcmp(note, _view.note, sizeof(note)) != 0;
  }
}
