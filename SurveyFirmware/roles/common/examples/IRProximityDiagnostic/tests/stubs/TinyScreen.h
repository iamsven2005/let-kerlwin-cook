#pragma once
#include "Arduino.h"
enum { TinyScreenPlus, TS_8b_White, TS_8b_Black, TS_8b_Green, TSRectangleNoFill, TSRectangleFilled };
const int liberationSans_8ptFontInfo = 0;
struct TinyScreen { TinyScreen(int) {} void begin() {} void setBrightness(int) {} void setFont(int) {} void fontColor(int,int) {} void clearScreen() {} void setCursor(int,int) {} void print(const char*) {} void print(int) {} void drawRect(int,int,int,int,int,int) {} };
