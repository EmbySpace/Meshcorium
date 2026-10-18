#include "AXS15231BDisplay.h"
#include <Arduino.h>
// U8g2lib.h must be visible before Arduino_GFX: Arduino_GFX only enables its
// U8g2 font renderer (with UTF-8 / Unicode glyph lookup) when it finds U8g2.
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include "UIScreens.h"   // boot screen, drawn with the UI renderer

#if !defined(U8G2_FONT_SUPPORT) || !defined(U8G2_WITH_UNICODE)
  #error "Arduino_GFX U8g2 font support with Unicode is required (add olikraus/U8g2 to lib_deps)"
#endif

#define UI_FONT u8g2_font_10x20_t_cyrillic

// Color scheme (RGB565), dark theme
ColorVal UIColor::window_bkg    = 0x0000;  // black
ColorVal UIColor::title_bkg     = 0x001F;  // blue
ColorVal UIColor::title_txt     = 0xFFFF;  // white
ColorVal UIColor::primary_txt   = 0xFFFF;  // white
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;  // mid-gray
ColorVal UIColor::warning_txt   = 0xFD20;  // orange
ColorVal UIColor::popup_bkg     = 0x07FF;  // cyan
ColorVal UIColor::popup_txt     = 0x0000;  // black
ColorVal UIColor::corp_blue     = 0x001A;

#define PANEL_NATIVE_W  320
#define PANEL_NATIVE_H  480

// Arduino_Canvas allocates its framebuffer with aligned_alloc(). This subclass
// puts the 300 KB framebuffer explicitly into PSRAM so it never competes with
// internal RAM needed by BLE / MeshCore.
class PsramCanvas : public Arduino_Canvas {
public:
  PsramCanvas(int16_t w, int16_t h, Arduino_G* output) : Arduino_Canvas(w, h, output, 0, 0, 0) { }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    if (!_framebuffer) {
      _framebuffer = (uint16_t*) heap_caps_aligned_alloc(16, (size_t)WIDTH * HEIGHT * 2,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!_framebuffer) return false;
    }
    return Arduino_Canvas::begin(speed);
  }
};

bool AXS15231BDisplay::begin() {
  if (_ready) return true;

  if (!psramFound()) {
    Serial.println("AXS15231BDisplay: no PSRAM, cannot allocate framebuffer");
    return false;
  }

  _bus = new Arduino_ESP32QSPI(PIN_TFT_CS, PIN_TFT_SCK, PIN_TFT_D0, PIN_TFT_D1, PIN_TFT_D2, PIN_TFT_D3);
  _panel = new Arduino_AXS15231B(_bus, GFX_NOT_DEFINED /* rst */, 0 /* rotation */, false /* ips */,
                                 PANEL_NATIVE_W, PANEL_NATIVE_H);
  _canvas = new PsramCanvas(PANEL_NATIVE_W, PANEL_NATIVE_H, _panel);

  if (!_canvas->begin()) {
    Serial.println("AXS15231BDisplay: init failed");
    return false;
  }

  _canvas->setRotation(1);   // landscape 480x320
  _canvas->setTextWrap(false);
  _canvas->setFont(UI_FONT);
  _canvas->setUTF8Print(true);
  _canvas->fillScreen(UIColor::window_bkg);
  _canvas->flush();

  _ready = true;
  turnOn();
  return true;
}

// Screen on/off only switches the backlight. The touch controller lives in
// the same AXS15231B chip, and putting the panel to sleep (SLPIN) could stop
// it answering - the screen must be woken by a tap.
void AXS15231BDisplay::turnOn() {
  if (!_ready) return;
  if (!_isOn) {
    analogWrite(PIN_TFT_BL, _brightness);
  }
  _isOn = true;
}

void AXS15231BDisplay::turnOff() {
  if (!_ready) return;
  if (_isOn) {
    analogWrite(PIN_TFT_BL, 0);
  }
  _isOn = false;
}

void AXS15231BDisplay::clear() {
  if (!_ready) return;
  _canvas->fillScreen(UIColor::window_bkg);
  _canvas->flush();
}

void AXS15231BDisplay::startFrame(ColorVal bkg) {
  if (!_ready) return;
  _canvas->fillScreen(bkg);
  setTextSize(1);
  _color = UIColor::primary_txt;
  _canvas->setTextColor(_color);
}

void AXS15231BDisplay::setTextSize(int sz) {
  if (sz < 1) sz = 1;
  _textSize = sz;
  if (!_ready) return;
  _canvas->setTextSize(sz);
}

void AXS15231BDisplay::setColor(ColorVal c) {
  if (!_ready) return;
  _color = c;
  _canvas->setTextColor(_color);
}

void AXS15231BDisplay::setCursor(int x, int y) {
  if (!_ready) return;
  // U8g2 fonts are positioned by their baseline; callers pass the line top
  _canvas->setCursor(x, y + FONT_BASELINE * _textSize);
}

void AXS15231BDisplay::print(const char* str) {
  if (!_ready) return;
  char buf[256];
  sanitizeUTF8(buf, str, sizeof(buf));
  _canvas->print(buf);
}

void AXS15231BDisplay::fillRect(int x, int y, int w, int h) {
  if (!_ready) return;
  _canvas->fillRect(x, y, w, h, _color);
}

void AXS15231BDisplay::drawRect(int x, int y, int w, int h) {
  if (!_ready) return;
  _canvas->drawRect(x, y, w, h, _color);
}

void AXS15231BDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!_ready) return;
  _canvas->drawXBitmap(x, y, bits, w, h, _color);
}

uint16_t AXS15231BDisplay::getTextWidth(const char* str) {
  // monospaced font: width = characters * advance (after the same
  // substitutions print() makes)
  char buf[256];
  sanitizeUTF8(buf, str, sizeof(buf));
  return (uint16_t)(utf8Length(buf) * FONT_ADVANCE * _textSize);
}

// ---- UTF-8 helpers ----

// Decodes one code point; returns number of bytes consumed (>=1), cp = 0xFFFD on error.
static int utf8Decode(const char* s, uint32_t& cp) {
  const uint8_t* u = (const uint8_t*)s;
  if (u[0] < 0x80) { cp = u[0]; return 1; }
  int n; uint32_t c;
  if      ((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; }
  else if ((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; }
  else if ((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; }
  else { cp = 0xFFFD; return 1; }
  for (int i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) { cp = 0xFFFD; return i; }   // truncated sequence
    c = (c << 6) | (u[i] & 0x3F);
  }
  cp = c;
  return n;
}

static bool fontHasGlyph(uint32_t cp) {
  return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0x400 && cp <= 0x523);
}

void AXS15231BDisplay::sanitizeUTF8(char* dest, const char* src, size_t dest_size) {
  if (dest_size == 0) return;
  size_t j = 0;
  while (*src) {
    uint32_t cp;
    int n = utf8Decode(src, cp);
    const char* rep = NULL;     // replacement (ASCII)
    bool keep = false;

    if (fontHasGlyph(cp)) {
      keep = true;
    } else if (cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) {
      rep = " ";
    } else if ((cp >= 0x2010 && cp <= 0x2015) || cp == 0x2212) {
      rep = "-";
    } else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x2032) {
      rep = "'";
    } else if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0xAB || cp == 0xBB) {
      rep = "\"";
    } else if (cp == 0x2026) {
      rep = "...";
    } else if (cp == 0x2116) {
      rep = "No";
    } else if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF)) {
      rep = "";                 // joiners / variation selectors / skin tones: drop
    } else {
      rep = "?";
    }

    if (keep) {
      if (j + n >= dest_size) break;
      memcpy(&dest[j], src, n);
      j += n;
    } else {
      size_t rl = strlen(rep);
      if (j + rl >= dest_size) break;
      memcpy(&dest[j], rep, rl);
      j += rl;
    }
    src += n;
  }
  dest[j] = 0;
}

int AXS15231BDisplay::utf8Length(const char* s) {
  int count = 0;
  while (*s) {
    if ((*s & 0xC0) != 0x80) count++;
    s++;
  }
  return count;
}

void AXS15231BDisplay::endFrame() {
  if (!_ready) return;
  _canvas->flush();
}

// ---- extra primitives ----

void AXS15231BDisplay::drawLine(int x0, int y0, int x1, int y1) {
  if (!_ready) return;
  _canvas->drawLine(x0, y0, x1, y1, _color);
}

void AXS15231BDisplay::drawThickLine(int x0, int y0, int x1, int y1, int thickness) {
  if (!_ready) return;
  if (thickness <= 1) { _canvas->drawLine(x0, y0, x1, y1, _color); return; }
  // round caps: a filled circle at every step along the line
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int steps = dx > dy ? dx : dy;
  int r = thickness / 2;
  for (int i = 0; i <= steps; i++) {
    int x = steps ? x0 + (x1 - x0) * i / steps : x0;
    int y = steps ? y0 + (y1 - y0) * i / steps : y0;
    _canvas->fillCircle(x, y, r, _color);
  }
}

void AXS15231BDisplay::fillCircle(int x, int y, int r) {
  if (!_ready) return;
  _canvas->fillCircle(x, y, r, _color);
}

void AXS15231BDisplay::drawCircle(int x, int y, int r) {
  if (!_ready) return;
  _canvas->drawCircle(x, y, r, _color);
}

void AXS15231BDisplay::fillRoundRect(int x, int y, int w, int h, int r) {
  if (!_ready) return;
  _canvas->fillRoundRect(x, y, w, h, r, _color);
}

void AXS15231BDisplay::drawRoundRect(int x, int y, int w, int h, int r) {
  if (!_ready) return;
  _canvas->drawRoundRect(x, y, w, h, r, _color);
}

void AXS15231BDisplay::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2) {
  if (!_ready) return;
  _canvas->fillTriangle(x0, y0, x1, y1, x2, y2, _color);
}

void AXS15231BDisplay::fillArc(int x, int y, int r_out, int r_in, float start, float end) {
  if (!_ready) return;
  _canvas->fillArc(x, y, r_out, r_in, start, end, _color);
}

// ---- full-frame output from the UI renderer ----

void AXS15231BDisplay::present(const uint16_t* src) {
  if (!_ready) return;
  // the canvas framebuffer is in the panel's native portrait layout (320x480);
  // rotation 1 maps landscape (x, y) to native row x, column 319 - y
  uint16_t* fb = _canvas->getFramebuffer();
  for (int x = 0; x < PANEL_NATIVE_H; x++) {
    uint16_t* dst = fb + x * PANEL_NATIVE_W + (PANEL_NATIVE_W - 1);
    const uint16_t* s = src + x;
    for (int y = 0; y < PANEL_NATIVE_W; y++) {
      *dst-- = *s;
      s += PANEL_NATIVE_H;
    }
  }
  _canvas->flush();
}

void AXS15231BDisplay::showSplash(const char* core_version) {
  if (!_ready) return;
  uint16_t* frame = (uint16_t*)heap_caps_malloc(UI_W * UI_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!frame) return;
  UIGfx g(frame, UI_W, UI_H);
  _core_version = core_version;
  UIScreens::renderSplash(g, core_version);
  present(frame);
  heap_caps_free(frame);
}

void AXS15231BDisplay::showRadioError() {
  if (!_ready) return;
  uint16_t* frame = (uint16_t*)heap_caps_malloc(UI_W * UI_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!frame) return;
  UIGfx g(frame, UI_W, UI_H);
  UIScreens::renderRadioError(g, _core_version);
  turnOn();
  present(frame);
  heap_caps_free(frame);
}

void AXS15231BDisplay::setBrightness(uint8_t percent) {
  if (percent < 5) percent = 5;
  if (percent > 100) percent = 100;
  // perceived brightness is roughly quadratic in the PWM duty
  uint32_t duty = (uint32_t)percent * percent * 255 / 10000;
  if (duty < 2) duty = 2;
  _brightness = (uint8_t)duty;
  if (_ready && _isOn) analogWrite(PIN_TFT_BL, _brightness);
}
