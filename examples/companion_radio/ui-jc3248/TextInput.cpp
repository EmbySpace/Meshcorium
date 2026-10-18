#include "TextInput.h"
#include <string.h>

// removes the last UTF-8 character
static void utf8Pop(char* s) {
  size_t n = strlen(s);
  if (!n) return;
  n--;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  s[n] = 0;
}

void TextInput::open(Mode mode, const char* title, const char* action, const char* initial, int max_bytes,
                     const char* prefix) {
  _mode = mode;
  strncpy(view.title, title, sizeof(view.title) - 1);
  view.title[sizeof(view.title) - 1] = 0;
  view.action = action;
  view.prefix = prefix;
  if (max_bytes > (int)sizeof(_text) - 1) max_bytes = sizeof(_text) - 1;
  view.max_bytes = max_bytes;
  strncpy(_text, initial ? initial : "", max_bytes);
  _text[max_bytes] = 0;
  view.text = _text;
  view.bytes = strlen(_text);

  switch (mode) {
    case TEXT:    _letters = KB_RU; view.layout = KB_RU;  view.shift = _text[0] == 0; break;
    case HASHTAG: _letters = KB_EN; view.layout = KB_EN;  view.shift = false; break;
    case SECRET:  _letters = KB_EN; view.layout = KB_EN;  view.shift = false; break;
    case DIGITS:
    case IP:      _letters = KB_EN; view.layout = KB_SYM; view.shift = false; break;
  }
}

bool TextInput::key(const UIKey& k) {
  size_t len = strlen(_text);
  switch (k.type) {
    case KEY_CHAR: {
      char ch[8];
      if (view.shift) UIChats::upper(k.text, ch, sizeof(ch));
      else { strncpy(ch, k.text, sizeof(ch)); ch[7] = 0; }
      bool ok = true;
      if (_mode == HASHTAG && ch[0] >= 'A' && ch[0] <= 'Z') ch[0] += 32;     // hashtags are lower case
      if (_mode == DIGITS) ok = ch[0] >= '0' && ch[0] <= '9' && !ch[1];
      if (_mode == IP) ok = ((ch[0] >= '0' && ch[0] <= '9') || ch[0] == '.') && !ch[1];
      size_t n = strlen(ch);
      if (ok && (int)(len + n) <= view.max_bytes) memcpy(_text + len, ch, n + 1);
      view.shift = false;
      break;
    }
    case KEY_SPACE:
      if (_mode == HASHTAG || _mode == DIGITS || _mode == IP) break;
      if ((int)len + 1 <= view.max_bytes) { _text[len] = ' '; _text[len + 1] = 0; }
      // capital letter after the end of a sentence
      if (_mode == TEXT && len > 0 && strchr(".!?", _text[len - 1])) view.shift = true;
      break;
    case KEY_BACKSPACE:
      utf8Pop(_text);
      if (_text[0] == 0 && _mode == TEXT) view.shift = true;
      break;
    case KEY_SHIFT:
      view.shift = !view.shift;
      break;
    case KEY_LANG:
      _letters = _letters == KB_RU ? KB_EN : KB_RU;
      view.layout = _letters;
      break;
    case KEY_SYM:
      view.layout = view.layout == KB_SYM ? _letters : KB_SYM;
      break;
    case KEY_ACTION:
      return true;
  }
  view.bytes = strlen(_text);
  return false;
}

const char* TextInput::text() {
  const char* s = _text;
  while (*s == ' ') s++;
  strncpy(_out, s, sizeof(_out) - 1);
  _out[sizeof(_out) - 1] = 0;
  size_t n = strlen(_out);
  while (n > 0 && _out[n - 1] == ' ') _out[--n] = 0;
  return _out;
}
