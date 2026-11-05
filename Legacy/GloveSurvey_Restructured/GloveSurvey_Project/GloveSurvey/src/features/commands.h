/*
  commands - commands from the app: C,<id>,<command>[,args]  (answered with K,<id>,<text>)
    HELLO, FEATURES, RESET, TURN,<deg>, SETPOSE,x,y[,h], PAUSE, RUN, MODE,FAST|SLOW,
    MEASURE, ROAM, AUTOROAM,ON|OFF, CALSTEP_START, CALSTEP_END,<mm>, POSECAL,HANG|POINT,
    DEBUG,ON|OFF|NEXT|PREV|HOLD|AUTO|<feature>, MIRROR,ON|OFF, SET,<key>,<value>
  HELLO and FEATURES also send the F line: F,<feature>,<feature>,... (what is compiled in).
  A command for a switched-off feature is answered "<feature> is off in this build".
  ("A,<seq>" acks and "PING" are handled in link.cpp.)
*/
#pragma once
#include "../core/feature.h"

#if FEAT_COMMANDS
void commandsHandleLine(char* line);
void commandsDebug(DebugText& t);
#endif
