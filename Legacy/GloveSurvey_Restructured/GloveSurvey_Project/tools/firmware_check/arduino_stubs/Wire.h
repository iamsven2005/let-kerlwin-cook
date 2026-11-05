#pragma once
#include <Arduino.h>
struct TwoWire { void begin(){} void beginTransmission(uint8_t){} size_t write(uint8_t){return 1;} uint8_t endTransmission(bool=true){return 0;} uint8_t requestFrom(uint8_t, uint8_t n){return n;} int available(){return 1;} int read(){return 0x03;} };
extern TwoWire Wire;
