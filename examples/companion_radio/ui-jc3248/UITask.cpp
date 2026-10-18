#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include "AXS15231BTouch.h"
#include <helpers/AdvertTimeSync.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <time.h>

// TF card slot of the JC3248W535, wired for SPI: CS=IO10, MOSI=IO11, SCK=IO12,
// MISO=IO13. Used here through the SDMMC controller in 1-bit mode
// (CLK=SCK, CMD=MOSI, D0=MISO, DAT3=CS held high), so it needs neither of the
// SPI buses (radio and display have them).
#ifndef PIN_SD_CS
  #define PIN_SD_CS    10
  #define PIN_SD_CMD   11
  #define PIN_SD_CLK   12
  #define PIN_SD_D0    13
#endif

#define STATUS_MS       500UL       // model refresh
#define BATT_MS         30000UL     // battery sample interval (trend over UI_BATT_SAMPLES of them)
#define TREND_MV        15          // voltage change over the history that counts as (dis)charging
#define FULL_MV         4150        // flat at or above this = charged
#define HEADER_TAP_H    30          // taps on the status bar switch pages
#define USB_APP_ACTIVE_MS 120000UL  // USB counts as "app connected" this long after its last command

static AXS15231BTouch touch;

// screen timeout choices; 0 = never switch off
static const uint16_t TIMEOUT_SECS[] = { 15, 30, 60, 120, 300, 600, 0 };
static const char* const TIMEOUT_LABELS[] = { "15 с", "30 с", "1 мин", "2 мин", "5 мин", "10 мин", "всегда" };
#define TIMEOUT_COUNT    (sizeof(TIMEOUT_SECS) / sizeof(TIMEOUT_SECS[0]))
#define TIMEOUT_DEFAULT  2          // 1 min
#define BRIGHT_STEP      10
#define BRIGHT_MIN       10

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = (AXS15231BDisplay*)display;   // this UI is only built for the JC3248W535
  _sensors = sensors;
  _node_prefs = node_prefs;

  if (_display) {
    _frame = (uint16_t*)heap_caps_malloc(UI_W * UI_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_frame) _gfx = new UIGfx(_frame, UI_W, UI_H);
  }

  sampleBattery();
  _next_batt = millis() + BATT_MS;
  loadSettings();
  if (_display) _display->setBrightness(_brightness);
  _chats.begin(&SPIFFS);      // SPIFFS is mounted by main.cpp before the UI starts
  // Wi-Fi is configured on the device (page 3), the interfaces are already
  // registered and enabled by main.cpp at this point
  _wifi.begin(_interfaceManager, _node_prefs ? _node_prefs->node_name : "");
  _links.begin(_interfaceManager, &_wifi);
  _rep.begin();
  checkSdCard();
  touch.begin();
  _last_activity = millis();
  _dirty = true;
}

// ------------------------------------------------------------ settings ----

void UITask::loadSettings() {
  Preferences p;
  p.begin("jc3248ui", true);
  _timeout_idx = p.getUChar("scr_to", TIMEOUT_DEFAULT);
  _brightness = p.getUChar("bright", 80);
  p.end();
  if (_timeout_idx >= TIMEOUT_COUNT) _timeout_idx = TIMEOUT_DEFAULT;
  if (_brightness < BRIGHT_MIN || _brightness > 100) _brightness = 80;
}

void UITask::saveSettings() {
  Preferences p;
  p.begin("jc3248ui", false);
  p.putUChar("scr_to", _timeout_idx);
  p.putUChar("bright", _brightness);
  p.end();
}

// (Re)mounts the card and caches its size: computing the free space walks the
// FAT, which can take a moment on big cards, so it is not done on every redraw.
void UITask::checkSdCard() {
  SD_MMC.end();
  _sd = UI_SD_NONE;
  _sd_total_mb = _sd_used_mb = 0;
  _sd_type = NULL;

  pinMode(PIN_SD_CS, OUTPUT);       // DAT3 high = SD mode (not SPI)
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_SD_CMD, INPUT_PULLUP);
  pinMode(PIN_SD_D0, INPUT_PULLUP);
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
  if (!SD_MMC.begin("/sdcard", true /* 1-bit */, false /* never format */)) {
    _chats.attachSd(NULL);
    return;
  }

  switch (SD_MMC.cardType()) {
    case CARD_MMC:  _sd_type = "MMC"; break;
    case CARD_SD:   _sd_type = "SDSC"; break;
    case CARD_SDHC: _sd_type = "SDHC"; break;
    case CARD_NONE: SD_MMC.end(); _chats.attachSd(NULL); return;
    default:        _sd_type = "SD"; break;
  }
  _sd = UI_SD_OK;
  _sd_total_mb = (uint32_t)(SD_MMC.totalBytes() / (1024ULL * 1024ULL));
  _sd_used_mb = (uint32_t)(SD_MMC.usedBytes() / (1024ULL * 1024ULL));
  _chats.attachSd(&SD_MMC);
}

void UITask::applyAction(UIAction a) {
  if (a == UI_ACT_SD_REFRESH) {
    checkSdCard();
    _next_status = 0;
    return;
  }
  switch (a) {
    case UI_ACT_BRIGHT_DEC:  if (_brightness > BRIGHT_MIN) _brightness -= BRIGHT_STEP; break;
    case UI_ACT_BRIGHT_INC:  if (_brightness < 100) _brightness += BRIGHT_STEP; break;
    case UI_ACT_TIMEOUT_DEC: if (_timeout_idx > 0) _timeout_idx--; break;
    case UI_ACT_TIMEOUT_INC: if (_timeout_idx + 1 < TIMEOUT_COUNT) _timeout_idx++; break;
    default: return;
  }
  if (a == UI_ACT_BRIGHT_DEC || a == UI_ACT_BRIGHT_INC) _display->setBrightness(_brightness);
  saveSettings();
  _next_status = 0;       // refresh the model (and the screen) right away
}

// -------------------------------------------------------------- screen ----

void UITask::wake() {
  _last_activity = millis();
  if (!_display->isOn()) {
    _display->turnOn();
    _dirty = true;          // the frame was not redrawn while the screen was dark
  }
}

void UITask::checkScreenTimeout() {
  uint16_t secs = TIMEOUT_SECS[_timeout_idx];
  if (secs == 0 || !_display->isOn()) return;
  if (millis() - _last_activity >= (unsigned long)secs * 1000UL) {
    _display->turnOff();
  }
}

// -------------------------------------------------------------- battery ----

void UITask::sampleBattery() {
  _batt_mv = getBattMilliVolts();
  if (_batt_hist_len == UI_BATT_SAMPLES) {
    memmove(&_batt_hist[0], &_batt_hist[1], (UI_BATT_SAMPLES - 1) * sizeof(_batt_hist[0]));
    _batt_hist_len--;
  }
  _batt_hist[_batt_hist_len++] = _batt_mv;
}

// The board has no charger status pin, so charging is inferred from the
// voltage trend over the last (UI_BATT_SAMPLES - 1) * BATT_MS = 2 minutes.
UIBattTrend UITask::battTrend() const {
  if (_batt_mv < 1000) return UI_BATT_NONE;
  if (_batt_hist_len < UI_BATT_SAMPLES) return UI_BATT_UNKNOWN;
  int delta = (int)_batt_hist[_batt_hist_len - 1] - (int)_batt_hist[0];
  if (delta >= TREND_MV) return UI_BATT_CHARGING;
  if (delta <= -TREND_MV) return UI_BATT_DISCHARGING;
  return _batt_mv >= FULL_MV ? UI_BATT_FULL : UI_BATT_DISCHARGING;
}

// ---------------------------------------------------------------- model ----

void UITask::fillModel(UIModel& m) {
  memset(&m, 0, sizeof(m));   // also clears padding, the model is compared with memcmp
  MultiSerialInterface* ifm = _interfaceManager;
  m.page = _page;

  if (_node_prefs) {
    strncpy(m.node_name, _node_prefs->node_name, sizeof(m.node_name) - 1);
    m.freq_mhz = _node_prefs->freq;
    m.bw_khz = _node_prefs->bw;
    m.sf = _node_prefs->sf;
    m.cr = _node_prefs->cr;
    m.tx_power = _node_prefs->tx_power_dbm;
    strncpy(m.region, _node_prefs->default_scope_name, sizeof(m.region) - 1);
  }
  m.contacts = the_mesh.getNumContacts();
  m.max_contacts = MAX_CONTACTS;

  uint32_t t = rtc_clock.getCurrentTime();
  if (AdvertTimeSync::isTimeValid(t)) {       // set by the app, an RTC chip or from adverts
    time_t lt = (time_t)t + UI_TZ_OFFSET_MIN * 60;
    struct tm tmv;
    gmtime_r(&lt, &tmv);
    m.time_valid = true;
    m.hour = tmv.tm_hour; m.minute = tmv.tm_min;
    m.day = tmv.tm_mday; m.month = tmv.tm_mon + 1; m.year = tmv.tm_year + 1900;
    m.weekday = tmv.tm_wday;
  }

  // interfaces
  if (!ifm->hasInterface(InterfaceType::Bluetooth))             m.ble = UI_LINK_ABSENT;
  else if (!ifm->isInterfaceEnabled(InterfaceType::Bluetooth))  m.ble = UI_LINK_OFF;
  else if (ifm->isInterfaceConnected(InterfaceType::Bluetooth)) m.ble = UI_LINK_CLIENT;
  else                                                          m.ble = UI_LINK_READY;
  m.ble_pin = the_mesh.getBLEPin();

  // Wi-Fi (WifiManager, configured on page 3)
  const WifiSettings& ws = _wifi.settings();
  if (ws.enabled && (ws.mode == JCWIFI_AP || ws.ssid[0])) {
    strncpy(m.wifi_ssid, _wifi.currentSsid(), sizeof(m.wifi_ssid) - 1);
  }
  if (!ws.enabled)                 m.wifi = UI_LINK_OFF;
  else if (!_wifi.isUp())          m.wifi = UI_LINK_OFF;
  else if (_wifi.clientConnected()) m.wifi = UI_LINK_CLIENT;
  else                             m.wifi = UI_LINK_READY;
  m.wifi_ip = _wifi.ip();

  // USB: cable to a PC = the USB-CDC sees the host's SOF frames (isPlugged);
  // an app on the port can only be recognised by the commands it sends
  if (!ifm->hasInterface(InterfaceType::USB)) {
    m.usb = UI_LINK_ABSENT;
  } else if (!Serial.isPlugged()) {
    m.usb = UI_LINK_OFF;
  } else if (!ifm->isInterfaceEnabled(InterfaceType::USB)) {
    // switched off on page 3 but a cable is in: blink grey / white every 0.5 s
    // (the model is rebuilt every STATUS_MS = 0.5 s, so this alternates)
    m.usb = (millis() / 500) % 2 ? UI_LINK_READY : UI_LINK_OFF;
  } else {
    unsigned long rx = ifm->lastFrameMillis(InterfaceType::USB);
    m.usb = (rx != 0 && millis() - rx < USB_APP_ACTIVE_MS) ? UI_LINK_CLIENT : UI_LINK_READY;
  }

  // power
  m.batt_mv = _batt_mv;
  m.trend = battTrend();
  uint32_t up = millis() / 1000;
  // seconds only matter on the power page during the first hour; elsewhere
  // a coarser value avoids redrawing every second
  if (_page == UI_PAGE_POWER) m.uptime_s = up < 3600 ? up : up / 60 * 60;

  // radio: the noise floor changes all the time, only track it while visible
  if (_page == UI_PAGE_RADIO) m.noise_dbm = radio_driver.getNoiseFloor();

  // settings
  m.brightness_pct = _brightness;
  m.timeout_label = TIMEOUT_LABELS[_timeout_idx];
  m.bright_can_dec = _brightness > BRIGHT_MIN;
  m.bright_can_inc = _brightness < 100;
  m.timeout_can_dec = _timeout_idx > 0;
  m.timeout_can_inc = _timeout_idx + 1 < TIMEOUT_COUNT;
  m.sd = _sd;
  m.sd_total_mb = _sd_total_mb;
  m.sd_used_mb = _sd_used_mb;
  m.sd_type = _sd_type;
  m.fw_version = FIRMWARE_VERSION;

  m.chats = _page == UI_PAGE_CHATS ? _chats.view() : NULL;
  m.links = _page == UI_PAGE_LINKS ? _links.view() : NULL;
  m.rep = _page == UI_PAGE_REPEATER ? _rep.view() : NULL;
  m.chats_unread = _chats.unreadTotal();
}

void UITask::render() {
  if (!_gfx) return;
  UIScreens::render(*_gfx, _shown);
  _display->present(_frame);
}

// ------------------------------------------------------------------ loop ----

void UITask::handleTouch() {
  int x = 0, y = 0;
  AXS15231BTouch::Gesture g = touch.gesture(x, y);
  if (g == AXS15231BTouch::NONE) return;

  // the touch that wakes a dark screen only wakes it
  bool was_off = !_display->isOn();
  wake();
  if (was_off) return;

  bool chats = _page == UI_PAGE_CHATS, links = _page == UI_PAGE_LINKS;
  // pages with their own sub screens get swipes first (back / scrolling)
  auto subSwipe = [&](int dx, int dy) -> bool {
    if (chats) return _chats.swipe(dx, dy);
    if (links) return _links.swipe(dx, dy);
    return false;
  };
  int step = 0;
  switch (g) {
    case AXS15231BTouch::SWIPE_LEFT:
      if (!subSwipe(-1, 0)) step = 1;
      break;
    case AXS15231BTouch::SWIPE_RIGHT:
      if (!subSwipe(1, 0)) step = -1;
      break;
    case AXS15231BTouch::SWIPE_UP:
    case AXS15231BTouch::SWIPE_DOWN:
      subSwipe(0, g == AXS15231BTouch::SWIPE_UP ? -1 : 1);
      break;
    case AXS15231BTouch::TAP:
      if (y < HEADER_TAP_H) step = x < UI_W / 2 ? -1 : 1;
      else if (chats) _chats.tap(x, y);
      else if (links) _links.tap(x, y);
      else if (_page == UI_PAGE_REPEATER) _rep.tap(x, y);
      else applyAction(UIScreens::hitTest(_shown, x, y));
      break;
    default: break;
  }

  if (step != 0) {
    _page = (_page + UI_PAGE_COUNT + step) % UI_PAGE_COUNT;
    _chats.setVisible(_page == UI_PAGE_CHATS);
    _links.setVisible(_page == UI_PAGE_LINKS);
    _next_status = 0;      // build the model of the new page right away
  }
  if (_page == UI_PAGE_CHATS || _page == UI_PAGE_LINKS || _page == UI_PAGE_REPEATER) _next_status = 0;   // sub screens change on every tap
}

void UITask::loop() {
  if (_display == NULL) return;

  handleTouch();
  if (_wake_request) {
    _wake_request = false;
    wake();
  }
  checkScreenTimeout();

  unsigned long now = millis();
  if ((long)(now - _next_batt) >= 0) {
    _next_batt = now + BATT_MS;
    sampleBattery();
  }
  if ((long)(now - _next_status) >= 0) {
    _next_status = now + STATUS_MS;
    UIModel m;
    fillModel(m);
    if (memcmp(&m, &_shown, sizeof(m)) != 0) {
      _shown = m;
      _dirty = true;
    }
  }

  _chats.loop();
  if (_chats.dirty()) {
    _chats.clearDirty();
    if (_page == UI_PAGE_CHATS) _dirty = true;
    _next_status = 0;
  }
  _wifi.loop();
  _links.loop();
  if (_links.dirty()) {
    _links.clearDirty();
    if (_page == UI_PAGE_LINKS) _dirty = true;
  }
  _rep.loop();
  if (_rep.dirty()) {
    _rep.clearDirty();
    if (_page == UI_PAGE_REPEATER) _dirty = true;
  }
  if (_links.restartRequested()) {
    _chats.flush();          // don't lose unsaved chat history
    delay(200);
    ESP.restart();
  }

  if (_dirty && _display->isOn()) {    // no redraws while the screen is dark
    _dirty = false;
    render();
  }
}
