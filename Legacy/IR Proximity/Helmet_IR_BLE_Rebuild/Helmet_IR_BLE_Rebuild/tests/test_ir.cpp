#include <assert.h>
#include <stdio.h>
#include "../firmware/Helmet_Left/src/IrScanner.h"
#include "../firmware/Helmet_Left/src/Protocol.h"
#include <string.h>
#include <initializer_list>

static uint32_t clockUs = 0;
static unsigned highCount[2] = {0,0};
static unsigned readCount[2] = {0,0};
static unsigned lowPerBurst[2] = {6,5};
static int current[2] = {LOW,LOW};
extern "C" {
uint32_t millis() { return clockUs / 1000; }
uint32_t micros() { return clockUs; }
void delay(unsigned long ms) { clockUs += ms*1000; }
void delayMicroseconds(unsigned int us) { clockUs += us; }
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) {
  unsigned p = pin == A0 ? 0 : 1;
  current[p] = value;
  assert(!(current[0] == HIGH && current[1] == HIGH));
  if (value == HIGH) ++highCount[p];
}
int digitalRead(uint8_t pin) {
  unsigned p = pin == A1 ? 0 : 1;
  return (readCount[p]++ % 25) < lowPerBurst[p] ? LOW : HIGH;
}
}
int main() {
  IrScanner scanner;
  scanner.begin();
  for (unsigned ticks = 0; !scanner.hasSample && ticks < 1000; ++ticks) {
    scanner.tick(); clockUs += 100;
  }
  assert(scanner.hasSample && scanner.sequence == 1);
  assert(scanner.values[0] == 6 && scanner.values[1] == 0);
  assert(highCount[0] == 150 && highCount[1] == 150);
  lowPerBurst[0] = 0; lowPerBurst[1] = 25;
  for (unsigned ticks = 0; scanner.sequence < 2 && ticks < 10000; ++ticks) {
    scanner.tick(); clockUs += 100;
  }
  assert(scanner.values[0] == 0 && scanner.values[1] == 6);
  Reading r;
  const char *good = "R,6,0,65535";
  assert(parseReading((const uint8_t *)good,strlen(good),r));
  assert(r.board == 'R' && r.outer == 6 && r.inner == 0 && r.sequence == 65535);
  for (const char *bad : {"L,7,0,1", "R,0,0,65536", "L,1,2,", "L,1,2,1\n"}) {
    assert(!parseReading((const uint8_t *)bad,strlen(bad),r));
  }
  puts("IR pulse count, threshold, independent pairs, protocol: PASS");
}
