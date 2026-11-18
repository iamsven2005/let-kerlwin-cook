#include "protocol.h"
#include <string.h>
#include <stdio.h>

static const char* const kFamilyCodes[FAM_COUNT] = { "D", "I3", "I4", "V", "O", "L", "S" };

const char* familyCode(uint8_t fam) { return fam < FAM_COUNT ? kFamilyCodes[fam] : "?"; }

void uiStateInit(UiState& s) {
  s.accMissing = false;
  s.batLow = false;
  uiStateReset(s);
}

void uiStateReset(UiState& s) {
  s.cardKind = CK_NONE;
  s.card[0] = 0;
  s.family = FAM_D;
  s.seqKind = SEQK_NONE;
  s.seqPos = 0;
  s.reg = 0;
  s.colR = s.colG = s.colB = 0x60;   // neutral grey until COL:
  s.hand = 0;
  s.msgActive = false;
  s.msg[0] = 0;
  s.hint[0] = 0;
  s.doffAllowed = false;             // v1.4 A1: default DOFF:0, display always on
  s.noHub = false;
  s.dirty = DR_ALL;
}

static bool startsWith(const char* s, const char* p, const char** rest) {
  size_t n = strlen(p);
  if (strncmp(s, p, n) != 0) return false;
  *rest = s + n;
  return true;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// 1..4 of U/D/L/R, copied into out. False if malformed.
static bool parseSteps(const char* a, char out[MAX_CARD_STEPS + 1]) {
  size_t n = strlen(a);
  if (n < 1 || n > MAX_CARD_STEPS) return false;
  for (size_t i = 0; i < n; i++)
    if (a[i] != 'U' && a[i] != 'D' && a[i] != 'L' && a[i] != 'R') return false;
  memcpy(out, a, n);
  out[n] = 0;
  return true;
}

// Copy printable text, truncating to max chars; other bytes become '?'.
static void copyText(char* out, const char* in, size_t max) {
  size_t n = 0;
  for (; in[n] && n < max; n++) out[n] = (in[n] >= ' ' && in[n] <= '~') ? in[n] : '?';
  out[n] = 0;
}

static HubCmd applyCard(UiState& s, const char* a) {
  uint8_t kind, fam = FAM_D;
  char steps[MAX_CARD_STEPS + 1] = "";
  const char* r;
  if (strcmp(a, "-") == 0) {
    kind = CK_NONE;
  } else if (strcmp(a, "FOR") == 0) {
    kind = CK_FOR;
  } else if (startsWith(a, "IF:", &r)) {
    // CARD:IF:<family>:<steps>   e.g. CARD:IF:O:LLU
    const char* colon = strchr(r, ':');
    if (!colon) return HC_IGNORED;
    size_t fl = (size_t)(colon - r);
    uint8_t f = FAM_COUNT;
    for (uint8_t i = 0; i < FAM_COUNT; i++)
      if (strlen(kFamilyCodes[i]) == fl && strncmp(r, kFamilyCodes[i], fl) == 0) f = i;
    if (f == FAM_COUNT) return HC_IGNORED;
    if (!parseSteps(colon + 1, steps)) return HC_IGNORED;
    kind = CK_IF;
    fam = f;
  } else {
    if (!parseSteps(a, steps)) return HC_IGNORED;
    kind = CK_MOVE;
  }
  if (s.msgActive) { s.msgActive = false; s.dirty |= DR_FULL; }   // any CARD: clears MSG (HINT stays)
  if (kind != s.cardKind || fam != s.family || strcmp(steps, s.card) != 0) {
    s.cardKind = kind;
    s.family = fam;
    strcpy(s.card, steps);
    s.dirty |= DR_CARD;
  }
  return HC_APPLIED;
}

HubCmd applyHubLine(UiState& s, const char* line) {
  const char* a;

  if (startsWith(line, "CARD:", &a)) return applyCard(s, a);

  if (startsWith(line, "SEQ:", &a)) {
    // v1.4 B3: SEQ:<n> member at n (1 = leftmost), SEQ:H<n> head at n,
    // SEQ:H head without a number, SEQ:- none. n is one digit 1..9.
    uint8_t kind, pos = 0;
    if (strcmp(a, "-") == 0) kind = SEQK_NONE;
    else if (strcmp(a, "H") == 0) kind = SEQK_HEAD;
    else if (a[0] == 'H' && a[1] >= '1' && a[1] <= '9' && a[2] == 0) { kind = SEQK_HEAD; pos = (uint8_t)(a[1] - '0'); }
    else if (a[0] >= '1' && a[0] <= '9' && a[1] == 0) { kind = SEQK_MEMBER; pos = (uint8_t)(a[0] - '0'); }
    else return HC_IGNORED;
    if (kind != s.seqKind || pos != s.seqPos) { s.seqKind = kind; s.seqPos = pos; s.dirty |= DR_SEQ; }
    return HC_APPLIED;
  }

  if (startsWith(line, "REG:", &a)) {
    // Positional R, L, V, F (brief section 5). F is shown as given.
    if (strlen(a) != 4) return HC_IGNORED;
    uint8_t m = 0;
    for (int i = 0; i < 4; i++) {
      if (a[i] == '1') m |= (uint8_t)(1 << i);
      else if (a[i] != '0') return HC_IGNORED;
    }
    if (m != s.reg) { s.reg = m; s.dirty |= DR_STRIP; }
    return HC_APPLIED;
  }

  if (startsWith(line, "COL:", &a)) {
    if (strlen(a) != 6) return HC_IGNORED;
    int v[6];
    for (int i = 0; i < 6; i++) if ((v[i] = hexVal(a[i])) < 0) return HC_IGNORED;
    uint8_t r = (uint8_t)(v[0] << 4 | v[1]), g = (uint8_t)(v[2] << 4 | v[3]), b = (uint8_t)(v[4] << 4 | v[5]);
    if (r != s.colR || g != s.colG || b != s.colB) {
      s.colR = r; s.colG = g; s.colB = b;
      s.dirty |= DR_HAND;                 // the colour block also carries the hand number
    }
    return HC_APPLIED;
  }

  if (startsWith(line, "HAND:", &a)) {
    if (!(a[0] >= '0' && a[0] <= '6' && a[1] == 0)) return HC_IGNORED;   // HAND:0 = unassigned
    uint8_t h = (uint8_t)(a[0] - '0');
    if (h != s.hand) { s.hand = h; s.dirty |= DR_HAND; }
    return HC_APPLIED;
  }

  if (startsWith(line, "MSG:", &a)) {
    // Empty MSG: clears; text longer than 14 chars is truncated.
    if (a[0] == 0) {
      if (s.msgActive) { s.msgActive = false; s.dirty |= DR_FULL; }
      return HC_APPLIED;
    }
    char m[MAX_MSG_CHARS + 1];
    copyText(m, a, MAX_MSG_CHARS);
    if (!s.msgActive || strcmp(m, s.msg) != 0) {
      strcpy(s.msg, m);
      s.msgActive = true;
      s.dirty |= DR_FULL;
    }
    return HC_APPLIED;
  }

  if (startsWith(line, "HINT:", &a)) {
    // v1.4 B4 / v1.6 X1: one-line hint, documented max 14 chars (up to 16 accepted, longer cut).
    // Empty clears. CARD: doesn't clear it.
    char h[MAX_HINT_CHARS + 1];
    copyText(h, a, MAX_HINT_CHARS);
    if (strcmp(h, s.hint) != 0) { strcpy(s.hint, h); s.dirty |= DR_INFO; }
    return HC_APPLIED;
  }

  if (strcmp(line, "BLINK") == 0) return HC_BLINK;

  if (startsWith(line, "DOFF:", &a)) {
    if (strcmp(a, "1") == 0) s.doffAllowed = true;
    else if (strcmp(a, "0") == 0) s.doffAllowed = false;
    else return HC_IGNORED;
    return HC_APPLIED;
  }

  if (strcmp(line, "PING") == 0) return HC_PING;

  return HC_IGNORED;
}

void uiSetNoHub(UiState& s, bool on)      { if (on != s.noHub)      { s.noHub = on;      s.dirty |= DR_STATUS; } }
void uiSetAccMissing(UiState& s, bool on) { if (on != s.accMissing) { s.accMissing = on; s.dirty |= DR_STATUS; } }
void uiSetBatLow(UiState& s, bool on)     { if (on != s.batLow)     { s.batLow = on;     s.dirty |= DR_STATUS; } }

// ---------------------------------------------------------------------------

void LineAssembler::finish(void (*cb)(const char*, void*), void* ctx) {
  if (overflow_) { dropped_++; }
  else if (len_ > 0) { buf_[len_] = 0; cb(buf_, ctx); }
  len_ = 0;
  overflow_ = false;
}

void LineAssembler::feed(const uint8_t* data, size_t len, void (*cb)(const char*, void*), void* ctx) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == 0) { finish(cb, ctx); continue; }
    if (c == '\r') continue;
    if (len_ >= MAX_LINE) { overflow_ = true; continue; }
    buf_[len_++] = c;
  }
}

void LineAssembler::endOfWrite(void (*cb)(const char*, void*), void* ctx) {
  if (len_ > 0 || overflow_) finish(cb, ctx);
}

void fmtHello(char* out, size_t n, uint16_t nodeId, const char* fw) {
  snprintf(out, n, "HELLO:%04X:%s", nodeId, fw);
}

void fmtButtons(char* out, size_t n, uint8_t mask) {
  snprintf(out, n, "B:%X", mask & 0x0F);
}

void fmtBattery(char* out, size_t n, uint8_t percent) {
  snprintf(out, n, "BAT:%u", percent > 100 ? 100 : percent);
}

void fmtFaceSync(char* out, size_t n, bool faceDown) {
  snprintf(out, n, "FS:%d", faceDown ? 1 : 0);
}
