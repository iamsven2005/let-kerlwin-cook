#pragma once
#include <stdint.h>
#include <stddef.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
uint32_t micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
#ifdef __cplusplus
}
#endif
