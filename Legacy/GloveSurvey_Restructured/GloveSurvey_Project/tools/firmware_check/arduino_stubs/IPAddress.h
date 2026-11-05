#pragma once
#include <Arduino.h>
struct IPAddress { uint8_t b[4]; IPAddress(){memset(b,0,4);} IPAddress(uint8_t a,uint8_t c,uint8_t d,uint8_t e){b[0]=a;b[1]=c;b[2]=d;b[3]=e;}
  uint8_t operator[](int i) const {return b[i];} bool operator==(const IPAddress& o) const {return !memcmp(b,o.b,4);} };
