#include "UIGfx.h"
#include <math.h>
#include <string.h>

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline uint8_t toAlpha(float cov) { return (uint8_t)(clamp01(cov) * 255.0f + 0.5f); }

void UIGfx::setClip(int x, int y, int w, int h) {
  _cx0 = x < 0 ? 0 : x;
  _cy0 = y < 0 ? 0 : y;
  _cx1 = x + w > _w ? _w : x + w;
  _cy1 = y + h > _h ? _h : y + h;
}

Color UIGfx::blend(Color dst, Color src, uint8_t a) {
  if (a == 0) return dst;
  if (a == 255) return src;
  uint32_t ia = 255 - a;
  uint32_t r = (((src >> 11) & 31) * a + ((dst >> 11) & 31) * ia + 127) / 255;
  uint32_t g = (((src >> 5) & 63) * a + ((dst >> 5) & 63) * ia + 127) / 255;
  uint32_t b = ((src & 31) * a + (dst & 31) * ia + 127) / 255;
  return (Color)((r << 11) | (g << 5) | b);
}

void UIGfx::clear(Color c) {
  for (int i = 0, n = _w * _h; i < n; i++) _buf[i] = c;
}

void UIGfx::fillRect(int x, int y, int w, int h, Color c) {
  int x0 = x < _cx0 ? _cx0 : x, y0 = y < _cy0 ? _cy0 : y;
  int x1 = x + w > _cx1 ? _cx1 : x + w, y1 = y + h > _cy1 ? _cy1 : y + h;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t* p = &_buf[yy * _w + x0];
    for (int xx = x0; xx < x1; xx++) *p++ = c;
  }
}

void UIGfx::blendRect(int x, int y, int w, int h, Color c, uint8_t alpha) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) plot(xx, yy, c, alpha);
}

void UIGfx::vGradient(int x, int y, int w, int h, Color top, Color bottom) {
  for (int j = 0; j < h; j++) {
    Color c = blend(top, bottom, (uint8_t)(h > 1 ? j * 255 / (h - 1) : 0));
    fillRect(x, y + j, w, 1, c);
  }
}

void UIGfx::fillRoundRect(int x, int y, int w, int h, int r, Color c) {
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  if (r <= 0) { fillRect(x, y, w, h, c); return; }

  for (int py = y; py < y + h; py++) {
    float fy = py + 0.5f;
    float cy;
    if (fy < y + r) cy = (float)(y + r);
    else if (fy > y + h - r) cy = (float)(y + h - r);
    else { fillRect(x, py, w, 1, c); continue; }

    float dy = fy - cy;
    fillRect(x + r, py, w - 2 * r, 1, c);
    for (int i = 0; i < r; i++) {
      float dx = (x + r) - (x + i + 0.5f);
      uint8_t a = toAlpha(r - sqrtf(dx * dx + dy * dy) + 0.5f);
      plot(x + i, py, c, a);
      plot(x + w - 1 - i, py, c, a);
    }
  }
}

void UIGfx::card(int x, int y, int w, int h, int r, Color fill, Color border) {
  fillRoundRect(x, y, w, h, r, border);
  fillRoundRect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, fill);
}

void UIGfx::fillCircle(float cx, float cy, float r, Color c) {
  int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - cy;
    for (int px = x0; px <= x1; px++) {
      float dx = px + 0.5f - cx;
      float cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
      if (cov > 0) plot(px, py, c, toAlpha(cov));
    }
  }
}

void UIGfx::line(float x0, float y0, float x1, float y1, float width, Color c) {
  float r = width * 0.5f;
  int bx0 = (int)floorf(fminf(x0, x1) - r - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + r + 1);
  int by0 = (int)floorf(fminf(y0, y1) - r - 1), by1 = (int)ceilf(fmaxf(y0, y1) + r + 1);
  float dx = x1 - x0, dy = y1 - y0;
  float len2 = dx * dx + dy * dy;
  for (int py = by0; py <= by1; py++) {
    for (int px = bx0; px <= bx1; px++) {
      float qx = px + 0.5f - x0, qy = py + 0.5f - y0;
      float t = len2 > 0 ? (qx * dx + qy * dy) / len2 : 0;
      t = t < 0 ? 0 : (t > 1 ? 1 : t);
      float ex = qx - t * dx, ey = qy - t * dy;
      float cov = r - sqrtf(ex * ex + ey * ey) + 0.5f;
      if (cov > 0) plot(px, py, c, toAlpha(cov));
    }
  }
}

void UIGfx::arc(float cx, float cy, float r_out, float r_in, float start, float end, Color c, bool round_caps) {
  float sweep = end - start;
  if (sweep <= 0) return;
  bool full = sweep >= 360.0f;
  const float DEG = 180.0f / 3.14159265f;

  int x0 = (int)floorf(cx - r_out - 1), x1 = (int)ceilf(cx + r_out + 1);
  int y0 = (int)floorf(cy - r_out - 1), y1 = (int)ceilf(cy + r_out + 1);
  for (int py = y0; py <= y1; py++) {
    float dy = py + 0.5f - cy;
    for (int px = x0; px <= x1; px++) {
      float dx = px + 0.5f - cx;
      float d = sqrtf(dx * dx + dy * dy);
      float cov = clamp01(r_out - d + 0.5f);
      float ci = clamp01(d - r_in + 0.5f);
      if (ci < cov) cov = ci;
      if (cov <= 0) continue;

      if (!full) {
        float ang = atan2f(dy, dx) * DEG;          // -180..180, clockwise on screen
        float rel = fmodf(ang - start + 720.0f, 360.0f);
        float px_per_deg = d / DEG;
        float ca;
        if (rel <= sweep) {
          float inside = (rel < sweep - rel ? rel : sweep - rel) * px_per_deg;
          ca = clamp01(inside + 0.5f);
        } else {
          float a1 = rel - sweep, a2 = 360.0f - rel;
          float outside = (a1 < a2 ? a1 : a2) * px_per_deg;
          ca = clamp01(0.5f - outside);
        }
        cov *= ca;
        if (cov <= 0) continue;
      }
      plot(px, py, c, toAlpha(cov));
    }
  }

  if (round_caps && !full) {
    float rm = (r_out + r_in) * 0.5f, rc = (r_out - r_in) * 0.5f;
    float a0 = start / DEG, a1 = end / DEG;
    fillCircle(cx + rm * cosf(a0), cy + rm * sinf(a0), rc, c);
    fillCircle(cx + rm * cosf(a1), cy + rm * sinf(a1), rc, c);
  }
}

// ---------------------------------------------------------------- text ----

int UIGfx::decodeUTF8(const char* s, uint32_t& cp) {
  const uint8_t* u = (const uint8_t*)s;
  if (u[0] < 0x80) { cp = u[0]; return 1; }
  int n; uint32_t c;
  if ((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; }
  else if ((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; }
  else if ((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; }
  else { cp = '?'; return 1; }
  for (int i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) { cp = '?'; return i; }
    c = (c << 6) | (u[i] & 0x3F);
  }
  cp = c;
  return n;
}

const UIGlyph* UIGfx::findGlyph(const UIFont& f, uint32_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t m = f.glyphs[mid].cp;
    if (m == cp) return &f.glyphs[mid];
    if (m < cp) lo = mid + 1; else hi = mid - 1;
  }
  return NULL;
}

static const UIGlyph* glyphOrFallback(const UIFont& f, uint32_t cp) {
  if (cp == '\n' || cp == '\t') cp = ' ';
  if (cp == 0x2212) cp = '-';
  const UIGlyph* g = UIGfx::findGlyph(f, cp);
  if (!g) g = UIGfx::findGlyph(f, '?');
  return g;
}

void UIGfx::drawGlyph(const UIFont& f, const UIGlyph& g, int x, int y, Color c) {
  const uint8_t* p = f.bits + g.off;
  int stride = (g.w + 1) / 2;
  int gx = x + g.x, gy = y + g.y;
  for (int j = 0; j < g.h; j++) {
    int py = gy + j;
    if (py < _cy0 || py >= _cy1) continue;
    const uint8_t* row = p + j * stride;
    for (int i = 0; i < g.w; i++) {
      uint8_t v = (i & 1) ? (row[i >> 1] & 0x0F) : (row[i >> 1] >> 4);
      if (v) plot(gx + i, py, c, (uint8_t)(v * 17));
    }
  }
}

int UIGfx::textWidth(const UIFont& f, const char* s, int tracking) {
  int w = 0, n = 0;
  while (s && *s) {
    uint32_t cp;
    s += decodeUTF8(s, cp);
    const UIGlyph* g = glyphOrFallback(f, cp);
    if (g) { w += g->adv; n++; }
  }
  return n > 1 ? w + (n - 1) * tracking : w;
}

int UIGfx::text(const UIFont& f, int x, int y, const char* s, Color c, UIAlign align, int tracking) {
  int w = textWidth(f, s, tracking);
  if (align == ALIGN_CENTER) x -= w / 2;
  else if (align == ALIGN_RIGHT) x -= w;
  while (s && *s) {
    uint32_t cp;
    s += decodeUTF8(s, cp);
    const UIGlyph* g = glyphOrFallback(f, cp);
    if (!g) continue;
    if (g->w) drawGlyph(f, *g, x, y, c);
    x += g->adv + tracking;
  }
  return w;
}

void UIGfx::ellipsize(const UIFont& f, const char* s, int max_w, char* dest, size_t size) {
  if (size == 0) return;
  if (textWidth(f, s) <= max_w) {
    strncpy(dest, s, size - 1);
    dest[size - 1] = 0;
    return;
  }
  int dots = textWidth(f, "...");
  int w = 0;
  size_t n = 0;
  const char* p = s;
  while (*p) {
    uint32_t cp;
    int len = decodeUTF8(p, cp);
    const UIGlyph* g = glyphOrFallback(f, cp);
    int gw = g ? g->adv : 0;
    if (w + gw + dots > max_w || n + len + 4 > size) break;
    memcpy(&dest[n], p, len);
    n += len; w += gw; p += len;
  }
  memcpy(&dest[n], "...", 4);
}

void UIGfx::icon(const UIFont& f, uint16_t cp, int cx, int cy, Color c) {
  const UIGlyph* g = findGlyph(f, cp);
  if (!g || !g->w) return;
  int x = cx - g->w / 2 - g->x;
  int y = cy - g->h / 2 - g->y;
  drawGlyph(f, *g, x, y, c);
}
