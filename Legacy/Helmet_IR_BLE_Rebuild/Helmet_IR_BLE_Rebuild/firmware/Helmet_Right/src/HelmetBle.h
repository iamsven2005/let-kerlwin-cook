#pragma once
#include <Arduino.h>

void bleBegin(char board);
void blePoll();
bool bleSend(const char *text);
bool bleTakeMessage(uint8_t *out, uint8_t &length);
bool bleConnected();
bool bleSubscribed();
bool bleReady();
uint8_t bleLastError();
uint32_t bleRecoveries();
