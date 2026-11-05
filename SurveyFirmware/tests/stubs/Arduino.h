#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PI 3.14159265358979f
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
enum { A0=14, A1, A2, A3, A4, A5 };
#include <algorithm>
using std::min;
inline void noInterrupts() {}
inline void interrupts() {}
inline unsigned long micros() { return 1234; }
inline void randomSeed(unsigned long) {}
inline long random(long n) { return n ? 0 : 0; }
inline long random(long a, long b) { return a + random(b-a); }
unsigned long millis();
void delay(unsigned long);
void delayMicroseconds(unsigned int);
void pinMode(int, int);
void digitalWrite(int, int);
int digitalRead(int);
template<class T, class L, class H> T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }
struct SerialStub { const char* in = ""; void begin(long){} size_t print(const char* s){fputs(s,stdout);return strlen(s);} size_t println(const char* s){puts(s);return strlen(s);} int available(){return *in!=0;} int read(){return *in++;} };
extern SerialStub SerialUSB;
