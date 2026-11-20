#include "display.h"
#include "card_layout.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef ARDUINO
#include <SPI.h>
#endif

using namespace Layout;

static const uint16_t C_BLACK = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static inline uint16_t C_GREY()     { return tsColor(140, 140, 140); }
static inline uint16_t C_DARKGREY() { return tsColor(60, 60, 60); }
static inline uint16_t C_RED()      { return tsColor(255, 60, 60); }
static inline uint16_t C_ORANGE()   { return tsColor(255, 150, 0); }
static inline uint16_t C_YELLOW()   { return tsColor(255, 220, 0); }
static inline uint16_t C_GREEN()    { return tsColor(0, 220, 0); }
static inline uint16_t C_CYAN()     { return tsColor(0, 200, 255); }

// Shared card style (brief section 7), identical on hub and node:
// segment n uses colour ((n-1) mod 3) + 1 (brief v1.3, FW-Q22).
static uint16_t segColor(uint8_t seg) {
  switch (seg % 3) {
    case 0:  return tsColor(0xE0, 0xE0, 0xE0);
    case 1:  return tsColor(0x56, 0xCC, 0xF2);
    default: return tsColor(0xBB, 0x6B, 0xD9);
  }
}

uint8_t textWidth(const FONT_INFO& f, const char* s) {
  unsigned w = 0;
  for (; *s; s++) {
    char c = *s;
    if (c < f.startCh || c > f.endCh) continue;
    w += f.charDesc[c - f.startCh].width + 1;
  }
  return w > 255 ? 255 : (uint8_t)w;
}

// OLED power-up in two halves, so the 10 ms boost-converter settle time is
// spent in the main loop instead of a busy wait (TinyScreen::on()).
static void oledPowerBegin(TinyScreen& d) {
#ifdef ARDUINO
  (void)d;
  digitalWrite(TSP_PIN_SHDN, HIGH);
#else
  (void)d;
#endif
}
static void oledPowerFinish(TinyScreen& d) {
#ifdef ARDUINO
  d.startCommand();
  SPI1.transfer(0xAF);   // display on
  d.endTransfer();
#else
  d.on();
#endif
}

// ---------------------------------------------------------------------------

void Renderer::begin(uint8_t brightness, bool flip) {
  d_.begin();
  d_.setBitDepth(TSBitDepth16);
  d_.setFlip(flip ? 1 : 0);
  d_.setBrightness(brightness);
  d_.clearScreen();
  powered_ = true;
  modeDirty_ = true;
}

void Renderer::setMode(ScreenMode m) {
  if (m == mode_) return;
  mode_ = m;
  modeDirty_ = true;
  abortOps();
}

void Renderer::setPower(bool on, UiState& s, uint32_t nowMs) {
  if (on == powered_) return;
  powered_ = on;
  if (on) {
    oledPowerBegin(d_);
    poweringUp_ = true;
    powerOnAt_ = nowMs;
    markAllDirty(s);
  } else {
    poweringUp_ = false;
    d_.off();
    abortOps();
  }
}

void Renderer::startBlink(uint32_t nowMs) {
  blinking_ = true;
  blinkStart_ = nowMs;
  blinkPhase_ = -1;
  abortOps();
}

void Renderer::showToast(const char* text, uint32_t nowMs) {
  strncpy(toast_, text, sizeof(toast_) - 1);
  toast_[sizeof(toast_) - 1] = 0;
  toastUntil_ = nowMs + TOAST_MS;
  if (!toastUntil_) toastUntil_ = 1;
  toastDirty_ = true;
  statusDirty_ = true;
}

void Renderer::step(UiState& s, const StatusInfo& st, uint32_t nowMs, uint32_t budgetUs, uint32_t (*nowUs)()) {
  if (poweringUp_) {
    if ((uint32_t)(nowMs - powerOnAt_) < DISPLAY_ON_DELAY_MS) return;
    poweringUp_ = false;
    oledPowerFinish(d_);
  }
  if (toastUntil_ && !toastActive(nowMs)) {      // toast expired: put the hint back
    toastUntil_ = 0;
    toastDirty_ = true;
    statusDirty_ = true;
  }
  if (blinking_) {
    uint32_t el = nowMs - blinkStart_;
    if (el >= BLINK_MS) {
      blinking_ = false;
      s.dirty |= DR_FULL;                          // restore everything below the strip
      statusDirty_ = true;
    } else {
      int8_t ph = (int8_t)(el / BLINK_PHASE_MS);
      if (ph != blinkPhase_ && powered_) {
        blinkPhase_ = ph;
        // Everything except the register strip flashes (v1.4 B1).
        d_.drawRect(0, STRIP_H, 96, 64 - STRIP_H, TSRectangleFilled, (ph & 1) ? C_BLACK : C_WHITE);
      }
      return;
    }
  }
  if (!powered_) return;

  uint32_t t0 = nowUs();
  do {
    if (idle()) {
      bool need = modeDirty_ || toastDirty_ ||
                  (mode_ == SM_GAME   ? s.dirty != 0 :
                   mode_ == SM_SPLASH ? statusDirty_ :                      // hub state waits for the real screen
                                        (statusDirty_ || (s.dirty & DR_STRIP)));
      if (!need) return;
      build(s, st, nowMs);
      if (idle()) return;
    }
    exec(ops_[opPos_++]);
  } while ((uint32_t)(nowUs() - t0) < budgetUs);
}

void Renderer::exec(const DrawOp& op) {
  switch (op.type) {
    case OP_CLEAR:
      d_.clearWindow(op.x, op.y, op.w, op.h);
      break;
    case OP_RECT:
      d_.drawRect(op.x, op.y, op.w, op.h, op.fill ? TSRectangleFilled : TSRectangleNoFill, op.color);
      break;
    case OP_TEXT:
      d_.setFont(*op.font);
      d_.fontColor(op.color, op.bg);
      d_.setCursor(op.x, op.y);
      d_.print(op.text);
      break;
  }
}

// ---------------------------------------------------------------------------
// Op builders

void Renderer::opClear(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
  if (opCount_ >= MAX_OPS) return;
  DrawOp& o = ops_[opCount_++];
  o.type = OP_CLEAR; o.x = x; o.y = y; o.w = w; o.h = h;
}

void Renderer::opRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool fill, uint16_t c) {
  if (opCount_ >= MAX_OPS || w == 0 || h == 0) return;
  DrawOp& o = ops_[opCount_++];
  o.type = OP_RECT; o.x = x; o.y = y; o.w = w; o.h = h; o.fill = fill; o.color = c;
}

void Renderer::opText(uint8_t x, uint8_t y, const char* t, const FONT_INFO& f, uint16_t fg, uint16_t bg) {
  if (opCount_ >= MAX_OPS) return;
  DrawOp& o = ops_[opCount_++];
  o.type = OP_TEXT; o.x = x; o.y = y; o.font = &f; o.color = fg; o.bg = bg;
  strncpy(o.text, t, sizeof(o.text) - 1);
  o.text[sizeof(o.text) - 1] = 0;
}

void Renderer::opTextCentered(uint8_t x0, uint8_t w, uint8_t y, const char* t, const FONT_INFO& f, uint16_t fg, uint16_t bg) {
  uint8_t tw = textWidth(f, t);
  uint8_t x = (tw >= w) ? x0 : (uint8_t)(x0 + (w - tw) / 2);
  opText(x, y, t, f, fg, bg);
}

// ---------------------------------------------------------------------------

void Renderer::build(UiState& s, const StatusInfo& st, uint32_t nowMs) {
  opCount_ = opPos_ = 0;
  bool modeFull = modeDirty_;
  modeDirty_ = false;

  if (mode_ == SM_SPLASH) {
    buildSplash(st, modeFull);
    statusDirty_ = false;
    toastDirty_ = false;
    return;
  }

  // The register strip is part of every screen after the splash.
  if (modeFull) { opClear(0, 0, 96, 64); buildStrip(s); }
  else if (s.dirty & DR_STRIP) buildStrip(s);

  if (mode_ != SM_GAME) {
    buildIdle(s, st, modeFull, nowMs);
    statusDirty_ = false;
    toastDirty_ = false;
    s.dirty = 0;            // the game layout is redrawn in full when the mode changes
    return;
  }

  uint8_t d = s.dirty;
  s.dirty = 0;
  if (toastDirty_) { d |= DR_INFO; toastDirty_ = false; }
  bool full = modeFull || (d & DR_FULL);
  if (full) {
    if (!modeFull) opClear(0, STRIP_H, 96, 64 - STRIP_H);
    d |= DR_CARD | DR_HAND | DR_SEQ | DR_STATUS | DR_INFO;
  }

  if (s.msgActive) {
    if (full) buildMsg(s);  // card / hand / badge are hidden behind the message
  } else {
    if (d & DR_CARD) buildCard(s);
    if (d & DR_HAND) buildHand(s);
    if (d & DR_SEQ)  buildSeq(s);
  }
  if (d & DR_STATUS) buildStatus(s);
  if (d & DR_INFO)   buildInfo(s, nowMs);
}

// ----- register strip --------------------------------------------------------

void Renderer::buildStrip(const UiState& s) {
  static const char* const labels[4] = { "R", "L", "V", "F" };
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t bx = (uint8_t)(i * 24);
    bool set = s.reg & (1 << i);
    if (set) {
      opRect(bx, 0, 23, STRIP_H, true, C_WHITE);
      opTextCentered(bx, 23, 0, labels[i], liberationSansNarrow_8ptFontInfo, C_BLACK, C_WHITE);
    } else {
      opRect(bx, 0, 23, STRIP_H, true, C_BLACK);
      opTextCentered(bx, 23, 0, labels[i], liberationSansNarrow_8ptFontInfo, C_GREY(), C_BLACK);
      opRect(bx, 0, 23, STRIP_H, false, C_DARKGREY());   // outline last, so the letter can't cut it
    }
  }
}

// ----- card area -------------------------------------------------------------

void Renderer::buildCard(const UiState& s) {
  opClear(CARD_X, CARD_Y, CARD_W, CARD_H);
  switch (s.cardKind) {
    case CK_NONE:
      // "Waiting" state (CARD:-)
      opRect(CARD_X + 4, CARD_Y + 2, CARD_W - 8, CARD_H - 4, false, C_DARKGREY());
      opTextCentered(CARD_X, CARD_W, CARD_Y + 15, "waiting", liberationSans_8ptFontInfo, C_GREY(), C_BLACK);
      return;
    case CK_MOVE:
      buildMoveCard(s.card, CARD_X, CARD_Y, CARD_W, CARD_H);
      return;
    case CK_IF: {
      // "IF", the condition family as cells, an arrow, then the action card drawn small.
      opText(CARD_X + 1, CARD_Y, "IF", liberationSansNarrow_10ptFontInfo, C_YELLOW(), C_BLACK);
      buildFamilyIcon(s.family, CARD_X + 1, CARD_Y + 17, 5);
      uint8_t ay = CARD_Y + 21;
      opRect(CARD_X + 22, ay, 5, 1, true, C_WHITE);       // shaft
      opRect(CARD_X + 27, ay - 2, 1, 5, true, C_WHITE);   // head
      opRect(CARD_X + 28, ay - 1, 1, 3, true, C_WHITE);
      opRect(CARD_X + 29, ay, 1, 1, true, C_WHITE);
      buildMoveCard(s.card, CARD_X + 31, CARD_Y, CARD_W - 31, CARD_H);
      return;
    }
    case CK_FOR: {
      opTextCentered(CARD_X, CARD_W, CARD_Y + 1, "FOR", liberationSans_12ptFontInfo, C_YELLOW(), C_BLACK);
      // Loop icon: a 2-px ring with an arrowhead on its top edge, pointing right.
      const uint8_t lx = CARD_X + 18, ly = CARD_Y + 22, lw = 24, lh = 14;
      const uint16_t c = tsColor(0x56, 0xCC, 0xF2);
      opRect(lx, ly, lw, lh, false, c);
      opRect(lx + 1, ly + 1, lw - 2, lh - 2, false, c);
      opRect(lx + 12, ly - 3, 1, 8, true, c);
      opRect(lx + 13, ly - 2, 1, 6, true, c);
      opRect(lx + 14, ly - 1, 1, 4, true, c);
      opRect(lx + 15, ly, 1, 2, true, c);
      return;
    }
  }
}

// Shared card style, scaled and centred to fit the area aw x ah at (ax, ay):
// cell = min(floor((aw-2)/gridW), floor((ah-2)/gridH), CARD_MAX_CELL)   (brief v1.5 FW-Q28)
void Renderer::buildMoveCard(const char* steps, uint8_t ax, uint8_t ay, uint8_t aw, uint8_t ah) {
  CardLayout L;
  if (!computeCardLayout(steps, L)) return;
  uint8_t cw = (uint8_t)((aw - 2) / L.gridW), ch = (uint8_t)((ah - 2) / L.gridH);
  uint8_t cell = cw < ch ? cw : ch;
  if (cell > CARD_MAX_CELL) cell = CARD_MAX_CELL;
  if (cell < 3) cell = 3;
  uint8_t gap = cell >= 16 ? 2 : 1;
  uint8_t blk = (uint8_t)(cell - gap);
  uint8_t ox = (uint8_t)(ax + (aw - L.gridW * cell + gap) / 2);
  uint8_t oy = (uint8_t)(ay + (ah - L.gridH * cell + gap) / 2);

  // One filled block per step in path order, each straight run in its own colour.
  for (uint8_t i = 0; i < L.nSteps; i++)
    opRect(ox + L.cellX[i] * cell, oy + L.cellY[i] * cell, blk, blk, true, segColor(L.segment[i]));

  // Start cell: hollow (outline, 2 px when the blocks are big enough).
  uint8_t sx = ox + L.startX * cell, sy = oy + L.startY * cell;
  opRect(sx, sy, blk, blk, false, C_WHITE);
  if (blk >= 12) opRect(sx + 1, sy + 1, blk - 2, blk - 2, false, C_WHITE);

#if CARD_STEP_NUMBERS
  for (uint8_t i = 0; i + 1 < L.nSteps; i++) {
    char t[2] = { (char)('1' + i), 0 };
    uint8_t bx = ox + L.cellX[i] * cell, by = oy + L.cellY[i] * cell;
    opText(bx + (blk - 5) / 2, by + (blk - 8) / 2, t, thinPixel7_10ptFontInfo, C_BLACK, segColor(L.segment[i]));
  }
#endif

  // Arrow on the last block, pointing in the last direction (triangle of 1-px spans).
  uint8_t i = L.nSteps - 1;
  uint8_t bx = ox + L.cellX[i] * cell, by = oy + L.cellY[i] * cell;
  uint8_t c = blk / 2;
  uint8_t h = (uint8_t)(blk * 3 / 10);
  if (h < 1) h = 1;
  if (h > c) h = c;
  for (uint8_t k = 0; k <= h; k++) {
    uint8_t len = (uint8_t)(2 * (h - k) + 1);
    uint8_t a0 = (uint8_t)(c - h / 2 + k);     // along the arrow axis, base -> tip
    uint8_t a1 = (uint8_t)(c + h / 2 - k);
    uint8_t across = (uint8_t)(c - (h - k));
    switch (L.dir[i]) {
      case 'R': opRect(bx + a0, by + across, 1, len, true, C_BLACK); break;
      case 'L': opRect(bx + a1, by + across, 1, len, true, C_BLACK); break;
      case 'D': opRect(bx + across, by + a0, len, 1, true, C_BLACK); break;
      case 'U': opRect(bx + across, by + a1, len, 1, true, C_BLACK); break;
    }
  }
}

// Condition family as plain cells (no start marker, no arrow), brief v1.4 B2.
void Renderer::buildFamilyIcon(uint8_t fam, uint8_t x, uint8_t y, uint8_t cell) {
  static const uint8_t shapes[FAM_COUNT][4][2] = {
    { {0,0},{1,0},{9,9},{9,9} },   // D  1x2
    { {0,0},{1,0},{2,0},{9,9} },   // I3 1x3
    { {0,0},{1,0},{2,0},{3,0} },   // I4 1x4
    { {0,0},{0,1},{1,1},{9,9} },   // V  2x2 corner
    { {0,0},{1,0},{0,1},{1,1} },   // O  2x2 square
    { {0,0},{0,1},{1,1},{2,1} },   // L  3x2 L
    { {1,0},{2,0},{0,1},{1,1} },   // S  3x2 S
  };
  if (fam >= FAM_COUNT) return;
  const uint16_t c = tsColor(0xA0, 0xA0, 0xA0);
  for (uint8_t i = 0; i < 4; i++) {
    if (shapes[fam][i][0] == 9) break;
    opRect(x + shapes[fam][i][0] * cell, y + shapes[fam][i][1] * cell, cell - 1, cell - 1, true, c);
  }
}

// ----- right column ----------------------------------------------------------

void Renderer::buildHand(const UiState& s) {
  // Colour block in the player colour, with the hand number on it (brief v1.5 FW-Q29).
  uint16_t col = tsColor(s.colR, s.colG, s.colB);
  opRect(COL_X, HAND_Y, COL_W, HAND_H, true, col);
  if (!s.hand) return;                       // HAND:0 = unassigned: no number
  char t[2] = { (char)('0' + s.hand), 0 };
  int luma = (77 * s.colR + 150 * s.colG + 29 * s.colB) >> 8;
  opTextCentered(COL_X, COL_W, HAND_Y + 1, t, liberationSansNarrow_8ptFontInfo, luma < 128 ? C_WHITE : C_BLACK, col);
}

void Renderer::buildSeq(const UiState& s) {
  opClear(COL_X, SEQ_Y, COL_W, SEQ_H);
  if (s.seqKind == SEQK_NONE) return;
  opRect(COL_X, SEQ_Y, COL_W, SEQ_H, true, C_WHITE);
  char t[4];
  if (s.seqKind == SEQK_HEAD && s.seqPos == 0) {
    opTextCentered(COL_X, COL_W, SEQ_Y + 2, "HEAD", liberationSansNarrow_10ptFontInfo, C_BLACK, C_WHITE);
    return;
  }
  t[0] = (s.seqKind == SEQK_HEAD) ? 'H' : '#';
  t[1] = (char)('0' + s.seqPos);
  t[2] = 0;
  opTextCentered(COL_X, COL_W, SEQ_Y, t, liberationSans_12ptFontInfo, C_BLACK, C_WHITE);
}

void Renderer::buildStatus(const UiState& s) {
  // Steady indicators (brief v1.5 FW-Q30): NO HUB takes the row, else ACC + battery icon. Never blinks.
  opClear(COL_X, STAT_Y, COL_W, STAT_H);
  const FONT_INFO& f = thinPixel7_10ptFontInfo;
  if (s.noHub) {                             // hub silent: the whole row (most urgent)
    opTextCentered(COL_X, COL_W, STAT_Y + 1, "NO HUB", f, C_RED(), C_BLACK);
    return;
  }
  if (s.accMissing) opText(COL_X + 1, STAT_Y + 1, "ACC", f, C_ORANGE(), C_BLACK);
  if (s.batLow) {                            // low battery: battery icon at the right end of the row
    const uint8_t bx = COL_X + COL_W - 12, by = STAT_Y + 2;
    opRect(bx, by, 10, 7, false, C_YELLOW());       // body
    opRect(bx + 10, by + 2, 1, 3, true, C_YELLOW()); // terminal
    opRect(bx + 2, by + 2, 2, 3, true, C_RED());    // almost empty
  }
}

// ----- hint line -------------------------------------------------------------

void Renderer::buildInfo(const UiState& s, uint32_t nowMs) {
  opClear(0, INFO_Y, 96, INFO_H);
  if (toastActive(nowMs)) {
    opTextCentered(0, 96, INFO_Y, toast_, liberationSansNarrow_8ptFontInfo, C_CYAN(), C_BLACK);
  } else if (s.hint[0]) {
    // Normal font if it fits, else the narrow pixel font (14 x 'W' = 84 px): brief v1.6 X1.
    const FONT_INFO& f = textWidth(liberationSansNarrow_8ptFontInfo, s.hint) <= 95 ? liberationSansNarrow_8ptFontInfo
                                                                                   : thinPixel7_10ptFontInfo;
    opTextCentered(0, 96, INFO_Y + (&f == &thinPixel7_10ptFontInfo ? 1 : 0), s.hint, f, C_WHITE, C_BLACK);
  }
}

// ----- MSG -------------------------------------------------------------------

// MSG uses y12-41 full width; the status area and the hint line stay visible (brief v1.5 FW-Q34).
void Renderer::buildMsg(const UiState& s) {
  static const FONT_INFO* const fonts[] = {
    &liberationSans_16ptFontInfo, &liberationSans_14ptFontInfo, &liberationSans_12ptFontInfo,
    &liberationSans_10ptFontInfo, &liberationSansNarrow_10ptFontInfo, &liberationSans_8ptFontInfo,
    &liberationSansNarrow_8ptFontInfo,
  };
  const uint8_t nFonts = sizeof(fonts) / sizeof(fonts[0]);
  const uint8_t maxW = 94, top = CARD_Y;
  opClear(0, STRIP_H, 96, INFO_Y - STRIP_H);

  for (uint8_t i = 0; i < nFonts; i++) {
    if (textWidth(*fonts[i], s.msg) <= maxW && fonts[i]->height <= MSG_H) {
      opTextCentered(0, 96, (uint8_t)(top + (MSG_H - fonts[i]->height) / 2), s.msg, *fonts[i], C_WHITE, C_BLACK);
      return;
    }
  }
  // Two lines: split at the space nearest the middle (or mid-word if none).
  char l1[MAX_MSG_CHARS + 1], l2[MAX_MSG_CHARS + 1];
  int n = (int)strlen(s.msg), cut = -1;
  for (int i = 0; i < n; i++)
    if (s.msg[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) cut = i;
  int a = (cut < 0) ? (n + 1) / 2 : cut, b = (cut < 0) ? a : cut + 1;
  memcpy(l1, s.msg, a); l1[a] = 0;
  strcpy(l2, s.msg + b);
  const FONT_INFO* f = fonts[nFonts - 1];
  for (uint8_t i = 0; i < nFonts; i++) {
    if (textWidth(*fonts[i], l1) <= maxW && textWidth(*fonts[i], l2) <= maxW && fonts[i]->height * 2 + 1 <= MSG_H) { f = fonts[i]; break; }
  }
  uint8_t y = (uint8_t)(top + (MSG_H - (f->height * 2 + 1)) / 2);
  opTextCentered(0, 96, y, l1, *f, C_WHITE, C_BLACK);
  opTextCentered(0, 96, (uint8_t)(y + f->height + 1), l2, *f, C_WHITE, C_BLACK);
}

// ----- disconnected / serial-test screens ------------------------------------

void Renderer::buildIdle(const UiState& s, const StatusInfo& st, bool full, uint32_t nowMs) {
  char t[32];
  if (full) {
    if (mode_ == SM_TEST_IDLE)
      opTextCentered(0, 96, 12, "SERIAL TEST", liberationSansNarrow_10ptFontInfo, C_CYAN(), C_BLACK);
    else
      opTextCentered(0, 96, 12, "DISCONNECTED", liberationSansNarrow_10ptFontInfo, tsColor(255, 80, 40), C_BLACK);
    snprintf(t, sizeof(t), BLE_NAME_PREFIX "%04X", st.nodeId);
    opTextCentered(0, 96, 27, t, liberationSans_8ptFontInfo, C_WHITE, C_BLACK);
    if (!st.testMode && !st.bleOk) opTextCentered(0, 96, 39, "BLE ERROR", liberationSansNarrow_8ptFontInfo, C_RED(), C_BLACK);
    else                           opTextCentered(0, 96, 39, "fw " FW_VERSION, liberationSansNarrow_8ptFontInfo, C_GREY(), C_BLACK);
  }
  // Bottom line: toast, else ACC warning, else orientation + last gesture (calibration without a hub).
  opClear(0, INFO_Y, 96, INFO_H);
  if (toastActive(nowMs)) {
    opTextCentered(0, 96, INFO_Y, toast_, liberationSansNarrow_8ptFontInfo, C_CYAN(), C_BLACK);
  } else if (s.accMissing) {
    opTextCentered(0, 96, INFO_Y, "ACC not found", liberationSansNarrow_8ptFontInfo, C_ORANGE(), C_BLACK);
  } else {
    const char* face = !st.faceKnown ? "--" : st.faceDown ? "DOWN" : "UP";
    if (st.lastEvent[0]) snprintf(t, sizeof(t), "%s   last %s", face, st.lastEvent);
    else                 snprintf(t, sizeof(t), "%s", face);
    opTextCentered(0, 96, INFO_Y, t, liberationSansNarrow_8ptFontInfo, C_WHITE, C_BLACK);
  }
}

// ----- boot splash (v1.4 A1) -------------------------------------------------

void Renderer::buildSplash(const StatusInfo& st, bool full) {
  char t[32];
  const FONT_INFO& f = liberationSansNarrow_8ptFontInfo;
  if (full) {
    opClear(0, 0, 96, 64);
    opText(2, 0, "CCNODE fw " FW_VERSION, liberationSansNarrow_10ptFontInfo, C_CYAN(), C_BLACK);
    snprintf(t, sizeof(t), "ID %04X   rst %s", st.nodeId, st.resetCause);
    opText(2, 14, t, f, C_WHITE, C_BLACK);
  }
  // Live lines, refreshed while the splash is up.
  opClear(0, 26, 96, 38);
  if (st.acc == ACCS_OK)           opText(2, 26, "ACC OK", f, C_GREEN(), C_BLACK);
  else if (st.acc == ACCS_MISSING) opText(2, 26, "ACC not found", f, C_RED(), C_BLACK);
  else                             opText(2, 26, "ACC ...", f, C_GREY(), C_BLACK);

  if (st.acc == ACCS_OK && st.faceKnown) {
    int mg = (int)(st.zAvg * 1000.0f + (st.zAvg >= 0 ? 0.5f : -0.5f));
    int a = mg < 0 ? -mg : mg;
    snprintf(t, sizeof(t), "zAvg %c%d.%02d", mg < 0 ? '-' : '+', a / 1000, (a % 1000) / 10);
    opText(2, 38, t, f, C_WHITE, C_BLACK);
    opText(58, 38, st.faceDown ? "DOWN" : "UP", f, st.faceDown ? C_ORANGE() : C_GREEN(), C_BLACK);
  } else {
    opText(2, 38, "zAvg --", f, C_GREY(), C_BLACK);
  }

  if (st.testMode)   opText(2, 50, "BLE off (test mode)", f, C_GREY(), C_BLACK);
  else if (st.bleOk) { snprintf(t, sizeof(t), "BLE OK  %s", st.bleState ? st.bleState : ""); opText(2, 50, t, f, C_GREEN(), C_BLACK); }
  else               opText(2, 50, "BLE FAIL (shield?)", f, C_RED(), C_BLACK);
}
