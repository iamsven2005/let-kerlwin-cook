/*
  commands.h - commands from the app: C,<id>,<command>[,args]  (answered with K,<id>,<text>)
    HELLO, PING, RESET, TURN,<deg>, SETPOSE,x,y[,h], PAUSE, RUN, MODE,FAST|SLOW, MEASURE,
    ROAM, AUTOROAM,ON|OFF, CALSTEP_START, CALSTEP_END,<mm>, POSECAL,HANG|POINT,
    SET,<key>,<value>
  Also "A,<seq>" (a Wi-Fi reading arrived) and "PING" (keep-alive, no answer).
*/
#pragma once

extern bool wifiReconnectWanted;     // SET,join,1: connect with new network details
extern bool roamRequested;           // ROAM: look for a stronger access point now
extern bool autoRoam;                // AUTOROAM: roam by itself while standing still

void commandsHandleLine(char* line);
