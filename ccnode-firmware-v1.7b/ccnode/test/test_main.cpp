// Host-side tests for the CCNode firmware logic.
//   make -C test        (see test/Makefile)
// Covers: hub message parsing, line framing, card layout, buttons, node ID,
// flip/tap detection on synthetic accelerometer traces, and the renderer
// (incremental redraw == full redraw; screenshots written to test/out/).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <random>

#include "config.h"
#include "protocol.h"
#include "card_layout.h"
#include "buttons.h"
#include "node_id.h"
#include "gestures.h"
#include "display.h"
#include "battery.h"
#include "soft_i2c.h"
#include "accel_core.h"
#include "i2c_sim.h"
#include <array>
#include <algorithm>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ_STR(a, b) do { if (strcmp((a), (b)) == 0) g_pass++; else { g_fail++; printf("  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

// =============================================================================
// Protocol
// =============================================================================
static void testProtocol() {
  printf("protocol (v1.4)\n");
  UiState s; uiStateInit(s);
  CHECK(s.dirty == DR_ALL);
  // v1.4 defaults: DOFF:0 (display always on), no card, no badge, REG 0000, grey, no hand, no msg, no hint
  CHECK(!s.doffAllowed); CHECK(s.cardKind == CK_NONE); CHECK(s.seqKind == SEQK_NONE); CHECK(s.reg == 0);
  CHECK(s.colR == 0x60 && s.colG == 0x60 && s.colB == 0x60); CHECK(s.hand == 0); CHECK(!s.msgActive); CHECK(!s.hint[0]);
  CHECK(!s.noHub && !s.accMissing && !s.batLow);
  s.dirty = 0;

  // movement cards
  CHECK(applyHubLine(s, "CARD:UUR") == HC_APPLIED); CHECK(s.cardKind == CK_MOVE); CHECK_EQ_STR(s.card, "UUR"); CHECK(s.dirty & DR_CARD);
  s.dirty = 0;
  CHECK(applyHubLine(s, "CARD:UUR") == HC_APPLIED); CHECK(s.dirty == 0);          // unchanged: no redraw
  CHECK(applyHubLine(s, "CARD:-") == HC_APPLIED); CHECK(s.cardKind == CK_NONE);
  CHECK(applyHubLine(s, "CARD:") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:UUUUU") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:UX") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:uu") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:RDL") == HC_APPLIED); CHECK_EQ_STR(s.card, "RDL");   // square card
  // control cards (v1.4 B2)
  s.dirty = 0;
  CHECK(applyHubLine(s, "CARD:IF:O:LLU") == HC_APPLIED); CHECK(s.cardKind == CK_IF); CHECK(s.family == FAM_O); CHECK_EQ_STR(s.card, "LLU");
  CHECK(s.dirty & DR_CARD);
  CHECK(applyHubLine(s, "CARD:IF:I4:R") == HC_APPLIED); CHECK(s.family == FAM_I4); CHECK_EQ_STR(s.card, "R");
  CHECK(applyHubLine(s, "CARD:IF:I3:UU") == HC_APPLIED); CHECK(s.family == FAM_I3);
  const char* fams[] = { "D", "I3", "I4", "V", "O", "L", "S" };
  for (int i = 0; i < 7; i++) {
    std::string l = std::string("CARD:IF:") + fams[i] + ":D";
    CHECK(applyHubLine(s, l.c_str()) == HC_APPLIED); CHECK(s.family == i); CHECK_EQ_STR(familyCode(s.family), fams[i]);
  }
  CHECK(applyHubLine(s, "CARD:IF:X:LLU") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:IF:I:LLU") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:IF:O:") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:IF:O") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:IF:O:LLUUU") == HC_IGNORED);
  CHECK(applyHubLine(s, "CARD:IF:o:LLU") == HC_IGNORED);
  CHECK(s.cardKind == CK_IF && s.family == FAM_S);                               // unchanged by the rejects
  CHECK(applyHubLine(s, "CARD:FOR") == HC_APPLIED); CHECK(s.cardKind == CK_FOR);
  CHECK(applyHubLine(s, "CARD:FOR:2") == HC_IGNORED);
  CHECK(strlen("CARD:IF:I4:UUUU") + 1 <= 20);                                    // longest form fits a packet

  // sequence badge (v1.4 B3)
  CHECK(applyHubLine(s, "SEQ:H") == HC_APPLIED); CHECK(s.seqKind == SEQK_HEAD && s.seqPos == 0);
  CHECK(applyHubLine(s, "SEQ:H2") == HC_APPLIED); CHECK(s.seqKind == SEQK_HEAD && s.seqPos == 2);
  CHECK(applyHubLine(s, "SEQ:1") == HC_APPLIED); CHECK(s.seqKind == SEQK_MEMBER && s.seqPos == 1);
  CHECK(applyHubLine(s, "SEQ:7") == HC_APPLIED); CHECK(s.seqKind == SEQK_MEMBER && s.seqPos == 7);
  CHECK(applyHubLine(s, "SEQ:0") == HC_IGNORED);
  CHECK(applyHubLine(s, "SEQ:10") == HC_IGNORED);
  CHECK(applyHubLine(s, "SEQ:H0") == HC_IGNORED);
  CHECK(applyHubLine(s, "SEQ:H12") == HC_IGNORED);
  CHECK(applyHubLine(s, "SEQ:h2") == HC_IGNORED);
  CHECK(s.seqKind == SEQK_MEMBER && s.seqPos == 7);
  CHECK(applyHubLine(s, "SEQ:-") == HC_APPLIED); CHECK(s.seqKind == SEQK_NONE);

  s.dirty = 0;
  CHECK(applyHubLine(s, "REG:0010") == HC_APPLIED); CHECK(s.reg == REG_V); CHECK(s.dirty & DR_STRIP);
  CHECK(applyHubLine(s, "REG:1001") == HC_APPLIED); CHECK(s.reg == (REG_R | REG_F));
  CHECK(applyHubLine(s, "REG:101") == HC_IGNORED);
  CHECK(applyHubLine(s, "REG:10a1") == HC_IGNORED); CHECK(s.reg == (REG_R | REG_F));

  CHECK(applyHubLine(s, "COL:FF8000") == HC_APPLIED); CHECK(s.colR == 0xFF && s.colG == 0x80 && s.colB == 0);
  CHECK(applyHubLine(s, "COL:00ff7f") == HC_APPLIED); CHECK(s.colG == 0xFF && s.colB == 0x7F);
  CHECK(applyHubLine(s, "COL:12345") == HC_IGNORED);
  CHECK(applyHubLine(s, "COL:GG0000") == HC_IGNORED);

  CHECK(applyHubLine(s, "HAND:3") == HC_APPLIED); CHECK(s.hand == 3);
  CHECK(applyHubLine(s, "HAND:7") == HC_IGNORED); CHECK(s.hand == 3);
  CHECK(applyHubLine(s, "HAND:0") == HC_APPLIED); CHECK(s.hand == 0);

  // MSG
  s.dirty = 0;
  CHECK(applyHubLine(s, "MSG:P1 WINS") == HC_APPLIED); CHECK(s.msgActive); CHECK_EQ_STR(s.msg, "P1 WINS"); CHECK(s.dirty & DR_FULL);
  CHECK(applyHubLine(s, "SEQ:H") == HC_APPLIED); CHECK(s.msgActive);           // other updates keep MSG
  CHECK(applyHubLine(s, "MSG:ABCDEFGHIJKLMNOPQ") == HC_APPLIED); CHECK_EQ_STR(s.msg, "ABCDEFGHIJKLMN");
  CHECK(applyHubLine(s, "CARD:FOR") == HC_APPLIED); CHECK(!s.msgActive);         // any CARD clears MSG
  CHECK(applyHubLine(s, "MSG:LOBBY") == HC_APPLIED); CHECK(s.msgActive);
  CHECK(applyHubLine(s, "MSG:") == HC_APPLIED); CHECK(!s.msgActive);

  // HINT (v1.4 B4)
  s.dirty = 0;
  CHECK(applyHubLine(s, "HINT:tap R to start") == HC_APPLIED); CHECK_EQ_STR(s.hint, "tap R to start"); CHECK(s.dirty & DR_INFO);
  CHECK(applyHubLine(s, "HINT:FLIP TO START!") == HC_APPLIED); CHECK_EQ_STR(s.hint, "FLIP TO START!");     // 14 = documented max
  CHECK(strlen("HINT:FLIP TO START!") + 1 == 20);                                                             // fits one packet
  CHECK(applyHubLine(s, "HINT:0123456789ABCDEFGHIJ") == HC_APPLIED); CHECK_EQ_STR(s.hint, "0123456789ABCDEF");   // up to 16 accepted
  CHECK(applyHubLine(s, "CARD:UUR") == HC_APPLIED); CHECK_EQ_STR(s.hint, "0123456789ABCDEF"); // CARD doesn't clear it
  CHECK(applyHubLine(s, "MSG:LOBBY") == HC_APPLIED); CHECK(s.hint[0]);
  CHECK(applyHubLine(s, "HINT:") == HC_APPLIED); CHECK(!s.hint[0]);

  CHECK(applyHubLine(s, "BLINK") == HC_BLINK);
  CHECK(applyHubLine(s, "PING") == HC_PING);
  CHECK(applyHubLine(s, "DOFF:1") == HC_APPLIED); CHECK(s.doffAllowed);
  CHECK(applyHubLine(s, "DOFF:0") == HC_APPLIED); CHECK(!s.doffAllowed);
  CHECK(applyHubLine(s, "DOFF:2") == HC_IGNORED);
  CHECK(applyHubLine(s, "DISP:0") == HC_IGNORED);

  // node-side indicators: only dirty the status area, only on change
  s.dirty = 0; uiSetNoHub(s, true);      CHECK(s.noHub && s.dirty == DR_STATUS);
  s.dirty = 0; uiSetNoHub(s, true);      CHECK(s.dirty == 0);
  s.dirty = 0; uiSetAccMissing(s, true); CHECK(s.accMissing && s.dirty == DR_STATUS);
  s.dirty = 0; uiSetBatLow(s, true);     CHECK(s.batLow && s.dirty == DR_STATUS);
  // disconnect reset: hub state to defaults, noHub cleared, ACC/BAT kept (they're about the node)
  applyHubLine(s, "DOFF:1"); applyHubLine(s, "HINT:x"); applyHubLine(s, "SEQ:H3");
  uiStateReset(s);
  CHECK(!s.doffAllowed); CHECK(!s.hint[0]); CHECK(s.seqKind == SEQK_NONE); CHECK(!s.noHub);
  CHECK(s.accMissing && s.batLow);

  // junk never crashes
  CHECK(applyHubLine(s, "") == HC_IGNORED);
  CHECK(applyHubLine(s, "FOO:BAR") == HC_IGNORED);
  CHECK(applyHubLine(s, "PINGX") == HC_IGNORED);
  CHECK(applyHubLine(s, "\xff\xfe") == HC_IGNORED);

  char b[24];
  fmtHello(b, sizeof(b), 0x3F2A, FW_VERSION); CHECK_EQ_STR(b, "HELLO:3F2A:" FW_VERSION); CHECK(strlen(b) + 1 <= 20);
  fmtButtons(b, sizeof(b), 3); CHECK_EQ_STR(b, "B:3");
  fmtButtons(b, sizeof(b), 0xF); CHECK_EQ_STR(b, "B:F");
  fmtBattery(b, sizeof(b), 87); CHECK_EQ_STR(b, "BAT:87");
  fmtBattery(b, sizeof(b), 100); CHECK_EQ_STR(b, "BAT:100");
  fmtFaceSync(b, sizeof(b), true);  CHECK_EQ_STR(b, "FS:1");      // v1.5 FW-Q33: silent face-state sync
  fmtFaceSync(b, sizeof(b), false); CHECK_EQ_STR(b, "FS:0");
  CHECK(lipoPercent(3.0f) == 0); CHECK(lipoPercent(4.3f) == 100);
  { bool mono = true; uint8_t prev = 0; for (float v = 3.3f; v <= 4.3f; v += 0.01f) { uint8_t p = lipoPercent(v); if (p < prev) mono = false; prev = p; } CHECK(mono); }
  CHECK(lipoPercent(3.80f) == 45);
}

static std::vector<std::string> g_lines;
static void collect(const char* l, void*) { g_lines.push_back(l); }

static void testFraming() {
  printf("framing\n");
  LineAssembler a;
  g_lines.clear();
  const char* w1 = "CARD:UUR\nSEQ:H\r\nREG:00";
  a.feed((const uint8_t*)w1, strlen(w1), collect, nullptr);
  CHECK(g_lines.size() == 2);
  const char* w2 = "10\n";
  a.feed((const uint8_t*)w2, strlen(w2), collect, nullptr);
  a.endOfWrite(collect, nullptr);
  CHECK(g_lines.size() == 3 && g_lines[2] == "REG:0010");
  // no newline at the end of a write: the write ends the message (brief section 5)
  const char* w3 = "PING";
  a.feed((const uint8_t*)w3, strlen(w3), collect, nullptr);
  CHECK(g_lines.size() == 3);
  a.endOfWrite(collect, nullptr);
  CHECK(g_lines.size() == 4 && g_lines[3] == "PING");
  // over-long line dropped, next line fine
  std::string junk(100, 'X'); junk += "\nPING\n";
  a.feed((const uint8_t*)junk.data(), junk.size(), collect, nullptr);
  CHECK(g_lines.size() == 5 && g_lines[4] == "PING");
  CHECK(a.droppedLines() == 1);
  // empty lines ignored
  a.feed((const uint8_t*)"\n\n\r\n", 4, collect, nullptr);
  CHECK(g_lines.size() == 5);
}

// =============================================================================
// Card layout
// =============================================================================
static void testCardLayout() {
  printf("card layout\n");
  CardLayout L;
  CHECK(computeCardLayout("UUR", L));
  CHECK(L.nSteps == 3 && L.gridW == 2 && L.gridH == 3);
  CHECK(L.startX == 0 && L.startY == 2);
  CHECK(L.cellX[0] == 0 && L.cellY[0] == 1);
  CHECK(L.cellX[2] == 1 && L.cellY[2] == 0);
  CHECK(L.segment[0] == 0 && L.segment[1] == 0 && L.segment[2] == 1);
  CHECK(computeCardLayout("LLLL", L) && L.gridW == 5 && L.gridH == 1 && L.startX == 4);
  CHECK(computeCardLayout("URDL", L) && L.gridW == 2 && L.gridH == 2 && L.segment[3] == 3);
  CHECK(computeCardLayout("UD", L) && L.cellX[1] == L.startX && L.cellY[1] == L.startY);  // reversal returns to start
  CHECK(!computeCardLayout("", L));
  CHECK(!computeCardLayout("UUUUU", L));
  CHECK(!computeCardLayout("UQ", L));
  // all 4^1..4^4 cards fit in 5x5
  const char d[4] = { 'U', 'D', 'L', 'R' };
  int count = 0; bool fits = true;
  for (int n = 1; n <= 4; n++) {
    int total = 1; for (int i = 0; i < n; i++) total *= 4;
    for (int k = 0; k < total; k++) {
      char c[5]; int v = k;
      for (int i = 0; i < n; i++) { c[i] = d[v % 4]; v /= 4; }
      c[n] = 0;
      if (!computeCardLayout(c, L) || L.gridW > 5 || L.gridH > 5) fits = false;
      count++;
    }
  }
  CHECK(fits); CHECK(count == 340);
}

// =============================================================================
// Buttons, node ID
// =============================================================================
static void testButtons() {
  printf("buttons\n");
  // TinyScreen: LL=bit0, UL=bit1, UR=bit2, LR=bit3 -> protocol TL,TR,BL,BR
  CHECK(tsButtonsToProtocol(1 << 1) == 0x1);
  CHECK(tsButtonsToProtocol(1 << 2) == 0x2);
  CHECK(tsButtonsToProtocol(1 << 0) == 0x4);
  CHECK(tsButtonsToProtocol(1 << 3) == 0x8);
  CHECK(tsButtonsToProtocol((1 << 1) | (1 << 2)) == 0x3);   // B:3 = both top
  ButtonDebouncer b(20);
  b.begin(0, 0);
  CHECK(!b.update(1, 0));
  CHECK(!b.update(0, 5));     // bounce
  CHECK(!b.update(1, 6));
  CHECK(!b.update(1, 25));
  CHECK(b.update(1, 26) && b.state() == 1);
  CHECK(!b.update(1, 100));
  CHECK(!b.update(0, 200));
  CHECK(b.update(0, 220) && b.state() == 0);
}

static void testNodeId() {
  printf("node id\n");
  uint32_t a[4] = { 0x12345678, 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6978 };
  uint32_t b[4] = { 0x12345678, 0x9ABCDEF0, 0x0F1E2D3C, 0x4B5A6979 };
  CHECK(nodeIdFromSerial(a) == nodeIdFromSerial(a));
  CHECK(nodeIdFromSerial(a) != nodeIdFromSerial(b));
  // collision rate for 6 random nodes, many trials
  std::mt19937 rng(1);
  int collisions = 0, trials = 20000;
  for (int t = 0; t < trials; t++) {
    uint16_t ids[6];
    for (int i = 0; i < 6; i++) { uint32_t s[4] = { (uint32_t)rng(), (uint32_t)rng(), (uint32_t)rng(), (uint32_t)rng() }; ids[i] = nodeIdFromSerial(s); }
    bool c = false;
    for (int i = 0; i < 6; i++) for (int j = i + 1; j < 6; j++) if (ids[i] == ids[j]) c = true;
    collisions += c;
  }
  printf("  6-node ID collision rate: %.4f%% (expected ~0.023%%)\n", 100.0 * collisions / trials);
  // v1.5 FW-Q31: static random address = 11xxxxxx..., derived from the ID, stable, unique per ID
  uint8_t ad[6], ad2[6]; char as[18];
  bleStaticAddress(0x3F2A, ad); bleAddressString(ad, as);
  CHECK_EQ_STR(as, "C0:CC:00:00:3F:2A");
  CHECK((ad[5] & 0xC0) == 0xC0);
  bleStaticAddress(0x3F2A, ad2); CHECK(memcmp(ad, ad2, 6) == 0);
  bleStaticAddress(0x3F2B, ad2); CHECK(memcmp(ad, ad2, 6) != 0);
  bleStaticAddress(0x0000, ad2); { bool allZero = true; for (int i = 0; i < 5; i++) if (ad2[i]) allZero = false; CHECK(!(allZero && (ad2[5] & 0x3F) == 0)); }
  bleStaticAddress(0xFFFF, ad2); { bool allOne = (ad2[5] & 0x3F) == 0x3F; for (int i = 0; i < 5; i++) if (ad2[i] != 0xFF) allOne = false; CHECK(!allOne); }
  CHECK(collisions < trials / 500);
}

// =============================================================================
// Gestures: synthetic accelerometer traces at 1 kHz
// =============================================================================
struct Sim {
  GestureEngine eng;
  uint32_t t = 0;
  std::mt19937 rng{42};
  std::normal_distribution<float> noise{0.0f, 0.02f};   // 20 mg RMS sensor noise
  std::vector<std::pair<uint32_t, uint8_t>> events;
  float noiseScale = 1.0f;
  GestureConfig cfg;
  bool raw = false;                 // true: feed raw sensor values (calibration test)
  explicit Sim(const GestureConfig& c = defaultGestureConfig()) : eng(c), cfg(c) {}
  static int16_t cnt(float g) {
    float c = g / G_PER_COUNT;
    if (c > 2047) c = 2047; if (c < -2048) c = -2048;   // +-8 g saturation
    return (int16_t)lrintf(c);
  }
  // x, y, z in the NODE frame (x+ = towards a T:R spike, z+ = face-up gravity); converted to raw
  // sensor values through the calibration signs, so the tests hold for any CAL_*_SIGN.
  void sample(float x, float y, float z) {
    if (!raw) { x *= cfg.xSign; z *= cfg.zSign; if (cfg.sideAxis == 1) std::swap(x, y); }
    uint8_t ev = eng.update(cnt(x + noise(rng) * noiseScale), cnt(y + noise(rng) * noiseScale), cnt(z + noise(rng) * noiseScale), t);
    if (ev) events.push_back({ t, ev });
    t++;
  }
  void still(uint32_t ms, float z = 1.0f) { for (uint32_t i = 0; i < ms; i++) sample(0, 0, z); }
  // Rotate gravity about the Y axis from angle a0 to a1 (radians) over ms.
  void rotate(float a0, float a1, uint32_t ms, float spikeEvery = 0) {
    for (uint32_t i = 0; i < ms; i++) {
      float a = a0 + (a1 - a0) * (float)i / ms;
      float x = sinf(a), z = cosf(a);
      float sx = 0, sz = 0;
      // handling knocks once the node is visibly tilting (> ~35 deg); knocks before
      // any tilt are indistinguishable from real taps (brief v1.1 Q9), see the pickup test
      if (spikeEvery > 0 && i > ms / 5 && i < ms * 4 / 5 && i % (uint32_t)spikeEvery == 3) { sx = 2.5f; sz = -2.0f; }
      sample(x + sx, 0, z + sz);
    }
  }
  // Damped-sine impact on one axis, starting with sign 'sign'.
  void knock(int axis, float g, int sign, float zBase = 1.0f) {
    for (int i = 0; i < 12; i++) {
      float v = sign * g * expf(-i / 2.5f) * cosf(i * 1.3f);
      float a[3] = { 0, 0, zBase };
      a[axis] += v;
      if (axis == 0) a[2] += 0.3f * fabsf(v) * ((i & 1) ? -1 : 1);   // some cross-talk
      sample(a[0], a[1], a[2]);
    }
  }
  int count(uint8_t mask) const { int n = 0; for (auto& e : events) if (e.second & mask) n++; return n; }
  void clear() { events.clear(); }
};

static GestureConfig legacyCfg() { GestureConfig c = defaultGestureConfig(); c.sideTaps = true; return c; }

static void testGestures() {
  printf("gestures\n");
  {  // stillness: no false events over 20 s, with noise
    Sim s; s.noiseScale = 2.0f;
    s.still(20000);
    CHECK(s.events.empty());
    CHECK(!s.eng.faceDown());
  }
  {  // boot face-down: state taken from the first sample, no event
    Sim s; s.still(2000, -1.0f);
    CHECK(s.eng.faceDown()); CHECK(s.events.empty());
  }
  {  // LEGACY side knocks (SIDE_TAPS_ENABLED 1): positive first spike = T:R with CAL_TAP_X_SIGN=+1
    Sim s(legacyCfg()); s.still(1000);
    s.knock(0, 3.0f, +1); s.still(400);
    s.knock(0, 3.0f, -1); s.still(400);
    CHECK(s.count(GE_TAP_R) == 1); CHECK(s.count(GE_TAP_L) == 1);
    CHECK(s.events.size() == 2);
    if (s.events.size() == 2) {
      // latency: event within ~5 ms of the spike start (1000 ms)
      printf("  side tap latency: %u ms after spike start\n", s.events[0].first - 1000);
      CHECK(s.events[0].first - 1000 <= 6);
      CHECK(s.events[0].second == GE_TAP_R);
    }
  }
  {  // vertical taps, face-up and face-down
    Sim s; s.still(1000);
    s.knock(2, 4.0f, +1); s.still(500);
    CHECK(s.count(GE_TAP_V) == 1);
    Sim d; d.still(1000, -1.0f);
    d.knock(2, 4.0f, -1, -1.0f); d.still(500);
    CHECK(d.count(GE_TAP_V) == 1); CHECK(d.events.size() == 1);
  }
  {  // a V tap made by lifting ~1 cm and dropping: 45 ms near free-fall, then impact
    Sim s; s.still(1000);
    for (int i = 0; i < 45; i++) s.sample(0, 0, 0.05f);
    s.knock(2, 5.0f, +1); s.still(600);
    printf("  drop-tap: zAvg dipped, events=%zu\n", s.events.size());
    CHECK(s.count(GE_TAP_V) == 1);
    CHECK(s.count(GE_FLIP_DOWN | GE_FLIP_UP) == 0);
  }
  {  // refractory (legacy side taps): two knocks 100 ms apart -> one event; 200 ms apart -> two
    Sim s(legacyCfg()); s.still(1000);
    s.knock(0, 3.0f, +1); s.still(88); s.knock(0, 3.0f, +1); s.still(400);
    CHECK(s.count(GE_TAP_R) == 1);
    s.clear();
    s.knock(0, 3.0f, +1); s.still(188); s.knock(0, 3.0f, +1); s.still(400);
    CHECK(s.count(GE_TAP_R) == 2);
  }
  {  // below threshold and other-axis knocks give nothing
    Sim s(legacyCfg()); s.still(1000);
    s.knock(0, 1.2f, +1); s.still(300);
    s.knock(1, 4.0f, +1); s.still(300);    // knock on the top/bottom edge
    CHECK(s.events.empty());
  }
  {  // saturated knock (> 8 g) still classified (legacy side taps)
    Sim s(legacyCfg()); s.still(1000);
    s.knock(0, 12.0f, -1); s.still(300);
    CHECK(s.count(GE_TAP_L) == 1);
  }
  {  // flip face-down with handling knocks: exactly one F:1, no taps
    Sim s; s.still(1000);
    s.rotate(0, (float)M_PI, 400, 37);
    s.still(1500, -1.0f);
    CHECK(s.count(GE_FLIP_DOWN) == 1);
    CHECK(s.count(GE_TAP_R | GE_TAP_L | GE_TAP_V) == 0);
    CHECK(s.eng.faceDown());
    if (s.count(GE_FLIP_DOWN) == 1) {
      uint32_t tf = 0; for (auto& e : s.events) if (e.second & GE_FLIP_DOWN) tf = e.first;
      printf("  flip commit %u ms after rotation start\n", tf - 1000);
      CHECK(tf - 1000 >= 500 && tf - 1000 <= 1100);
    }
    // a knock right after settling face-down is still suppressed (< 300 ms after commit)...
    // ...but a tap 600 ms after commit is reported
    s.clear();
    s.still(200, -1.0f);
    s.knock(2, 4.0f, -1, -1.0f); s.still(400, -1.0f);
    CHECK(s.count(GE_TAP_V) == 1);
    // flip back up
    s.clear();
    s.rotate((float)M_PI, 0, 400, 41);
    s.still(1500);
    CHECK(s.count(GE_FLIP_UP) == 1); CHECK(s.events.size() == 1);
  }
  {  // KNOWN LIMITATION (brief v1.1 Q9): a knock while lifting, before any tilt, is reported
    Sim s; s.still(1000);
    s.knock(2, 3.0f, +1);
    s.rotate(0, (float)M_PI, 400); s.still(1500, -1.0f);
    printf("  pickup knock before tilt -> %d tap event(s) (expected 1, documented)\n", s.count(GE_TAP_R | GE_TAP_L | GE_TAP_V));
    CHECK(s.count(GE_FLIP_DOWN) == 1);
  }
  {  // v1.1/Q9: pick up and flip 20 times WITHOUT tapping -> 0 T:V (README test).
     // Pickup = a fast vertical push (0.8-3 g, 40-150 ms), grip jitter, rotation, set-down impact.
    Sim s; s.still(1000);
    std::mt19937 r(123);
    std::uniform_real_distribution<float> amp(0.8f, 3.0f), u(0, 1);
    int flipsExpected = 0;
    for (int n = 0; n < 20; n++) {
      float z0 = s.eng.faceDown() ? -1.0f : 1.0f;
      int dur = 40 + (int)(u(r) * 110);
      float A = amp(r);
      for (int i = 0; i < dur; i++) {                      // lift: smooth half-sine push "up" (away from table)
        float push = A * sinf((float)M_PI * i / dur);
        float jit = (i % 17 == 5) ? 0.4f * (u(r) - 0.5f) : 0;
        s.sample(jit, jit, z0 + push * (z0 > 0 ? 1 : -1));
      }
      float a0 = z0 > 0 ? 0 : (float)M_PI, a1 = z0 > 0 ? (float)M_PI : 0;
      s.rotate(a0, a1, 250 + (int)(u(r) * 350), 31);      // flip, with handling knocks while tilted
      float z1 = -z0;
      for (int i = 0; i < 30; i++) s.sample(0, 0, z1 * 0.6f);   // lowering
      s.knock(2, 3.5f, z1 > 0 ? 1 : -1, z1);               // set-down impact
      s.still(1200, z1);
      flipsExpected++;
    }
    int v = s.count(GE_TAP_V), f = s.count(GE_FLIP_DOWN | GE_FLIP_UP), sides = s.count(GE_TAP_R | GE_TAP_L);
    printf("  20 pickups+flips: T:V=%d (want 0), flips=%d/%d, side taps=%d\n", v, f, flipsExpected, sides);
    CHECK(v == 0); CHECK(f == flipsExpected); CHECK(sides == 0); CHECK(s.count(GE_TILT_L | GE_TILT_R) == 0);
    // ...and a real lift-and-tap is still detected afterwards
    s.clear(); s.still(500, s.eng.faceDown() ? -1.0f : 1.0f);
    float z = s.eng.faceDown() ? -1.0f : 1.0f;
    for (int i = 0; i < 45; i++) s.sample(0, 0, z * 0.05f);
    s.knock(2, 5.0f, z > 0 ? 1 : -1, z); s.still(500, z);
    CHECK(s.count(GE_TAP_V) == 1);
  }
  {  // a smooth fast lift alone (no flip) is not a tap; with the check disabled it would be
    Sim s; s.still(1000);
    for (int i = 0; i < 80; i++) s.sample(0, 0, 1.0f + 2.5f * sinf((float)M_PI * i / 80));
    s.still(500);
    CHECK(s.count(GE_TAP_V) == 0);
    GestureConfig c = defaultGestureConfig(); c.tapVDecay = 1.0f;
    Sim t(c); t.still(1000);
    for (int i = 0; i < 80; i++) t.sample(0, 0, 1.0f + 2.5f * sinf((float)M_PI * i / 80));
    t.still(500);
    CHECK(t.count(GE_TAP_V) == 1);
  }
  {  // a single-sided impact (no ringing) that decays quickly is still a tap
    Sim s; s.still(1000);
    const float pulse[] = { 2.0f, 4.0f, 2.5f, 1.0f, 0.4f, 0.1f };
    for (float p : pulse) s.sample(0, 0, 1.0f + p);
    s.still(500);
    CHECK(s.count(GE_TAP_V) == 1);
  }
  {  // stood on its edge / held tilted: no flip, taps suppressed while tilted
    Sim s; s.still(1000);
    s.rotate(0, (float)M_PI / 2, 300);
    for (int i = 0; i < 2000; i++) s.sample(1.0f, 0, 0.0f);
    CHECK(s.count(GE_FLIP_DOWN | GE_FLIP_UP) == 0);
    s.knock(0, 3.0f, +1, 0.0f);
    for (int i = 0; i < 500; i++) s.sample(1.0f, 0, 0.0f);
    CHECK(s.count(GE_TAP_R | GE_TAP_L | GE_TAP_V) == 0);
  }
  {  // half flip and back within 300 ms: no F event (debounce)
    Sim s; s.still(1000);
    s.rotate(0, (float)M_PI, 250);
    s.still(150, -1.0f);
    s.rotate((float)M_PI, 0, 250);
    s.still(1500);
    CHECK(s.count(GE_FLIP_DOWN | GE_FLIP_UP) == 0);
  }
  {  // time gap (FIFO overrun / stall) does not break anything
    Sim s(legacyCfg()); s.still(1000);
    s.t += 50;
    s.still(500);
    s.knock(0, 3.0f, +1); s.still(300);
    CHECK(s.count(GE_TAP_R) == 1);
  }
  {  // calibration signs: X sign flips R/L, Z sign flips the face-up reading
    GestureConfig c = legacyCfg(); c.tapXSign = -1; c.zSign = -1;
    Sim s(c); s.raw = true; s.still(1000, -1.0f);   // raw -1 g is face-up with zSign=-1
    CHECK(!s.eng.faceDown());
    s.knock(0, 3.0f, +1, -1.0f); s.still(300, -1.0f);
    CHECK(s.count(GE_TAP_L) == 1);
  }
  {  // brief-only mode (tilt detector off) still detects flips and taps
    GestureConfig c = legacyCfg(); c.fastTiltAlpha = 0;
    Sim s(c); s.still(1000);
    s.knock(0, 3.0f, +1); s.still(400);
    s.rotate(0, (float)M_PI, 400); s.still(1500, -1.0f);
    CHECK(s.count(GE_TAP_R) == 1); CHECK(s.count(GE_FLIP_DOWN) == 1);
  }
  // ===========================================================================
  // Brief v1.7: side taps off by default, tilt gesture replaces them
  // ===========================================================================
  {  // side knocks / bumps are not reported as anything (and never as tilt)
    Sim s; s.still(1000);
    for (int k = 0; k < 10; k++) { s.knock(0, 3.0f, (k & 1) ? 1 : -1); s.still(300); }
    s.knock(0, 12.0f, +1); s.still(300);
    CHECK(s.events.empty());
  }
}

// Roll / pitch pose in the node frame: x = sin(roll) (+ = right edge down),
// y = sin(pitch) cos(roll), z = cos(roll) cos(pitch) (negated face-down).
static void pose(Sim& s, float rollDeg, float pitchDeg, uint32_t ms, bool faceDown = false, float tremor = 0.0f) {
  const float d2r = (float)M_PI / 180.0f;
  for (uint32_t i = 0; i < ms; i++) {
    float r = rollDeg * d2r, p = pitchDeg * d2r;
    float tx = tremor * sinf(s.t * 0.031f), ty = tremor * sinf(s.t * 0.017f + 1);
    s.sample(sinf(r) + tx, sinf(p) * cosf(r) + ty, (faceDown ? -1.0f : 1.0f) * cosf(r) * cosf(p));
  }
}
static void sweep(Sim& s, float r0, float r1, float p0, float p1, uint32_t ms, bool faceDown = false) {
  for (uint32_t i = 0; i < ms; i++) {
    float f = (float)i / ms;
    pose(s, r0 + (r1 - r0) * f, p0 + (p1 - p0) * f, 1, faceDown);
  }
}
// A deliberate tilt: roll over 150-250 ms, hold, roll back to level.
static void tiltGesture(Sim& s, float deg, uint32_t holdMs, bool faceDown = false, float tremor = 0.0f) {
  sweep(s, 0, deg, 0, 0, 200, faceDown);
  pose(s, deg, 0, holdMs, faceDown, tremor);
  sweep(s, deg, 0, 0, 0, 200, faceDown);
  pose(s, 0, 0, 600, faceDown);
}

static void testTilt() {
  printf("tilt gesture (v1.7)\n");
  const uint8_t TILT = GE_TILT_L | GE_TILT_R;
  {  // clean tilts both ways, x10 each, angles 30..60, holds 350..900 ms, light hand tremor
    Sim s; pose(s, 0, 0, 1000);
    std::mt19937 r(5);
    for (int k = 0; k < 10; k++) tiltGesture(s, -(30.0f + (r() % 31)), 350 + r() % 550, false, 0.01f);
    int L = s.count(GE_TILT_L), R = s.count(GE_TILT_R);
    for (int k = 0; k < 10; k++) tiltGesture(s, 30.0f + (r() % 31), 350 + r() % 550, false, 0.01f);
    printf("  10x left -> T:L=%d T:R=%d; 10x right -> T:R=%d\n", L, R, s.count(GE_TILT_R));
    CHECK(L == 10); CHECK(R == 0); CHECK(s.count(GE_TILT_R) == 10); CHECK(s.count(GE_TILT_L) == 10);
    CHECK(s.count(GE_TAP_V | GE_FLIP_DOWN | GE_FLIP_UP) == 0);
    // the event comes when the node is back at level, and the arrow showed while armed
    Sim a; pose(a, 0, 0, 1000);
    sweep(a, 0, 40, 0, 0, 200); pose(a, 40, 0, 400);
    CHECK(a.eng.tiltArmedDir() == 1); CHECK(a.count(TILT) == 0);
    sweep(a, 40, 0, 0, 0, 200); pose(a, 0, 0, 300);
    CHECK(a.count(GE_TILT_R) == 1); CHECK(a.eng.tiltArmedDir() == 0);
  }
  {  // A small wrist roll in either direction, with a slight forward lean.
    Sim s; pose(s, 0, 0, 1000);
    for (float d : {-20.0f, 20.0f}) {
      sweep(s, 0, d, 0, 8, 100); pose(s, d, 8, 220, false, 0.008f);
      CHECK(s.eng.tiltArmedDir() == (d < 0 ? -1 : 1));
      CHECK(s.count(TILT) == 0);
      sweep(s, d, 0, 8, 0, 100);
      s.knock(2, 4.0f, 1);  // the raised edge landing is not a vertical tap
      pose(s, 0, 0, 350);
      CHECK(s.count(d < 0 ? GE_TILT_L : GE_TILT_R) == 1);
      CHECK(s.count(GE_TAP_V | GE_FLIP_DOWN | GE_FLIP_UP) == 0);
      s.clear();
    }
  }
  {  // A 15-degree lean or a fast pass through 40 degrees does not register.
    Sim s; pose(s, 0, 0, 1000);
    tiltGesture(s, 15, 800); tiltGesture(s, -15, 800);
    sweep(s, 0, 40, 0, 0, 60); pose(s, 40, 0, 50); sweep(s, 40, 0, 0, 0, 60); pose(s, 0, 0, 600);
    CHECK(s.count(TILT) == 0);
  }
  {  // too steep (80 deg) cancels; coming back down through 65..25 must not re-arm
    Sim s; pose(s, 0, 0, 1000);
    sweep(s, 0, 40, 0, 0, 150); pose(s, 40, 0, 400);           // armed
    sweep(s, 40, 80, 0, 0, 200); pose(s, 80, 0, 300);          // past max -> cancel
    sweep(s, 80, 40, 0, 0, 200); pose(s, 40, 0, 600);          // back in range, held
    sweep(s, 40, 0, 0, 0, 200); pose(s, 0, 0, 600);
    CHECK(s.count(TILT) == 0);
    tiltGesture(s, 40, 500);                                   // a normal tilt afterwards still works
    CHECK(s.count(GE_TILT_R) == 1);
  }
  {  // a flip about the left/right axis passes through the tilt range: no tilt, one F:1
    Sim s; pose(s, 0, 0, 1000);
    sweep(s, 0, 180, 0, 0, 500); pose(s, 180, 0, 1500);
    CHECK(s.count(TILT) == 0); CHECK(s.count(GE_FLIP_DOWN) == 1);
    // ...and a slow flip that pauses at 45 deg for 1 s on the way
    Sim t; pose(t, 0, 0, 1000);
    sweep(t, 0, 45, 0, 0, 200); pose(t, 45, 0, 1000); sweep(t, 45, 180, 0, 0, 400); pose(t, 180, 0, 1500);
    sweep(t, 180, 0, 0, 0, 500); pose(t, 0, 0, 1500);           // and back up
    CHECK(t.count(TILT) == 0); CHECK(t.count(GE_FLIP_DOWN) == 1); CHECK(t.count(GE_FLIP_UP) == 1);
  }
  {  // no return to level within 3 s: cancelled, nothing when it finally levels
    Sim s; pose(s, 0, 0, 1000);
    sweep(s, 0, -40, 0, 0, 200); pose(s, -40, 0, 3600); sweep(s, -40, 0, 0, 0, 200); pose(s, 0, 0, 600);
    CHECK(s.count(TILT) == 0);
  }
  {  // tilts while face-down are ignored
    Sim s; pose(s, 0, 0, 200, true); pose(s, 0, 0, 1500, true);
    CHECK(s.eng.faceDown());
    for (int k = 0; k < 4; k++) tiltGesture(s, (k & 1) ? 40.0f : -40.0f, 600, true);
    CHECK(s.count(TILT) == 0);
  }
  {  // a forward/back tilt (pitch) is not a roll: nothing
    Sim s; pose(s, 0, 0, 1000);
    for (int i = 0; i < 200; i++) pose(s, 0, 40.0f * i / 200, 1);
    pose(s, 0, 40, 600);
    for (int i = 0; i < 200; i++) pose(s, 0, 40.0f - 40.0f * i / 200, 1);
    pose(s, 0, 0, 600);
    CHECK(s.count(TILT) == 0);
  }
  {  // checklist: pick up and put down normally x10 (no flip): 0 tilt events
     // in-hand: changing roll AND pitch, translation accelerations, then lowered and set down
    Sim s; pose(s, 0, 0, 1000);
    std::mt19937 r(11);
    std::uniform_real_distribution<float> u(0, 1);
    int tilts = 0, vs = 0;
    for (int n = 0; n < 10; n++) {
      s.clear();
      for (int i = 0; i < 80; i++) s.sample(0, 0, 1.0f + 0.8f * sinf((float)M_PI * i / 80));   // lift
      float ra = 20 + 25 * u(r), pa = 15 + 25 * u(r), per = 500 + 500 * u(r);
      int hold = 800 + (int)(1200 * u(r));
      const float d2r = (float)M_PI / 180.0f;
      for (int i = 0; i < hold; i++) {                                     // looking at it, moving it
        float ph = 2 * (float)M_PI * i / per;
        float roll = ra * sinf(ph), pitch = pa * sinf(ph * 0.7f + 1.0f);
        float mv = 0.25f * sinf(ph * 1.9f);                                // hand translation
        float rr = roll * d2r, pp = pitch * d2r;
        s.sample(sinf(rr) + 0.5f * mv, sinf(pp) * cosf(rr), cosf(rr) * cosf(pp) + mv);
      }
      for (int i = 0; i < 120; i++) s.sample(0, 0, 0.75f);                 // lowering
      s.knock(2, 3.0f, 1);                                                  // set down
      pose(s, 0, 0, 1500);
      tilts += s.count(TILT); vs += s.count(GE_TAP_V);
    }
    printf("  10x pick up + put down: tilt events=%d (want 0), T:V=%d\n", tilts, vs);
    CHECK(tilts == 0);
  }
  {  // FW-Q36: picked up, held STILL at a pure 40 deg roll for 1 s, levelled in the hand, then lowered
     // and set down -- sample for sample the gesture until it is lowered. The default settle (150 ms,
     // level and still) rejects it; settle 0 (the original brief rule) does not.
    auto run = [](GestureConfig c, float lowerG = 0.75f) {
      Sim s(c); pose(s, 0, 0, 1000);
      for (int n = 0; n < 10; n++) {
        for (int i = 0; i < 80; i++) s.sample(0, 0, 1.0f + 0.8f * sinf((float)M_PI * i / 80));
        sweep(s, 0, 40, 0, 0, 300); pose(s, 40, 0, 1000); sweep(s, 40, 0, 0, 0, 300);
        for (int i = 0; i < 120; i++) s.sample(0, 0, lowerG);
        s.knock(2, 3.0f, 1); pose(s, 0, 0, 1500);
      }
      return s.count(GE_TILT_L | GE_TILT_R);
    };
    GestureConfig settle = defaultGestureConfig();
    GestureConfig none = defaultGestureConfig(); none.tiltSettleMs = 0;
    CHECK(settle.tiltSettleMs == 150);
    int a = run(none), b = run(settle), c = run(settle, 0.88f);
    printf("  10x held-still roll pickups: settle 0 -> %d tilts, settle 150 -> %d (gentle lowering: %d)\n", a, b, c);
    CHECK(a == 10); CHECK(b == 0); CHECK(c == 0);
    Sim t(settle); pose(t, 0, 0, 1000);                     // settle still passes real tilts
    for (int k = 0; k < 10; k++) tiltGesture(t, (k & 1) ? 40.0f : -40.0f, 500, false, 0.01f);
    CHECK(t.count(GE_TILT_L) == 5); CHECK(t.count(GE_TILT_R) == 5);
  }
  {  // real tilt by lifting one edge: the edge lands back on the table with a knock (1..12 g, saturating)
     // just as the node reaches level. Must still count, with and without the settle.
    for (float g : { 1.0f, 4.0f, 12.0f }) for (int st : { 150, 0 }) {
      GestureConfig c = defaultGestureConfig(); c.tiltSettleMs = (uint16_t)st;
      Sim s(c); pose(s, 0, 0, 1000);
      for (int k = 0; k < 10; k++) {
        float d = (k & 1) ? 40.0f : -40.0f;
        sweep(s, 0, d, 0, 0, 200); pose(s, d, 0, 500); sweep(s, d, 0, 0, 0, 150 + 20 * k);
        s.knock(2, g, 1); s.knock(0, g * 0.3f, d > 0 ? 1 : -1);
        pose(s, 0, 0, 800);
      }
      if (s.count(GE_TILT_L) != 5 || s.count(GE_TILT_R) != 5) printf("  edge landing %.0f g settle %d: L=%d R=%d\n", g, st, s.count(GE_TILT_L), s.count(GE_TILT_R));
      CHECK(s.count(GE_TILT_L) == 5); CHECK(s.count(GE_TILT_R) == 5);
    }
    // latency with the default settle: the event comes ~150 ms after reaching level
    Sim s; pose(s, 0, 0, 1000);
    sweep(s, 0, 45, 0, 0, 200); pose(s, 45, 0, 400); sweep(s, 45, 0, 0, 0, 200);
    uint32_t tl = s.t; pose(s, 0, 0, 600);
    CHECK(s.events.size() == 1);
    if (s.events.size() == 1) {
      int lat = (int)s.events[0].first - (int)tl;
      printf("  T:R %d ms after reaching level (settle 150)\n", lat);
      CHECK(lat >= 120 && lat <= 220);
    }
  }
  {  // FW-Q37 A: tilt then V. A T:V shortly after the T:L/T:R is dropped; >= 300 ms after it counts
    int firstOk = -1;
    for (int gap = 0; gap <= 400; gap += 25) {
      Sim s; pose(s, 0, 0, 1000);
      sweep(s, 0, -40, 0, 0, 200); pose(s, -40, 0, 500); sweep(s, -40, 0, 0, 0, 200);
      while (s.count(GE_TILT_L) == 0 && s.t < 5000) pose(s, 0, 0, 1);
      pose(s, 0, 0, gap); s.knock(2, 4.0f, 1); pose(s, 0, 0, 400);
      if (s.count(GE_TAP_V) == 1 && firstOk < 0) firstOk = gap;
      if (gap >= 300) CHECK(s.count(GE_TAP_V) == 1);
    }
    printf("  tilt then V: V counts from %d ms after the T:L\n", firstOk);
  }
  {  // a slightly impure roll (10 deg of pitch while tilting) still counts
    Sim s; pose(s, 0, 0, 1000);
    sweep(s, 0, -40, 0, 10, 200); pose(s, -40, 10, 500); sweep(s, -40, 0, 10, 0, 200); pose(s, 0, 0, 600);
    CHECK(s.count(GE_TILT_L) == 1);
  }
  {  // the 20x pickup-and-flip run (calibration step 10) gives no tilt events either
    Sim s; pose(s, 0, 0, 1000);
    for (int n = 0; n < 20; n++) {
      bool down = s.eng.faceDown();
      for (int i = 0; i < 70; i++) s.sample(0, 0, (down ? -1.0f : 1.0f) * (1.0f + 1.5f * sinf((float)M_PI * i / 70)));
      sweep(s, down ? 180 : 0, down ? 0 : 180, 0, 0, 300 + n * 13);
      pose(s, down ? 0 : 180, 0, 1300);
    }
    CHECK(s.count(TILT) == 0); CHECK(s.count(GE_FLIP_DOWN | GE_FLIP_UP) == 20);
  }
  {  // CAL_X_SIGN flips the tilt direction
    GestureConfig c = defaultGestureConfig(); c.xSign = (int8_t)-c.xSign;
    Sim s(c); s.cfg = defaultGestureConfig();   // generate with the default sign, decode with the flipped one
    pose(s, 0, 0, 1000);
    tiltGesture(s, 40, 500);
    CHECK(s.count(GE_TILT_L) == 1); CHECK(s.count(GE_TILT_R) == 0);
  }
  {  // tilt notes for the debug log: candidate, armed, commit with the angle
    Sim s; pose(s, 0, 0, 1000);
    while (true) { TiltNote n; if (!s.eng.takeTiltNote(n)) break; }
    tiltGesture(s, -45, 500);
    TiltNote n; int kinds[5] = { 0 }; int armedDeg = 0;
    while (s.eng.takeTiltNote(n)) { kinds[n.kind]++; if (n.kind == TN_ARMED) armedDeg = n.deg; CHECK(n.dir == -1); }
    CHECK(kinds[TN_CANDIDATE] == 1 && kinds[TN_ARMED] == 1 && kinds[TN_COMMIT] == 1 && kinds[TN_CANCEL] == 0);
    printf("  armed at %d deg\n", armedDeg);
    CHECK(armedDeg >= 40 && armedDeg <= 46);
  }
  {  // V-tap counters (v1.7 s.3): accepted, push, suppressed
    Sim s; pose(s, 0, 0, 1000);
    for (int k = 0; k < 10; k++) { s.knock(2, 4.0f, +1); pose(s, 0, 0, 400); }             // 10 real taps
    for (int i = 0; i < 80; i++) s.sample(0, 0, 1.0f + 2.5f * sinf((float)M_PI * i / 80)); // a fast lift (push)
    pose(s, 0, 0, 500);
    sweep(s, 0, 40, 0, 0, 150); pose(s, 40, 0, 100); s.knock(2, 4.0f, 1, cosf(40 * (float)M_PI / 180)); pose(s, 40, 0, 300);
    sweep(s, 40, 0, 0, 0, 150); pose(s, 0, 0, 600);                                      // tap while tilted: suppressed
    const VStats& v = s.eng.vStats();
    printf("  V cand=%u ok=%u supp=%u push=%u axis=%u\n", v.candidates, v.accepted, v.suppressed, v.push, v.axis);
    CHECK(v.accepted == 10); CHECK(s.count(GE_TAP_V) == 10);
    CHECK(v.push == 1); CHECK(v.suppressed == 1);
    CHECK(v.candidates == v.accepted + v.suppressed + v.push + v.axis);
    s.eng.resetVStats(); CHECK(s.eng.vStats().candidates == 0);
  }
}

// =============================================================================
// Soft I2C + accelerometer driver against a simulated bus / BMA253 (v1.4 A4)
// =============================================================================
SimBus* g_bus = nullptr;
typedef SoftI2C<SimIO, 1000> TestI2C;
static TestI2C* g_i2c = nullptr;
static bool tWrite(uint8_t a, uint8_t r, uint8_t v) { return g_i2c->writeReg(a, r, v); }
static bool tRead(uint8_t a, uint8_t r, uint8_t* b, uint8_t n) { return g_i2c->readRegs(a, r, b, n); }
static void tRecover() { g_i2c->recover(); }

static void testSoftI2C() {
  printf("soft I2C (simulated bus)\n");
  SimBus bus; g_bus = &bus;
  TestI2C i2c; i2c.begin();
  uint8_t v = 0;
  CHECK(i2c.readRegs(0x18, 0x00, &v, 1)); CHECK(v == 0xFA);                       // chip ID
  CHECK(i2c.writeReg(0x18, 0x0F, 0x08)); CHECK(bus.dev.regs[0x0F] == 0x08);
  bus.dev.regs[0x10] = 0x11; bus.dev.regs[0x11] = 0x22; bus.dev.regs[0x12] = 0x33;
  uint8_t three[3] = { 0 };
  CHECK(i2c.readRegs(0x18, 0x10, three, 3)); CHECK(three[0] == 0x11 && three[1] == 0x22 && three[2] == 0x33);   // auto-increment
  bus.dev.pushFrame(100, -200, 1000); bus.dev.pushFrame(1, 2, 3);
  uint8_t f[12];
  CHECK(i2c.readRegs(0x18, 0x3F, f, 12));                                          // FIFO register doesn't increment
  CHECK((int16_t)((f[1] << 8) | f[0]) >> 4 == 100 && (int16_t)((f[3] << 8) | f[2]) >> 4 == -200);
  CHECK((int16_t)((f[11] << 8) | f[10]) >> 4 == 3);
  CHECK(!i2c.readRegs(0x19, 0x00, &v, 1)); CHECK(i2c.nacks >= 1);                  // wrong address: NACK, no hang
  CHECK(bus.sda() && bus.scl());                                                   // bus left idle

  // device unplugged: every call fails fast and leaves the bus idle
  bus.present = false;
  uint32_t t0 = bus.us;
  for (int k = 0; k < 10; k++) CHECK(!i2c.readRegs(0x18, 0x0E, &v, 1));
  printf("  10 transfers to a missing device: %u simulated us (bounded)\n", bus.us - t0);
  CHECK(bus.us - t0 < 10 * 200);
  CHECK(bus.sda() && bus.scl());
  bus.present = true;
  CHECK(i2c.readRegs(0x18, 0x00, &v, 1) && v == 0xFA);

  // slave holding SDA low (lost sync mid-byte): recovery clocks it free, next transfer works
  bus.holdSdaClocks = 5;
  CHECK(!bus.sda());
  bool ok = i2c.readRegs(0x18, 0x00, &v, 1);
  printf("  stuck SDA: recoveries=%u, first transfer %s, then ", i2c.recoveries, ok ? "ok" : "failed");
  ok = i2c.readRegs(0x18, 0x00, &v, 1);
  printf("%s\n", ok ? "ok" : "FAILED");
  CHECK(ok && v == 0xFA); CHECK(i2c.recoveries >= 1);

  // SCL held low forever (shorted / stuck): the transfer times out instead of hanging
  bus.sclStuck = true;
  t0 = bus.us;
  CHECK(!i2c.readRegs(0x18, 0x00, &v, 1));
  printf("  SCL stuck low: gave up after %u simulated us, timeouts=%u\n", bus.us - t0, i2c.timeouts);
  CHECK(bus.us - t0 < 5000);
  bus.sclStuck = false; bus.eval();                                                // line released
  CHECK(i2c.readRegs(0x18, 0x00, &v, 1) && v == 0xFA);                             // bus usable again
}

static std::vector<std::array<int16_t, 3>> g_samples;
static std::vector<uint32_t> g_sampleT;
static void accelCb(int16_t x, int16_t y, int16_t z, uint32_t t) { g_samples.push_back({ x, y, z }); g_sampleT.push_back(t); }

static void testAccelDriver() {
  printf("accel driver (simulated BMA253)\n");
  SimBus bus; g_bus = &bus;
  TestI2C i2c; i2c.begin(); g_i2c = &i2c;
  AccelIO io = { tWrite, tRead, tRecover };
  AccelDriver acc(io);
  g_samples.clear(); g_sampleT.clear();

  uint32_t ms = 1000;
  acc.begin(ms);
  acc.service(ms, accelCb);                       // probe: ID ok, soft reset issued
  CHECK(acc.state() == AccelDriver::ST_RESET_WAIT); CHECK(bus.dev.softResets == 1);
  acc.service(ms + 1, accelCb);                   // still waiting (non-blocking, no delay())
  CHECK(acc.state() == AccelDriver::ST_RESET_WAIT);
  acc.service(ms + 3, accelCb);
  CHECK(acc.ok()); CHECK(acc.takeEvents() == AccelDriver::EV_FOUND);
  CHECK(bus.dev.regs[0x0F] == 0x08 && bus.dev.regs[0x10] == 0x0E && bus.dev.regs[0x11] == 0x00 && bus.dev.regs[0x3E] == 0x80);
  ms += 3;

  // 1 kHz frames, serviced every ms: all delivered, in order, monotonic times
  int pushed = 0;
  for (int i = 0; i < 500; i++) { ms++; bus.dev.pushFrame((int16_t)i, (int16_t)-i, 256); pushed++; acc.service(ms, accelCb); }
  bool order = true, mono = true;
  for (size_t i = 0; i < g_samples.size(); i++) if (g_samples[i][0] != (int16_t)i || g_samples[i][1] != (int16_t)-i) order = false;
  for (size_t i = 1; i < g_sampleT.size(); i++) if (g_sampleT[i] <= g_sampleT[i - 1]) mono = false;
  printf("  delivered %zu of %d frames (rest still in the FIFO)\n", g_samples.size(), pushed);
  CHECK(g_samples.size() + bus.dev.fifo.size() == (size_t)pushed); CHECK(order); CHECK(mono);

  // unplug mid-run: marked missing within ACCEL_FAIL_LIMIT polls, each poll bounded, then polling stops
  bus.present = false;
  uint32_t worstUs = 0; int polls = 0;
  for (int i = 0; i < 100 && acc.ok(); i++) {
    ms += ACCEL_POLL_MS; bus.dev.pushFrame(0, 0, 256);
    uint32_t u0 = bus.us; acc.service(ms, accelCb); polls++;
    worstUs = std::max(worstUs, bus.us - u0);
  }
  printf("  unplugged: missing after %d polls, worst poll %u simulated us\n", polls, worstUs);
  CHECK(acc.state() == AccelDriver::ST_MISSING); CHECK(polls <= ACCEL_FAIL_LIMIT); CHECK(worstUs < 2000);
  CHECK(acc.takeEvents() == AccelDriver::EV_LOST);
  uint32_t edges0 = bus.edges;
  for (int i = 0; i < 4000; i++) acc.service(++ms, accelCb);                        // < 5 s: no bus traffic
  CHECK(bus.edges == edges0);

  // re-attach: found again on the next 5 s probe, re-configured, samples flow again
  bus.present = true; bus.dev.reset();
  bool back = false;
  for (int i = 0; i < 1200 && !back; i++) { ms++; acc.service(ms, accelCb); back = acc.ok(); }
  CHECK(back); CHECK(acc.takeEvents() == AccelDriver::EV_FOUND); CHECK(bus.dev.regs[0x3E] == 0x80);
  size_t before = g_samples.size();
  for (int i = 0; i < 20; i++) { ms++; bus.dev.pushFrame(1, 1, 256); acc.service(ms, accelCb); }
  CHECK(g_samples.size() > before);

  // never present at boot: MISSING quickly, probes every 5 s only
  SimBus bus2; g_bus = &bus2; bus2.present = false;
  TestI2C i2c2; i2c2.begin(); g_i2c = &i2c2;
  AccelDriver acc2(io);
  acc2.begin(0);
  acc2.service(0, accelCb);
  CHECK(acc2.state() == AccelDriver::ST_MISSING);
  for (uint32_t t = 1; t <= 20000; t++) acc2.service(t, accelCb);
  printf("  absent at boot: %u probes in 20 s\n", acc2.probes());
  CHECK(acc2.probes() >= 4 && acc2.probes() <= 6);

  g_bus = nullptr;
}

// =============================================================================
// Renderer (v1.4 layout)
// =============================================================================
static uint32_t g_fakeUs = 0;
static uint32_t fakeMicros() { return g_fakeUs += 300; }   // ~3 ops per 1 ms budget

static void writePPM(const TinyScreen& d, const char* path, int scale = 4) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 %d %d 255\n", 96 * scale, 64 * scale);
  for (int y = 0; y < 64 * scale; y++)
    for (int x = 0; x < 96 * scale; x++) {
      uint16_t c = d.fb[y / scale][x / scale];   // BGR565
      uint8_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c >> 11) << 3;
      if ((x % scale == 0 || y % scale == 0) && scale >= 4) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }
      fputc(r, f); fputc(g, f); fputc(b, f);
    }
  fclose(f);
}

static uint32_t g_now = 100000;   // ms clock for renderer tests (after the splash)

static int renderToIdle(Renderer& r, UiState& s, const StatusInfo& st, int maxSteps = 2000) {
  int steps = 0;
  bool splash = r.mode() == SM_SPLASH;   // the splash leaves hub state dirty for the real screen
  do { r.step(s, st, g_now, RENDER_BUDGET_US, fakeMicros); steps++; } while ((!r.idle() || (!splash && s.dirty)) && steps < maxSteps);
  return steps;
}

static void fullRender(UiState s, const StatusInfo& st, ScreenMode m, TinyScreen& out) {
  Renderer r(out);
  r.begin(10, false);
  r.setMode(m);
  s.dirty = DR_ALL;
  renderToIdle(r, s, st);
}

static StatusInfo defaultStatus() {
  StatusInfo st;
  st.nodeId = 0x3F2A; st.faceKnown = true; st.faceDown = false; st.zAvg = 0.98f; st.acc = ACCS_OK;
  st.bleOk = true; st.testMode = false; st.bleState = "adv-fast";
  strcpy(st.resetCause, "PWR"); strcpy(st.lastEvent, "T:R");
  return st;
}

static void shot(const char* name, std::initializer_list<const char*> cmds, ScreenMode m = SM_GAME,
                 StatusInfo st = defaultStatus(), bool noHub = false, bool acc = false, bool bat = false, int8_t tilt = 0) {
  static TinyScreen d(TinyScreenPlus);
  UiState s; uiStateInit(s);
  for (const char* c : cmds) applyHubLine(s, c);
  uiSetNoHub(s, noHub); uiSetAccMissing(s, acc); uiSetBatLow(s, bat); uiSetTiltArmed(s, tilt);
  fullRender(s, st, m, d);
  char p[128]; snprintf(p, sizeof(p), "out/%s.ppm", name);
  writePPM(d, p);
}

static bool fbEqual(const TinyScreen& a, const TinyScreen& b) { return memcmp(a.fb, b.fb, sizeof(a.fb)) == 0; }

// Pixels that differ between two frames, outside the rectangle (x0,y0,w,h).
static int diffOutside(const TinyScreen& a, const TinyScreen& b, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = 0; y < 64; y++) for (int x = 0; x < 96; x++) {
    bool inside = x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
    if (!inside && a.fb[y][x] != b.fb[y][x]) n++;
  }
  return n;
}
static bool stripEqual(const TinyScreen& a, const TinyScreen& b) {
  for (int y = 0; y < Layout::STRIP_H; y++) for (int x = 0; x < 96; x++) if (a.fb[y][x] != b.fb[y][x]) return false;
  return true;
}

static void testRenderer() {
  printf("renderer (v1.4 layout)\n");
  using namespace Layout;
  // Text fits its boxes
  CHECK(textWidth(liberationSansNarrow_10ptFontInfo, "HEAD") <= COL_W);
  CHECK(textWidth(liberationSans_12ptFontInfo, "H9") <= COL_W);
  CHECK(textWidth(liberationSans_12ptFontInfo, "#9") <= COL_W);
  CHECK(textWidth(thinPixel7_10ptFontInfo, "NO HUB") <= COL_W + 1);
  // v1.6 X1: a 14-char hint always fits the 96 px line in one of the two hint fonts, for letters,
  // digits, space and common punctuation (strings of a single repeated character, the worst case)
  {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?-:'+#/()";
    int bad = 0;
    for (const char* c = chars; *c; c++) {
      char t[15]; memset(t, *c, 14); t[14] = 0;
      if (textWidth(liberationSansNarrow_8ptFontInfo, t) > 95 && textWidth(thinPixel7_10ptFontInfo, t) > 96) { bad++; printf("  14x'%c' does not fit\n", *c); }
    }
    CHECK(bad == 0);
  }
  CHECK(textWidth(liberationSansNarrow_10ptFontInfo, "DISCONNECTED") <= 96);
  CHECK(liberationSansNarrow_8ptFontInfo.height <= STRIP_H);
  CHECK(liberationSansNarrow_8ptFontInfo.height <= INFO_H);

  // Screenshots (test/out/)
  shot("01_card_UUR_head", { "COL:E03030", "HAND:1", "SEQ:H", "REG:0010", "CARD:UUR" });
  shot("02_square_RDL_H2", { "COL:30A0FF", "HAND:4", "SEQ:H2", "REG:1101", "CARD:RDL" });
  shot("03_I4_RRR_seq3", { "COL:30D040", "HAND:2", "SEQ:3", "REG:1111", "CARD:RRR" });
  shot("04_UR_hint", { "COL:F0C000", "HAND:6", "SEQ:1", "CARD:UR", "HINT:tap R to start" });
  shot("04b_hint_14_wide", { "COL:F0C000", "HAND:6", "SEQ:1", "CARD:UR", "HINT:WWWWWWWWWWWWWW" });
  shot("04c_hint_14_caps", { "COL:F0C000", "HAND:6", "SEQ:1", "CARD:UR", "HINT:FLIP TO START!" });
  shot("05_LD", { "COL:C040F0", "HAND:5", "SEQ:2", "CARD:LD" });
  shot("06_IF_O_LLU", { "COL:E03030", "HAND:1", "SEQ:H", "REG:0100", "CARD:IF:O:LLU" });
  shot("07_IF_I4_R", { "COL:30A0FF", "HAND:3", "CARD:IF:I4:R" });
  shot("08_IF_S_UUL", { "COL:30D040", "HAND:2", "CARD:IF:S:UUL" });
  shot("09_FOR", { "COL:F0C000", "HAND:6", "SEQ:4", "CARD:FOR", "HINT:loop next card" });
  shot("10_waiting", { "COL:E03030", "HAND:0", "CARD:-" });
  shot("11_msg_lobby_node", { "COL:808080", "MSG:NODE 3F2A" });
  shot("12_msg_hint_nohub", { "COL:E03030", "REG:1000", "MSG:P1 WINS", "HINT:press any key" }, SM_GAME, defaultStatus(), true);
  shot("13_status_nohub", { "COL:E03030", "HAND:1", "SEQ:H", "REG:0110", "CARD:RRD" }, SM_GAME, defaultStatus(), true);
  shot("14_status_acc_bat", { "COL:30A0FF", "HAND:2", "SEQ:2", "CARD:UL" }, SM_GAME, defaultStatus(), false, true, true);
  shot("14b_tilt_armed_left", { "COL:30A0FF", "HAND:2", "SEQ:2", "CARD:UL" }, SM_GAME, defaultStatus(), false, false, true, -1);
  shot("14c_tilt_armed_right", { "COL:30A0FF", "HAND:2", "SEQ:2", "CARD:UL" }, SM_GAME, defaultStatus(), false, false, false, +1);
  shot("15_disconnected", {}, SM_DISCONNECTED);
  { StatusInfo st = defaultStatus(); shot("16_disconnected_acc_missing", {}, SM_DISCONNECTED, st, false, true); }
  shot("17_serial_test", {}, SM_TEST_IDLE);
  shot("18_splash", {}, SM_SPLASH);
  { StatusInfo st = defaultStatus(); st.acc = ACCS_MISSING; st.faceKnown = false; strcpy(st.resetCause, "WDT"); shot("19_splash_acc_missing_wdt", {}, SM_SPLASH, st); }
  { StatusInfo st = defaultStatus(); st.zAvg = -0.99f; st.faceDown = true; shot("20_splash_down", {}, SM_SPLASH, st); }

  StatusInfo st = defaultStatus();
  TinyScreen a(TinyScreenPlus), b(TinyScreenPlus), ref(TinyScreenPlus);

  // B1: NO HUB / ACC / BAT change only the status area, never the strip or the card
  {
    UiState s; uiStateInit(s);
    for (const char* c : { "COL:E03030", "HAND:1", "SEQ:H", "REG:1010", "CARD:RDL" }) applyHubLine(s, c);
    fullRender(s, st, SM_GAME, a);
    uiSetNoHub(s, true); fullRender(s, st, SM_GAME, b);
    CHECK(diffOutside(a, b, COL_X, STAT_Y, COL_W, STAT_H) == 0);
    uiSetNoHub(s, false); uiSetAccMissing(s, true); uiSetBatLow(s, true); fullRender(s, st, SM_GAME, b);
    CHECK(diffOutside(a, b, COL_X, STAT_Y, COL_W, STAT_H) == 0);
    // HINT changes only the hint line
    uiSetAccMissing(s, false); uiSetBatLow(s, false); applyHubLine(s, "HINT:flip to go"); fullRender(s, st, SM_GAME, b);
    CHECK(diffOutside(a, b, 0, INFO_Y, 96, INFO_H) == 0);
    // MSG leaves the strip alone
    applyHubLine(s, "MSG:P2 WINS"); fullRender(s, st, SM_GAME, b);
    CHECK(stripEqual(a, b));
  }

  // Every card the hub can send stays inside the card area and is centred; square cards keep
  // the hollow start and the arrowed last block apart (v1.4 B2).
  {
    const char d4[4] = { 'U', 'D', 'L', 'R' };
    int cards = 0, bad = 0, squares = 0, squareBad = 0;
    for (int n = 1; n <= 4; n++) {
      int total = 1; for (int i = 0; i < n; i++) total *= 4;
      for (int k = 0; k < total; k++) {
        char steps[5]; int v = k;
        for (int i = 0; i < n; i++) { steps[i] = d4[v % 4]; v /= 4; }
        steps[n] = 0;
        CardLayout L; computeCardLayout(steps, L);
        bool revisit = false;
        for (int i = 0; i < n; i++) {
          if (L.cellX[i] == L.startX && L.cellY[i] == L.startY) revisit = true;
          for (int j = 0; j < i; j++) if (L.cellX[i] == L.cellX[j] && L.cellY[i] == L.cellY[j]) revisit = true;
        }
        if (revisit) continue;
        cards++;
        UiState s; uiStateInit(s);
        std::string line = std::string("CARD:") + steps; applyHubLine(s, line.c_str());
        fullRender(s, st, SM_GAME, a);
        UiState e; uiStateInit(e); applyHubLine(e, "CARD:-"); e.cardKind = CK_NONE;
        // card pixels: compare against a render where the card area is cleared
        int minx = 99, maxx = -1, miny = 99, maxy = -1, outside = 0;
        for (int y = 0; y < 64; y++) for (int x = 0; x < 96; x++) {
          bool inCard = x >= CARD_X && x < CARD_X + CARD_W && y >= CARD_Y && y < CARD_Y + CARD_H;
          if (inCard && a.fb[y][x]) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
          (void)outside;
        }
        int lm = minx - CARD_X, rm = CARD_X + CARD_W - 1 - maxx, tm = miny - CARD_Y, bm = CARD_Y + CARD_H - 1 - maxy;
        if (minx > maxx || abs(lm - rm) > 2 || abs(tm - bm) > 2 || lm < 0 || tm < 0) bad++;
        // square cards: 3 steps whose last cell is next to the start
        if (n == 3 && L.gridW == 2 && L.gridH == 2) {
          squares++;
          uint8_t cw = (uint8_t)((CARD_W - 2) / L.gridW), ch = (uint8_t)((CARD_H - 2) / L.gridH);
          uint8_t cell = std::min<uint8_t>(std::min(cw, ch), CARD_MAX_CELL);
          uint8_t gap = cell >= 16 ? 2 : 1, blk = cell - gap;
          int ox = CARD_X + (CARD_W - L.gridW * cell + gap) / 2, oy = CARD_Y + (CARD_H - L.gridH * cell + gap) / 2;
          int sx = ox + L.startX * cell, sy = oy + L.startY * cell;
          int lx = ox + L.cellX[2] * cell, ly = oy + L.cellY[2] * cell;
          bool startHollow = a.fb[sy + blk / 2][sx + blk / 2] == 0 && a.fb[sy][sx + blk / 2] == 0xFFFF;
          int blackInLast = 0;
          for (int y = ly; y < ly + blk; y++) for (int x = lx; x < lx + blk; x++) if (a.fb[y][x] == 0) blackInLast++;
          bool gapBetween = true;   // a black gap separates the two cells
          if (L.cellX[2] == L.startX) { int gy = std::max(sy, ly) - 1; gapBetween = a.fb[gy][sx + blk / 2] == 0; }
          else { int gx = std::max(sx, lx) - 1; gapBetween = a.fb[sy + blk / 2][gx] == 0; }
          if (!startHollow || blackInLast < 5 || !gapBetween) squareBad++;
          if (strcmp(steps, "RDL") == 0) writePPM(a, "out/square_RDL_check.ppm");
        }
      }
    }
    printf("  cards: %d valid, off-centre/out-of-area: %d; square cards: %d, ambiguous: %d\n", cards, bad, squares, squareBad);
    CHECK(cards == 152); CHECK(bad == 0); CHECK(squares == 8); CHECK(squareBad == 0);
  }

  // IF cards for every family stay inside the card area
  {
    const char* fams[] = { "D", "I3", "I4", "V", "O", "L", "S" };
    const char* acts[] = { "R", "UU", "LLU", "RDL", "URUR" };
    int bad = 0;
    UiState s0; uiStateInit(s0); applyHubLine(s0, "COL:E03030");
    TinyScreen blank(TinyScreenPlus); s0.cardKind = CK_NONE;
    for (const char* f : fams) for (const char* act : acts) {
      UiState s; uiStateInit(s); applyHubLine(s, "COL:E03030");
      std::string l = std::string("CARD:IF:") + f + ":" + act; applyHubLine(s, l.c_str());
      fullRender(s, st, SM_GAME, a);
      UiState w; uiStateInit(w); applyHubLine(w, "COL:E03030"); applyHubLine(w, "CARD:FOR");
      fullRender(w, st, SM_GAME, b);
      if (diffOutside(a, b, CARD_X, CARD_Y, CARD_W, CARD_H) != 0) bad++;
    }
    CHECK(bad == 0);
  }

  // Incremental redraws must end up identical to a from-scratch render.
  std::mt19937 rng(7);
  const char* pool[] = {
    "CARD:UUR", "CARD:RDL", "CARD:L", "CARD:-", "CARD:IF:O:LLU", "CARD:IF:I4:R", "CARD:FOR", "CARD:DDLL",
    "SEQ:H", "SEQ:H2", "SEQ:1", "SEQ:9", "SEQ:-", "REG:0000", "REG:1010", "REG:0111", "REG:1111",
    "COL:FF0000", "COL:00FF00", "COL:102030", "HAND:1", "HAND:6", "HAND:0",
    "MSG:LOBBY", "MSG:P2 WINS", "MSG:", "HINT:tap R", "HINT:", "HINT:flip to start!", "DOFF:1", "BLINK", "PING", "JUNK",
  };
  const int poolN = sizeof(pool) / sizeof(pool[0]);
  Renderer r(a);
  r.begin(10, false);
  r.setMode(SM_GAME);
  UiState s; uiStateInit(s);
  int mismatches = 0, maxOpsPerStep = 0, stripTouchedByBlink = 0;
  for (int round = 0; round < 500; round++) {
    int n = 1 + rng() % 3;
    for (int k = 0; k < n; k++) {
      const char* c = pool[rng() % poolN];
      if (applyHubLine(s, c) == HC_BLINK) {
        // a blink must never touch the strip
        TinyScreen before = a;
        r.startBlink(g_now);
        r.step(s, st, g_now, RENDER_BUDGET_US, fakeMicros);
        if (!stripEqual(before, a)) stripTouchedByBlink++;
        g_now += BLINK_MS + 1;
      }
    }
    if (rng() % 4 == 0) uiSetNoHub(s, !s.noHub);
    if (rng() % 7 == 0) uiSetAccMissing(s, !s.accMissing);
    if (rng() % 9 == 0) uiSetBatLow(s, !s.batLow);
    if (rng() % 6 == 0) uiSetTiltArmed(s, (int8_t)((int)(rng() % 3) - 1));
    for (int k = 0; k < (int)(rng() % 3); k++) {
      uint32_t before = a.opCount;
      r.step(s, st, g_now, RENDER_BUDGET_US, fakeMicros);
      maxOpsPerStep = std::max(maxOpsPerStep, (int)(a.opCount - before));
    }
    if (rng() % 2) applyHubLine(s, pool[rng() % poolN]);
    renderToIdle(r, s, st);
    fullRender(s, st, SM_GAME, ref);
    if (!fbEqual(a, ref)) {
      if (mismatches == 0) { writePPM(a, "out/mismatch_incremental.ppm"); writePPM(ref, "out/mismatch_reference.ppm"); }
      mismatches++;
    }
  }
  printf("  incremental vs full render mismatches: %d / 500, max ops per step: %d, blink touched strip: %d\n",
         mismatches, maxOpsPerStep, stripTouchedByBlink);
  CHECK(mismatches == 0);
  CHECK(maxOpsPerStep <= 4);
  CHECK(stripTouchedByBlink == 0);

  // Only changed regions are redrawn: a REG change must not touch anything below the strip.
  {
    TinyScreen d(TinyScreenPlus); Renderer rr(d); rr.begin(10, false); rr.setMode(SM_GAME);
    UiState u; uiStateInit(u);
    applyHubLine(u, "CARD:UUR"); renderToIdle(rr, u, st);
    TinyScreen before = d;
    uint32_t ops0 = d.opCount;
    applyHubLine(u, "REG:1000"); renderToIdle(rr, u, st);
    CHECK(diffOutside(before, d, 0, 0, 96, STRIP_H) == 0);
    printf("  REG change cost: %u draw ops\n", d.opCount - ops0);
    CHECK(d.opCount - ops0 <= 12);
    uint32_t ops1 = d.opCount;
    applyHubLine(u, "REG:1000"); renderToIdle(rr, u, st);   // no change -> no drawing
    CHECK(d.opCount == ops1);
  }
  // Power: off with DOFF; on again is non-blocking (no drawing for DISPLAY_ON_DELAY_MS), then a full redraw.
  {
    TinyScreen d(TinyScreenPlus); Renderer rr(d); rr.begin(10, false); rr.setMode(SM_GAME);
    UiState u; uiStateInit(u);
    renderToIdle(rr, u, st);
    rr.setPower(false, u, g_now);
    CHECK(!d.isOn);
    uint32_t ops0 = d.opCount;
    applyHubLine(u, "CARD:RR"); for (int i = 0; i < 20; i++) rr.step(u, st, g_now, RENDER_BUDGET_US, fakeMicros);
    CHECK(d.opCount == ops0);
    rr.setPower(true, u, g_now);
    rr.step(u, st, g_now + DISPLAY_ON_DELAY_MS - 1, RENDER_BUDGET_US, fakeMicros);
    CHECK(!d.isOn); CHECK(d.opCount == ops0);              // still settling: nothing drawn, nothing blocked
    g_now += DISPLAY_ON_DELAY_MS;
    renderToIdle(rr, u, st);
    fullRender(u, st, SM_GAME, ref);
    CHECK(d.isOn); CHECK(fbEqual(d, ref));
  }
  // Blink: everything except the strip flashes, then the screen is restored.
  {
    TinyScreen d(TinyScreenPlus); Renderer rr(d); rr.begin(10, false); rr.setMode(SM_GAME);
    UiState u; uiStateInit(u); applyHubLine(u, "CARD:UUR"); applyHubLine(u, "REG:1111");
    renderToIdle(rr, u, st);
    TinyScreen before = d;
    rr.startBlink(g_now);
    rr.step(u, st, g_now, RENDER_BUDGET_US, fakeMicros);
    CHECK(d.fb[30][10] == 0xFFFF && d.fb[60][90] == 0xFFFF); CHECK(stripEqual(before, d));
    rr.step(u, st, g_now + BLINK_PHASE_MS, RENDER_BUDGET_US, fakeMicros);
    CHECK(d.fb[30][10] == 0 && stripEqual(before, d));
    g_now += BLINK_MS;
    renderToIdle(rr, u, st);
    CHECK(!rr.blinking()); CHECK(fbEqual(d, before));
  }
  // Toast in the hint line, then the hint comes back.
  {
    TinyScreen d(TinyScreenPlus); Renderer rr(d); rr.begin(10, false); rr.setMode(SM_GAME);
    UiState u; uiStateInit(u); applyHubLine(u, "CARD:UUR"); applyHubLine(u, "HINT:tap R");
    renderToIdle(rr, u, st);
    TinyScreen before = d;
    rr.showToast("DEBUG ON", g_now);
    renderToIdle(rr, u, st);
    CHECK(diffOutside(before, d, 0, INFO_Y, 96, INFO_H) == 0); CHECK(!fbEqual(before, d));
    writePPM(d, "out/21_toast_debug_on.ppm");
    g_now += TOAST_MS + 1;
    renderToIdle(rr, u, st);
    CHECK(fbEqual(before, d));
  }
  // Splash: the live line changes when zAvg changes, nothing else.
  {
    TinyScreen d(TinyScreenPlus); Renderer rr(d); rr.begin(10, false); rr.setMode(SM_SPLASH);
    UiState u; uiStateInit(u);
    StatusInfo s1 = defaultStatus();
    renderToIdle(rr, u, s1);
    TinyScreen before = d;
    s1.zAvg = -1.0f; s1.faceDown = true;
    rr.markStatusDirty();
    renderToIdle(rr, u, s1);
    CHECK(!fbEqual(before, d));
    CHECK(diffOutside(before, d, 0, 26, 96, 38) == 0);
  }
}

int main() {
  testProtocol();
  testFraming();
  testCardLayout();
  testButtons();
  testNodeId();
  testGestures();
  testTilt();
  testSoftI2C();
  testAccelDriver();
  testRenderer();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
