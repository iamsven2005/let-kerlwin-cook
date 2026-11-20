// Hub -> node message parsing and the display state it drives (brief v1.4).
// Pure C++ (host-testable). No allocation; fixed buffers only.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MAX_CARD_STEPS 4
#define MAX_MSG_CHARS  14
#define HINT_DOC_CHARS 14          // documented maximum (brief v1.6 X1: "HINT:" + 14 + "\n" = 20 bytes)
#define MAX_HINT_CHARS 16          // accepted without truncation (longer is cut), layout is designed for 14
#define MAX_LINE       40          // longest line accepted from the hub (longer = dropped)

// Dirty flags: which screen regions need redrawing (layout in display.cpp).
enum DirtyRegion : uint8_t {
  DR_STRIP  = 1 << 0,   // R L V F register strip (top)
  DR_CARD   = 1 << 1,   // card area
  DR_HAND   = 1 << 2,   // colour block + hand number
  DR_SEQ    = 1 << 3,   // sequence badge
  DR_STATUS = 1 << 4,   // status area (NO HUB / ACC / BAT)
  DR_INFO   = 1 << 5,   // hint line
  DR_FULL   = 1 << 6,   // layout change below the strip (MSG on/off, mode change)
  DR_ALL    = 0x7F,
};

enum RegBit : uint8_t { REG_R = 1 << 0, REG_L = 1 << 1, REG_V = 1 << 2, REG_F = 1 << 3 };

enum CardKind : uint8_t { CK_NONE = 0, CK_MOVE, CK_IF, CK_FOR };

// IF condition families (brief v1.4 B2), drawn as cell shapes.
enum CardFamily : uint8_t { FAM_D = 0, FAM_I3, FAM_I4, FAM_V, FAM_O, FAM_L, FAM_S, FAM_COUNT };
const char* familyCode(uint8_t fam);                 // "D", "I3", ...

enum SeqKind : uint8_t { SEQK_NONE = 0, SEQK_MEMBER, SEQK_HEAD };

struct UiState {
  // --- hub-driven state (reset to defaults on every disconnect) ---
  uint8_t  cardKind;                   // CardKind
  char     card[MAX_CARD_STEPS + 1];   // steps of a move card / of the IF action card
  uint8_t  family;                     // CardFamily, for CK_IF
  uint8_t  seqKind;                    // SeqKind
  uint8_t  seqPos;                     // 1..9, 0 = no number (plain SEQ:H)
  uint8_t  reg;                        // RegBit mask
  uint8_t  colR, colG, colB;
  uint8_t  hand;                       // 0 = unassigned (no number shown), 1..6
  bool     msgActive;
  char     msg[MAX_MSG_CHARS + 1];
  char     hint[MAX_HINT_CHARS + 1];   // "" = no hint
  bool     doffAllowed;                // DOFF:1 = display may switch off face-down. Default false (v1.4)
  // --- node-side indicators for the status area ---
  bool     noHub;                      // link up, hub silent ~6 s (cleared on disconnect)
  bool     accMissing;                 // accelerometer not responding (kept across disconnects)
  bool     batLow;                     // battery at or below LOW_BAT_PCT (kept across disconnects)
  int8_t   tiltArmed;                  // -1 / +1 while a tilt is armed (status-area arrow), 0 = none
  uint8_t  dirty;
};

void uiStateInit(UiState& s);          // everything, at boot
void uiStateReset(UiState& s);         // hub-driven defaults (brief v1.4 A1 / section 7); keeps accMissing/batLow

enum HubCmd : uint8_t {
  HC_IGNORED = 0,   // unknown or malformed: ignored (brief section 5)
  HC_APPLIED,       // state updated (may or may not have changed)
  HC_PING,          // caller must answer PONG
  HC_BLINK,         // caller starts the blink animation
};

// Apply one complete line (no terminator). Returns what happened.
HubCmd applyHubLine(UiState& s, const char* line);

// Node-side indicators (each marks the status area dirty only on change).
void uiSetNoHub(UiState& s, bool on);
void uiSetAccMissing(UiState& s, bool on);
void uiSetBatLow(UiState& s, bool on);
void uiSetTiltArmed(UiState& s, int8_t dir);

// Accumulates a byte stream into lines. '\n' ends a line, '\r' is ignored.
// The end of a BLE write also ends a pending line (call endOfWrite()), brief section 5.
class LineAssembler {
 public:
  void feed(const uint8_t* data, size_t len, void (*cb)(const char*, void*), void* ctx);
  void endOfWrite(void (*cb)(const char*, void*), void* ctx);
  void clear() { len_ = 0; overflow_ = false; }
  uint16_t droppedLines() const { return dropped_; }
 private:
  void finish(void (*cb)(const char*, void*), void* ctx);
  char     buf_[MAX_LINE + 1];
  uint8_t  len_ = 0;
  bool     overflow_ = false;
  uint16_t dropped_ = 0;
};

// Node -> hub message formatting helpers (all fit in 20 bytes incl. '\n').
void fmtHello(char* out, size_t n, uint16_t nodeId, const char* fw);   // HELLO:3F2A:1.4
void fmtButtons(char* out, size_t n, uint8_t mask);                    // B:3
void fmtBattery(char* out, size_t n, uint8_t percent);                 // BAT:87
void fmtFaceSync(char* out, size_t n, bool faceDown);                   // FS:1 (silent face-state sync, brief v1.5 FW-Q33)
