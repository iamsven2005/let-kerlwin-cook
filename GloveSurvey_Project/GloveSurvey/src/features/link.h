/*
  link - talking to the RoombaMapper app. Two transports, each its own feature:
    link_udp     Wi-Fi (UDP). Everything queued during one loop pass goes out as ONE
                 packet, spaced TX_GAP_MS apart (the Wi-Fi chip drops packets sent back
                 to back). The app's address is learned from the first packet it sends;
                 until then the glove broadcasts on the network.
    link_serial  the USB cable. Every line is also printed here.
  Incoming lines from either go to linkHandleLine(): acks to the reading cache,
  everything else to commands.
*/
#pragma once
#include "../core/feature.h"

#if FEAT_ANY_LINK
void linkSendLine(const char* line);        // queue a line
bool linkSendLowPriority(const char* line); // queue only if there's room (debug); false = not sent
void linkSendMessage(const char* text);     // queue "M,<text>"
void linkFlush(bool force);                 // send the queued lines (main loop, every pass)
void linkHandleLine(char* line);            // an incoming line from either transport
bool linkAppFound();                        // has the app been heard from (UDP)?
#else
inline void linkSendLine(const char*) {}
inline bool linkSendLowPriority(const char*) { return false; }
inline void linkSendMessage(const char*) {}
inline void linkFlush(bool) {}
inline bool linkAppFound() { return true; }
#endif

#if FEAT_LINK_UDP
void linkUdpBegin();                        // after Wi-Fi has connected
void linkUdpUpdate();                       // read incoming packets
void linkUdpDebug(DebugText& t);
bool linkUdpQueue(const char* line, bool lowPriority);
void linkUdpFlush(bool force);
bool linkUdpAppFound();
#endif

#if FEAT_LINK_SERIAL
void linkSerialUpdate();                    // read incoming lines
void linkSerialDebug(DebugText& t);
void linkSerialSend(const char* line);
#endif
