#include <Arduino.h>
#include <Wire.h>
#include <Wireling.h>
#include <WiFi101.h>
#include <TinyScreen.h>
extern "C" {
#include "driver/include/m2m_wifi.h"
}
SerialStub SerialUSB; TwoWire Wire; WirelingClass Wireling; WiFiClass WiFi;
const FONT_INFO thinPixel7_10ptFontInfo{10}, liberationSans_16ptFontInfo{16};
static unsigned long t = 0;
unsigned long millis(){ return t += 7; }
void delay(unsigned long ms){ if (ms==1000){ puts("HALTED"); exit(1);} } void delayMicroseconds(unsigned int){} void pinMode(int,int){} void digitalWrite(int,int){} int digitalRead(int){return 1;}
extern "C" int8_t m2m_wifi_connect(char*, uint8_t, uint8_t, void*, uint16_t){return 0;}
void setup(); void loop();
int main(int argc, char** argv){ setup(); if (argc>1) SerialUSB.in = argv[1]; for (int i=0;i<500;i++) loop(); if (argc>2) SerialUSB.in = argv[2]; for (int i=0;i<3500;i++) loop(); puts("ran 4000 passes"); return 0; }
