#pragma once

// Logic of the repeater page (page 7):
//  - packet forwarding on the current frequency (node prefs "repeat", the
//    phone app only allows it on its fixed repeat frequencies)
//  - advert type: companion or repeater (NVS "jc3248rep")
//  - automatic adverts nearby (zero hop) and to the whole mesh (flood)
//  - "advert now" buttons, the flood one limited to once a minute

#include <Arduino.h>
#include "UIRepeater.h"

class RepeaterController {
public:
  RepeaterController() : _as_repeater(false), _local_idx(0), _flood_idx(0), _last_local(0), _last_flood(0),
                         _boot(0), _dirty(true), _toast_until(0) { memset(&_view, 0, sizeof(_view)); }

  void begin();
  void loop();
  void tap(int x, int y);

  const UIRepView* view();
  bool dirty() const { return _dirty; }
  void clearDirty() { _dirty = false; }

private:
  UIRepView _view;
  bool _as_repeater;
  uint8_t _local_idx, _flood_idx;
  unsigned long _last_local, _last_flood;   // millis() of the last advert, 0 = none yet
  unsigned long _boot;
  bool _dirty;
  unsigned long _toast_until;

  void save();
  void build();
  bool advert(bool flood, bool manual);
  void showToast(const char* t);
};
