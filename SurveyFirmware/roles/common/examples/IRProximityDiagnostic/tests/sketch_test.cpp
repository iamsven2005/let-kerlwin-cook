#include <cassert>
#include <Wire.h>
#include <Wireling.h>
#include "../IRProximityDiagnostic.ino"
WireFake Wire; WirelingFake Wireling; SerialFake SerialUSB;
static unsigned long clockUs = 0;
static int bursts = 0, stops = 0, mode = 0;
static bool transmitting = false;
unsigned long millis() { clockUs += 1000; return clockUs / 1000; }
unsigned long micros() { clockUs += 100; return clockUs; }
void delay(unsigned long ms) { clockUs += ms * 1000; }
void pinMode(int,int) {}
void digitalWrite(int,int) {}
int digitalRead(int) { return mode && transmitting ? LOW : HIGH; }
void tone(int, unsigned int) { ++bursts; transmitting = true; }
void noTone(int) { ++stops; transmitting = false; }
int main() {
  setup();
  loop();
  assert(bursts == 35 && stops == 35 && !transmitting);
  mode = 1;
  loop();
  assert(bursts == 70 && stops == 70 && !transmitting);
}
