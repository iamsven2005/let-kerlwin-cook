#pragma once
#include <stdint.h>
#define F(x) x
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT_PULLUP 2
enum { A0=0,A1,A2 };
unsigned long millis(); unsigned long micros();
void delay(unsigned long); void pinMode(int,int); void digitalWrite(int,int); int digitalRead(int);
void tone(int,unsigned int); void noTone(int);
struct SerialFake { void begin(int) {} void print(const char*) {} void print(char) {} void print(int) {} void println() {} void println(const char*) {} };
extern SerialFake SerialUSB;
