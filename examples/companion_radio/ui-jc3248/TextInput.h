#pragma once

// Text entry with the on-screen keyboard (UIChats::renderKeyboard):
// key handling, shift / layouts and per-field rules. Used by the chats page
// (messages, channel names) and the interfaces page (SSID, password, IP...).

#include "UIChats.h"
#include <string.h>

class TextInput {
public:
  enum Mode : uint8_t {
    TEXT,       // sentences: capital first letter, spaces
    HASHTAG,    // "#name": lower case, no spaces, English layout
    SECRET,     // passwords / SSIDs: as typed, no auto capitals
    DIGITS,     // PIN, port
    IP,         // digits and dots
  };

  UIKeyboardView view;

  TextInput() : _mode(TEXT), _letters(KB_RU) { _text[0] = 0; memset(&view, 0, sizeof(view)); view.text = _text; }

  void open(Mode mode, const char* title, const char* action, const char* initial, int max_bytes,
            const char* prefix = NULL);
  // returns true when the confirm key was pressed
  bool key(const UIKey& k);
  // text with spaces trimmed at both ends
  const char* text();
  void clear() { _text[0] = 0; view.bytes = 0; }
  bool empty() const { return _text[0] == 0; }

private:
  Mode _mode;
  uint8_t _letters;          // RU / EN to return to from symbols
  char _text[212];
  char _out[212];
};
