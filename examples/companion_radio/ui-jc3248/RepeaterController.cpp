#include "RepeaterController.h"
#include "../MyMesh.h"
#include <Preferences.h>

#define NVS_NS            "jc3248rep"
#define TOAST_MS          2500
#define FLOOD_MANUAL_GAP  60000UL     // "advert to the whole mesh" button: once a minute

// zero-hop adverts reach only direct neighbours, so they may be frequent;
// flood adverts travel the whole mesh: at least 3 h apart, like MeshCore repeaters
static const uint16_t LOCAL_MIN[] = { 0, 2, 5, 15, 30, 60 };
static const char* const LOCAL_LABELS[] = { "выкл", "2 мин", "5 мин", "15 мин", "30 мин", "1 ч" };
static const uint8_t FLOOD_H[] = { 0, 3, 6, 12, 24 };
static const char* const FLOOD_LABELS[] = { "выкл", "3 ч", "6 ч", "12 ч", "24 ч" };
#define LOCAL_COUNT (sizeof(LOCAL_MIN) / sizeof(LOCAL_MIN[0]))
#define FLOOD_COUNT (sizeof(FLOOD_H) / sizeof(FLOOD_H[0]))

static void copyStr(char* d, size_t n, const char* s) { strncpy(d, s, n - 1); d[n - 1] = 0; }

static void agoText(unsigned long last, char* d, size_t n) {
  if (!last) { copyStr(d, n, "ещё не отправлялся"); return; }
  unsigned long m = (millis() - last) / 60000UL;
  if (m == 0) copyStr(d, n, "последний: только что");
  else if (m < 60) snprintf(d, n, "последний: %lu мин назад", m);
  else snprintf(d, n, "последний: %lu ч назад", m / 60);
}

void RepeaterController::begin() {
  Preferences p;
  p.begin(NVS_NS, true);
  _as_repeater = p.getBool("as_rep", false);
  _local_idx = p.getUChar("local", 0);
  _flood_idx = p.getUChar("flood", 0);
  p.end();
  if (_local_idx >= LOCAL_COUNT) _local_idx = 0;
  if (_flood_idx >= FLOOD_COUNT) _flood_idx = 0;
  the_mesh.setAdvertType(_as_repeater ? ADV_TYPE_REPEATER : ADV_TYPE_CHAT);
  _boot = millis();
  build();
}

void RepeaterController::save() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putBool("as_rep", _as_repeater);
  p.putUChar("local", _local_idx);
  p.putUChar("flood", _flood_idx);
  p.end();
}

void RepeaterController::showToast(const char* t) {
  copyStr(_view.toast, sizeof(_view.toast), t);
  _toast_until = millis() + TOAST_MS;
  _dirty = true;
}

bool RepeaterController::advert(bool flood, bool manual) {
  unsigned long now = millis();
  if (manual && flood && _last_flood && now - _last_flood < FLOOD_MANUAL_GAP) {
    showToast("Анонс по сети - не чаще раза в минуту");
    return false;
  }
  bool ok = the_mesh.uiSendAdvert(flood);
  if (ok) {
    if (flood) _last_flood = now | 1; else _last_local = now | 1;
  }
  if (manual) showToast(!ok ? "Не удалось отправить анонс" : flood ? "Анонс отправлен по сети" : "Анонс отправлен рядом");
  build();
  return ok;
}

void RepeaterController::build() {
  UIRepView v = _view;
  v.repeat_on = the_mesh.isRepeatOn();
  v.as_repeater = _as_repeater;
  v.freq_mhz = the_mesh.getNodePrefs()->freq;
  v.local_label = LOCAL_LABELS[_local_idx];
  v.flood_label = FLOOD_LABELS[_flood_idx];
  v.local_dec = _local_idx > 0;
  v.local_inc = _local_idx + 1 < LOCAL_COUNT;
  v.flood_dec = _flood_idx > 0;
  v.flood_inc = _flood_idx + 1 < FLOOD_COUNT;
  agoText(_last_local, v.local_note, sizeof(v.local_note));
  agoText(_last_flood, v.flood_note, sizeof(v.flood_note));
  if (memcmp(&v, &_view, sizeof(v)) != 0) {
    _view = v;
    _dirty = true;
  }
}

const UIRepView* RepeaterController::view() { return &_view; }

void RepeaterController::tap(int x, int y) {
  switch (UIRepeater::hitTest(_view, x, y)) {
    case REPH_REPEAT: {
      bool on = !the_mesh.isRepeatOn();
      the_mesh.uiSetRepeat(on);
      showToast(on ? "Ретрансляция включена" : "Ретрансляция выключена");
      break;
    }
    case REPH_AS_REPEATER:
      _as_repeater = !_as_repeater;
      the_mesh.setAdvertType(_as_repeater ? ADV_TYPE_REPEATER : ADV_TYPE_CHAT);
      save();
      showToast("Отправьте анонс, чтобы другие увидели");
      break;
    case REPH_LOCAL_DEC: if (_local_idx > 0) { _local_idx--; save(); } break;
    case REPH_LOCAL_INC: if (_local_idx + 1 < LOCAL_COUNT) { _local_idx++; save(); } break;
    case REPH_FLOOD_DEC: if (_flood_idx > 0) { _flood_idx--; save(); } break;
    case REPH_FLOOD_INC: if (_flood_idx + 1 < FLOOD_COUNT) { _flood_idx++; save(); } break;
    case REPH_ADVERT_LOCAL: advert(false, true); return;
    case REPH_ADVERT_FLOOD: advert(true, true); return;
    default: return;
  }
  build();
}

void RepeaterController::loop() {
  unsigned long now = millis();
  if (_view.toast[0] && (long)(now - _toast_until) >= 0) { _view.toast[0] = 0; _dirty = true; }

  // automatic adverts, counted from the last one (or from boot)
  if (LOCAL_MIN[_local_idx]) {
    unsigned long ref = _last_local ? _last_local : _boot;
    if (now - ref >= (unsigned long)LOCAL_MIN[_local_idx] * 60000UL) advert(false, false);
  }
  if (FLOOD_H[_flood_idx]) {
    unsigned long ref = _last_flood ? _last_flood : _boot;
    if (now - ref >= (unsigned long)FLOOD_H[_flood_idx] * 3600000UL) advert(true, false);
  }

  static unsigned long next_build = 0;
  if ((long)(now - next_build) >= 0) {       // "N min ago" texts, repeat flag changed by the app
    next_build = now + 5000;
    build();
  }
}
