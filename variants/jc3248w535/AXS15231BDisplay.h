#pragma once

// DisplayDriver for the JC3248W535 3.5" panel (AXS15231B over QSPI).
//
// The AXS15231B on this board does not handle partial window updates well,
// so everything is drawn into a full-screen RGB565 framebuffer in PSRAM
// (480*320*2 = 300 KB) and sent to the panel as one frame in endFrame().
// The UI only calls endFrame() when something changed, so there is no
// continuous redraw.
//
// Text: U8g2 font "10x20_t_cyrillic" (X11 misc-fixed, public domain), UTF-8.
// It contains U+0020..U+007E and U+0400..U+0523 (Latin + Cyrillic), monospaced
// 10 px advance, 20 px line. Characters outside that set are replaced by
// sanitizeUTF8() (dashes/quotes/ellipsis -> ASCII look-alikes, others -> '?').
// setCursor(x, y) takes the TOP-left corner of the text line, like the
// other MeshCore display drivers.

#include <helpers/ui/DisplayDriver.h>

// Arduino_GFX classes; the library itself is only included in the .cpp
class Arduino_DataBus;
class Arduino_GFX;
class Arduino_Canvas;

class AXS15231BDisplay : public DisplayDriver {
  Arduino_DataBus* _bus;
  Arduino_GFX* _panel;
  Arduino_Canvas* _canvas;
  bool _isOn;
  bool _ready;
  ColorVal _color;
  int _textSize;
  uint8_t _brightness;   // backlight PWM duty 1..255
  const char* _core_version;

public:
  static const int FONT_ADVANCE = 10;   // px per character at text size 1
  static const int FONT_LINE_H  = 20;   // px per line at text size 1
  static const int FONT_BASELINE = 16;  // baseline offset from line top at text size 1

  // Logical size is landscape 480x320 (panel is natively 320x480 portrait).
  AXS15231BDisplay() : DisplayDriver(480, 320), _bus(nullptr), _panel(nullptr), _canvas(nullptr),
                       _isOn(false), _ready(false), _color(0xFFFF), _textSize(1), _brightness(255), _core_version(NULL) { }

  bool begin();

  // Copies src to dest (UTF-8), replacing characters the font does not have.
  static void sanitizeUTF8(char* dest, const char* src, size_t dest_size);
  // Number of characters (code points) in a UTF-8 string.
  static int utf8Length(const char* s);

  int lineHeight() const { return FONT_LINE_H * _textSize; }

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    sanitizeUTF8(dest, src, dest_size);
  }
  void endFrame() override;

  // extra primitives (not in DisplayDriver), used by the JC3248 UI to draw
  // vector icons straight into the framebuffer - no bitmaps in flash / RAM
  void drawLine(int x0, int y0, int x1, int y1);
  void drawThickLine(int x0, int y0, int x1, int y1, int thickness);
  void fillCircle(int x, int y, int r);
  void drawCircle(int x, int y, int r);
  void fillRoundRect(int x, int y, int w, int h, int r);
  void drawRoundRect(int x, int y, int w, int h, int r);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2);
  // ring segment between radii r_in..r_out; angles in degrees, 0 = right, clockwise (270 = up)
  void fillArc(int x, int y, int r_out, int r_in, float start, float end);

  // Sends a whole landscape (480x320, row-major RGB565) frame rendered
  // elsewhere (the JC3248 UI renders with its own anti-aliased UIGfx).
  void present(const uint16_t* landscape);

  // boot screen ("Meshcorium | V1"), core_version e.g. FIRMWARE_VERSION
  void showSplash(const char* core_version);
  // "LoRa module error!" screen (uses the version given to showSplash)
  void showRadioError();

  // backlight level in percent (PWM on PIN_TFT_BL); applied while the screen is on
  void setBrightness(uint8_t percent);

  static ColorVal rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }
};
