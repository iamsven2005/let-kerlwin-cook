/*
  link.h - talking to the RoombaMapper app over Wi-Fi (UDP) and USB.
  Everything queued during one loop pass goes out as ONE packet, spaced TX_GAP_MS apart
  (the Wi-Fi chip drops packets sent back to back). The app's address is learned from the
  first packet it sends; until then the glove broadcasts on the network.
*/
#pragma once
#include <Arduino.h>

void linkBegin();                        // after Wi-Fi has connected
void linkSendLine(const char* line);     // queue a line (also printed on USB)
void linkSendMessage(const char* text);  // queue "M,<text>"
void linkSendTelemetry();                // queue the P line (call every 200 ms)
void linkCheckIncoming();                // read USB + Wi-Fi lines -> commandsHandleLine()
void linkFlush(bool force);              // send the queued lines
bool linkAppFound();                     // has the app been heard from?
