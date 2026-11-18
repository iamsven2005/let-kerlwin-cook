#pragma once
#include <stdint.h>
typedef struct { uint8_t au8UserName[21]; uint8_t au8Passwd[41]; } tstr1xAuthCredentials;
enum { M2M_WIFI_SEC_OPEN=1, M2M_WIFI_SEC_WPA_PSK=2, M2M_WIFI_SEC_802_1X=5 };
int8_t m2m_wifi_connect(char*, uint8_t, uint8_t, void*, uint16_t);
