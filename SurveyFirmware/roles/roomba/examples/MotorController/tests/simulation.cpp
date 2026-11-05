#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using std::min;
#define PI 3.14159265358979323846
struct SerialStub { void begin(int) {} } Serial;
unsigned long clock_ms = 0;
unsigned long millis() { return clock_ms; }
int digitalRead(int) { return 0; }
void noInterrupts() {}
void interrupts() {}
std::vector<std::string> sent;
uint8_t lib_aci_send_data(uint8_t, uint8_t* p, uint8_t n) { sent.emplace_back((char*)p, n); return 1; }
int BLEsetup() { return 0; }
void aci_loop() {}
#include "../MotorController.ino"
void packet(const char* s) { size_t n = strlen(s); memcpy(ble_rx_buffer, s, n); ble_rx_buffer_len = n; loop(); }
void advance(unsigned ms) { clock_ms += ms; loop(); }
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
 setup(); ble_connection_state = 1; loop();
 packet("ID?"); CHECK(sent.back() == "ID MOTOR");
 packet("M1 DRIVE 30"); CHECK(step == DRIVING); CHECK(sent.back() == "A1");
 advance(300); CHECK(step == IDLE); CHECK(sent.back() == "DONE");
 packet("M1 DRIVE 30"); CHECK(step == IDLE); CHECK(sent.back() == "A1");
 packet("M2 TURN 90 L"); CHECK(step == TURNING); advance(1300); CHECK(step == IDLE); CHECK(sent.back() == "DONE");
 packet("M3 DRIVE"); CHECK(step == DRIVING); packet("M4 STOP"); CHECK(step == IDLE); CHECK(sent.back() == "DONE");
 packet("M5 DRIVE"); CHECK(step == DRIVING); ble_connection_state = 0; loop(); CHECK(step == IDLE); CHECK(wheelSpeedL == 0 && wheelSpeedR == 0);
 std::puts("PASS: ID, drive, duplicate, turn, stop, disconnect (stub transport)");
}
