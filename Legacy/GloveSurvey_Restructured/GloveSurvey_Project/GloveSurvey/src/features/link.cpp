#include "link.h"
#if FEAT_ANY_LINK
#include "read_cache.h"
#include "commands.h"

void linkSendLine(const char* line) {
#if FEAT_LINK_SERIAL
  linkSerialSend(line);
#endif
#if FEAT_LINK_UDP
  linkUdpQueue(line, false);
#endif
}

bool linkSendLowPriority(const char* line) {
#if FEAT_LINK_UDP
  if (!linkUdpQueue(line, true)) return false;   // no room in this packet
#endif
#if FEAT_LINK_SERIAL
  linkSerialSend(line);
#endif
  return true;
}

void linkSendMessage(const char* text) {
  char line[80];
  snprintf(line, sizeof(line), "M,%s\n", text);
  linkSendLine(line);
}

void linkFlush(bool force) {
#if FEAT_LINK_UDP
  linkUdpFlush(force);
#else
  (void)force;
#endif
}

bool linkAppFound() {
#if FEAT_LINK_UDP
  return linkUdpAppFound();
#else
  return true;
#endif
}

void linkHandleLine(char* line) {
  if (!strncmp(line, "PING", 4)) return;                       // keep-alive, no answer
  if (line[0] == 'A' && line[1] == ',') {                      // a Wi-Fi reading arrived
    readingsAck((uint16_t)strtoul(line + 2, nullptr, 10));
    return;
  }
#if FEAT_COMMANDS
  commandsHandleLine(line);
#endif
}

#endif
