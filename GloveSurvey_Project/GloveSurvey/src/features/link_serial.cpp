#include "link.h"
#if FEAT_LINK_SERIAL

static char serialBuf[96];
static int  serialLen = 0;
static unsigned long linesOut = 0, linesIn = 0, tooLong = 0;

void linkSerialSend(const char* line) {
  SerialUSB.print(line);
  linesOut++;
}

void linkSerialUpdate() {
  while (SerialUSB.available()) {
    char c = SerialUSB.read();
    if (c == '\n') { serialBuf[serialLen] = 0; linesIn++; linkHandleLine(serialBuf); serialLen = 0; }
    else if (serialLen < (int)sizeof(serialBuf) - 1) serialBuf[serialLen++] = c;
    else tooLong++;
  }
}

#if DEBUG_ANY
void linkSerialDebug(DebugText& t) {
  t.add("lines out %lu", linesOut);
  t.add("lines in %lu", linesIn);
  t.add("cut short %lu", tooLong);
}
#endif

#endif
