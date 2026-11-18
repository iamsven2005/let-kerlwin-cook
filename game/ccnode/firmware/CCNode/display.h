// Screen renderer for the 96x64 OLED (brief v1.4 layout).
// Changes are turned into a short list of draw operations which are executed a
// few at a time (RENDER_BUDGET_US per loop pass), so drawing never stalls
// gesture sampling or BLE. Only dirty regions are redrawn.
//
//   y  0-10  REGISTER STRIP  [ R ][ L ][ V ][ F ]   full width, never covered (except the boot splash)
//   y 11     gap
//   y 12-52  CARD AREA x0-59           | RIGHT COLUMN x61-95
//                                      |   y12-23 colour block + hand number
//                                      |   y25-40 sequence badge (#n / Hn / HEAD)
//                                      |   y42-52 status area (NO HUB / ACC / BAT)
//   y 53-63  HINT LINE  full width (HINT:, "DEBUG ON/OFF" toasts)
//
//   MSG:   text in x0-95 / y12-41; status area and hint line stay.
//   BLINK: flashes y11-63; the strip stays.
#pragma once
#include <stdint.h>
#include <TinyScreen.h>
#include "protocol.h"

enum ScreenMode : uint8_t {
  SM_SPLASH = 0,         // boot splash (~3 s)
  SM_DISCONNECTED,       // BLE advertising, waiting for the hub
  SM_TEST_IDLE,          // serial test mode, before any hub command arrived
  SM_GAME,               // hub connected: card + right column (or MSG)
};

enum AccStatus : uint8_t { ACCS_PROBING = 0, ACCS_OK, ACCS_MISSING };

struct StatusInfo {
  uint16_t nodeId;
  bool     faceKnown;       // orientation established (accelerometer up and averaged)
  bool     faceDown;
  float    zAvg;
  uint8_t  acc;             // AccStatus
  bool     bleOk;           // stack initialised
  bool     testMode;
  const char* bleState;     // "adv-fast", "subscribed", ...
  char     resetCause[6];   // "PWR", "WDT", ...
  char     lastEvent[8];    // e.g. "T:R"
};

// 16-bit colour in the TinyScreen library's default (BGR) 16-bit format.
inline uint16_t tsColor(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
}

class Renderer {
 public:
  explicit Renderer(TinyScreen& d) : d_(d) {}
  void begin(uint8_t brightness, bool flip);

  void setMode(ScreenMode m);
  ScreenMode mode() const { return mode_; }
  void markStatusDirty() { statusDirty_ = true; }          // splash / idle-screen live content
  void markAllDirty(UiState& s) { s.dirty |= DR_ALL; statusDirty_ = true; modeDirty_ = true; abortOps(); }

  // Display power (face-down power save with DOFF:1). Switching on is
  // non-blocking: the boost converter gets DISPLAY_ON_DELAY_MS before the
  // panel is enabled, instead of TinyScreen::on()'s 10 ms busy wait.
  void setPower(bool on, UiState& s, uint32_t nowMs);
  bool powered() const { return powered_; }

  void startBlink(uint32_t nowMs);
  bool blinking() const { return blinking_; }

  // Short message in the hint line (e.g. "DEBUG ON"), TOAST_MS long.
  void showToast(const char* text, uint32_t nowMs);

  // Do up to budgetUs of drawing work. nowUs() is a microsecond clock.
  void step(UiState& s, const StatusInfo& st, uint32_t nowMs, uint32_t budgetUs, uint32_t (*nowUs)());
  bool idle() const { return opCount_ == opPos_; }

 private:
  enum OpType : uint8_t { OP_CLEAR, OP_RECT, OP_TEXT };
  struct DrawOp {
    uint8_t type, x, y, w, h, fill;
    uint16_t color, bg;
    const FONT_INFO* font;
    char text[24];
  };
  static const uint8_t MAX_OPS = 72;

  void abortOps() { opCount_ = opPos_ = 0; }
  void build(UiState& s, const StatusInfo& st, uint32_t nowMs);
  void exec(const DrawOp& op);

  // op builders
  void opClear(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
  void opRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill, uint16_t c);
  void opText(uint8_t x, uint8_t y, const char* t, const FONT_INFO& f, uint16_t fg, uint16_t bg);
  void opTextCentered(uint8_t x0, uint8_t w, uint8_t y, const char* t, const FONT_INFO& f, uint16_t fg, uint16_t bg);

  void buildStrip(const UiState& s);
  void buildCard(const UiState& s);
  void buildMoveCard(const char* steps, uint8_t ax, uint8_t ay, uint8_t aw, uint8_t ah);
  void buildFamilyIcon(uint8_t fam, uint8_t x, uint8_t y, uint8_t cell);
  void buildHand(const UiState& s);
  void buildSeq(const UiState& s);
  void buildStatus(const UiState& s);
  void buildInfo(const UiState& s, uint32_t nowMs);
  void buildMsg(const UiState& s);
  void buildIdle(const UiState& s, const StatusInfo& st, bool full, uint32_t nowMs);
  void buildSplash(const StatusInfo& st, bool full);
  bool toastActive(uint32_t nowMs) const { return toastUntil_ && (int32_t)(nowMs - toastUntil_) < 0; }

  TinyScreen& d_;
  DrawOp   ops_[MAX_OPS];
  uint8_t  opCount_ = 0, opPos_ = 0;
  ScreenMode mode_ = SM_SPLASH;
  bool     modeDirty_ = true;
  bool     statusDirty_ = true;
  bool     powered_ = true;
  bool     poweringUp_ = false;
  uint32_t powerOnAt_ = 0;
  bool     blinking_ = false;
  uint32_t blinkStart_ = 0;
  int8_t   blinkPhase_ = -1;
  char     toast_[24] = "";
  uint32_t toastUntil_ = 0;
  bool     toastDirty_ = false;
};

// Pixel width of a string in a TinyScreen font (same rule as getPrintWidth).
uint8_t textWidth(const FONT_INFO& f, const char* s);

// Card-area geometry, shared with the tests.
namespace Layout {
  const uint8_t STRIP_H = 11;
  const uint8_t CARD_X = 0, CARD_Y = 12, CARD_W = 60, CARD_H = 41;
  const uint8_t COL_X = 61, COL_W = 35;
  const uint8_t HAND_Y = 12, HAND_H = 12;
  const uint8_t SEQ_Y = 25, SEQ_H = 16;
  const uint8_t STAT_Y = 42, STAT_H = 11;
  const uint8_t INFO_Y = 53, INFO_H = 11;
  const uint8_t MSG_H = 30;                // MSG text area y12..41
}
