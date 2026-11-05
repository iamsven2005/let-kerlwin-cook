#pragma once
#include <Arduino.h>
#include "IPAddress.h"
enum { WL_NO_SHIELD=255, WL_CONNECTED=3 };
class WiFiClass {
public:
  void setPins(int,int,int,int){} uint8_t status(){return WL_CONNECTED;} int begin(const char*){return 0;} int begin(const char*,const char*){return 0;}
  int8_t scanNetworks(){return 0;} const char* SSID(uint8_t){return "";} uint8_t* BSSID(uint8_t* b){return b;} uint8_t* BSSID(uint8_t,uint8_t* b){return b;}
  int32_t RSSI(){return -60;} int32_t RSSI(uint8_t){return -60;} uint8_t channel(uint8_t){return 1;} int disconnect(){return 0;}
  IPAddress localIP(){return IPAddress();} IPAddress subnetMask(){return IPAddress(255,255,255,0);}
private:
  uint8_t startConnect(const char*, uint8_t, const void*){return 0;}
};
extern WiFiClass WiFi;
