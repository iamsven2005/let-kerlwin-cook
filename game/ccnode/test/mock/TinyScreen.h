// Host-side mock of the TinyScreen library: same API subset as used by the
// firmware, rendering into a 96x64 framebuffer (16-bit BGR, like the SSD1331
// in TSBitDepth16 mode). Text rendering replicates TinyScreen::write().
#pragma once
#include <stdint.h>
#include <string.h>

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))

typedef struct { const uint8_t width; const uint16_t offset; } FONT_CHAR_INFO;
typedef struct {
  const unsigned char height; const char startCh; const char endCh;
  const FONT_CHAR_INFO* charDesc; const unsigned char* bitmap;
} FONT_INFO;

#include "font.h"   // the real TinyScreen fonts (include path points at the library)

const uint8_t TinyScreenPlus = 2;
const uint8_t TSRectangleFilled = 1;
const uint8_t TSRectangleNoFill = 0;
const uint8_t TSBitDepth8 = 0;
const uint8_t TSBitDepth16 = 1;

class TinyScreen {
 public:
  explicit TinyScreen(uint8_t) {}
  uint16_t fb[64][96];
  bool     isOn = false;
  uint32_t opCount = 0;       // drawing commands issued
  uint32_t pixelBytes = 0;    // pixel data streamed (cost model)
  uint8_t  buttons = 0;

  void begin() { memset(fb, 0, sizeof(fb)); isOn = true; }
  void on() { isOn = true; }
  void off() { isOn = false; }
  void setBitDepth(uint8_t) {}
  void setFlip(uint8_t) {}
  void setBrightness(uint8_t) {}
  uint8_t getButtons() { return buttons; }
  void clearScreen() { clearWindow(0, 0, 96, 64); }

  void clearWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    opCount++;
    if (x > 95 || y > 63) return;
    fill(x, y, clampEnd(x, w, 95), clampEnd(y, h, 63), 0);
  }
  void drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t f, uint16_t c) {
    opCount++;
    if (x > 95 || y > 63) return;
    uint8_t x2 = clampEnd(x, w, 95), y2 = clampEnd(y, h, 63);
    if (f) { fill(x, y, x2, y2, c); return; }
    for (int i = x; i <= x2; i++) { fb[y][i] = c; fb[y2][i] = c; }
    for (int j = y; j <= y2; j++) { fb[j][x] = c; fb[j][x2] = c; }
  }

  void setFont(const FONT_INFO& f) { font_ = &f; }
  void fontColor(uint16_t f, uint16_t b) { fg_ = f; bg_ = b; }
  void setCursor(uint8_t x, uint8_t y) { cx_ = x; cy_ = y; }
  void print(const char* s) { opCount++; while (*s) write((uint8_t)*s++); }

 private:
  static uint8_t clampEnd(uint8_t s, uint8_t len, uint8_t mx) { int e = s + len - 1; return (uint8_t)(e > mx ? mx : e); }
  void fill(int x, int y, int x2, int y2, uint16_t c) {
    for (int j = y; j <= y2; j++) for (int i = x; i <= x2; i++) fb[j][i] = c;
  }
  // GDDRAM window streaming, as the SSD1331 does with setX/setY + data writes
  int wx0_, wx1_, wy0_, wy1_, px_, py_;
  void window(int x0, int x1, int y0, int y1) {
    if (x0 > 95) x0 = 95; if (x1 > 95) x1 = 95; if (y0 > 63) y0 = 63; if (y1 > 63) y1 = 63;
    wx0_ = x0; wx1_ = x1; wy0_ = y0; wy1_ = y1; px_ = x0; py_ = y0;
  }
  void push(uint16_t c) {
    pixelBytes += 2;
    fb[py_][px_] = c;
    if (++px_ > wx1_) { px_ = wx0_; if (++py_ > wy1_) py_ = wy0_; }
  }
  void write(uint8_t ch) {   // mirrors TinyScreen::write()
    const uint8_t xMax = 95, yMax = 63;
    if (!font_) return;
    if (ch < font_->startCh || ch > font_->endCh) return;
    if (cx_ > xMax || cy_ > yMax) return;
    uint8_t chWidth = font_->charDesc[ch - font_->startCh].width;
    uint8_t bytesPerRow = chWidth / 8;
    if (chWidth > bytesPerRow * 8) bytesPerRow++;
    uint16_t offset = font_->charDesc[ch - font_->startCh].offset + (bytesPerRow * font_->height) - 1;
    window(cx_, cx_ + chWidth + 1, cy_, cy_ + font_->height);
    for (uint8_t y = 0; y < font_->height && y + cy_ < yMax + 1; y++) {
      push(bg_);
      for (uint8_t byte = 0; byte < bytesPerRow; byte++) {
        uint8_t data = font_->bitmap[offset - y - ((bytesPerRow - byte - 1) * font_->height)];
        uint8_t bits = byte * 8;
        for (uint8_t i = 0; i < 8 && (bits + i) < chWidth && (bits + i + cx_) < xMax; i++)
          push((data & (0x80 >> i)) ? fg_ : bg_);
      }
      if ((cx_ + chWidth) < xMax) push(bg_);
    }
    cx_ += chWidth + 1;
  }
  const FONT_INFO* font_ = nullptr;
  uint16_t fg_ = 0xFFFF, bg_ = 0;
  uint8_t cx_ = 0, cy_ = 0;
};
