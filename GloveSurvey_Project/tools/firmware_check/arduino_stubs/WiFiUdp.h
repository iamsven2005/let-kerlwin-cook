#pragma once
#include "WiFi101.h"
struct WiFiUDP { uint8_t begin(uint16_t){return 1;} void stop(){} int beginPacket(IPAddress,uint16_t){return 1;} size_t write(const uint8_t*,size_t n){return n;} int endPacket(){return 1;}
  int parsePacket(){return 0;} IPAddress remoteIP(){return IPAddress();} int read(char*,size_t){return 0;} };
