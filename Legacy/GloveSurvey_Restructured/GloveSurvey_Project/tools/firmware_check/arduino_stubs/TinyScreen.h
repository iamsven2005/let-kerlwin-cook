#pragma once
#include <Arduino.h>
struct FONT_INFO { int h; };
extern const FONT_INFO thinPixel7_10ptFontInfo, liberationSans_16ptFontInfo;
enum { TinyScreenPlus=1, TSBitDepth8=1, TSRectangleFilled=1 };
enum { TS_8b_Black=0, TS_8b_White, TS_8b_Blue, TS_8b_Yellow, TS_8b_Green, TS_8b_Red };
enum { TSButtonUpperLeft=1, TSButtonUpperRight=2, TSButtonLowerLeft=4, TSButtonLowerRight=8 };
struct TinyScreen { TinyScreen(int){} void begin(){} void setBrightness(int){} void setBitDepth(int){} void clearScreen(){}
  void setFont(const FONT_INFO&){} void fontColor(uint8_t,uint8_t){} int getPrintWidth(char*){return 10;} void setCursor(int,int){} size_t print(const char*){return 0;}
  void drawRect(int,int,int,int,int,uint8_t){} uint8_t getButtons(uint8_t){return 0;} };
