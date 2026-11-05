#pragma once
#define TinyScreenPlus 0
#define thinPixel7_10ptFontInfo 0
struct TinyScreen { TinyScreen(int) {} void begin() {} void setBrightness(int) {} void setFont(int) {} void clearScreen() {} void setCursor(int,int) {} void print(const char*) {} };
