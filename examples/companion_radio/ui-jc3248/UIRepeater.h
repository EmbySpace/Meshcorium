#pragma once

// Repeater page (page 7) of the JC3248W535 UI: packet forwarding on the
// current frequency, advertising as a repeater, automatic adverts and
// "advert now" buttons. Pure drawing + hit-testing; logic in RepeaterController.

#include "UIGfx.h"

struct UIRepView {
  bool repeat_on;
  bool as_repeater;
  float freq_mhz;
  const char* local_label;      // auto advert nearby (zero hop) interval
  const char* flood_label;      // auto advert to the whole mesh interval
  bool local_dec, local_inc, flood_dec, flood_inc;
  char local_note[48];          // "последний: 5 мин назад"
  char flood_note[48];
  char toast[48];
};

enum UIRepHit : uint8_t {
  REPH_NONE, REPH_REPEAT, REPH_AS_REPEATER, REPH_LOCAL_DEC, REPH_LOCAL_INC,
  REPH_FLOOD_DEC, REPH_FLOOD_INC, REPH_ADVERT_LOCAL, REPH_ADVERT_FLOOD
};

namespace UIRepeater {
  void render(UIGfx& g, const UIRepView& v);
  UIRepHit hitTest(const UIRepView& v, int x, int y);
}
