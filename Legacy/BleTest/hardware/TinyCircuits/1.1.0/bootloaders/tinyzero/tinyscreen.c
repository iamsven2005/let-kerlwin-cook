/* ----------------------------------------------------------------------------
 *         TinyScreen Display Capability
 * ----------------------------------------------------------------------------
 * Written by Ben Rose for TinyCircuits
 *
 * All rights reserved.
 * ----------------------------------------------------------------------------
 */

/*
 * --------------------
 * SAM-BA Implementation on SAMD21
 * --------------------
*/

#include <stdio.h>
#include <sam.h>
#include "compiler.h"
#include "tinyscreen.h"
#include "font5x7.h"
 
const uint32_t TSP_PIN_SCK = (1ul << 11);
const uint32_t TSP_PIN_DAT = (1ul << 10);
const uint32_t TSP_PIN_DC = (1ul << 12);
const uint32_t TSP_PIN_CS = (1ul << 13);
const uint32_t TSP_PIN_SHDN = (1ul << 28);
const uint32_t TSP_PIN_RST = (1ul << 27);

volatile PortGroup *TSP_PORT_SCK = (volatile PortGroup *)&PORT->Group[1];
volatile PortGroup *TSP_PORT_DAT = (volatile PortGroup *)&PORT->Group[1];
volatile PortGroup *TSP_PORT_DC = (volatile PortGroup *)&PORT->Group[0];
volatile PortGroup *TSP_PORT_CS = (volatile PortGroup *)&PORT->Group[0];
volatile PortGroup *TSP_PORT_SHDN = (volatile PortGroup *)&PORT->Group[0];
volatile PortGroup *TSP_PORT_RST = (volatile PortGroup *)&PORT->Group[0];

int TS_x = 0;
int TS_y = 0;
uint8_t TS_buf[96 * 64];

void TSstartCommand() {
  TSP_PORT_DC->OUTCLR.reg = TSP_PIN_DC;
  TSP_PORT_CS->OUTCLR.reg = TSP_PIN_CS;
}

void TSstartData(void) {
  TSP_PORT_DC->OUTSET.reg = TSP_PIN_DC;
  TSP_PORT_CS->OUTCLR.reg = TSP_PIN_CS;
}

void TSendTransfer(void) {
  TSP_PORT_CS->OUTSET.reg = TSP_PIN_CS;
}

void TSwrite(uint32_t dt) {
  uint32_t i;
  for (i = 0x80; i; i = i >> 1) {
    TSP_PORT_SCK->OUTCLR.reg = TSP_PIN_SCK;
    if (i & dt) {
      TSP_PORT_DAT->OUTSET.reg = TSP_PIN_DAT;
    } else {
      TSP_PORT_DAT->OUTCLR.reg = TSP_PIN_DAT;
    }
    TSP_PORT_SCK->OUTSET.reg = TSP_PIN_SCK;
  }
}

void TSinit(void) {
  //init pins
  TSP_PORT_SCK->DIRSET.reg = TSP_PIN_SCK;
  TSP_PORT_DAT->DIRSET.reg = TSP_PIN_DAT;
  TSP_PORT_SHDN->DIRSET.reg = TSP_PIN_SHDN;
  TSP_PORT_DC->DIRSET.reg = TSP_PIN_DC;
  TSP_PORT_CS->DIRSET.reg = TSP_PIN_CS;
  TSP_PORT_RST->DIRSET.reg = TSP_PIN_RST;
  TSP_PORT_SCK->OUTSET.reg = TSP_PIN_SCK;
  TSP_PORT_DAT->OUTCLR.reg = TSP_PIN_DAT;
  TSP_PORT_SHDN->OUTSET.reg = TSP_PIN_SHDN;
  TSP_PORT_DC->OUTSET.reg = TSP_PIN_DC;
  TSP_PORT_CS->OUTSET.reg = TSP_PIN_CS;
  TSP_PORT_RST->OUTSET.reg = TSP_PIN_RST;
  //reset
  TSP_PORT_RST->OUTCLR.reg = TSP_PIN_RST;
  for (uint32_t i=0; i<5*250; i++)
    __asm__ __volatile__("");//delay about 5ms
  TSP_PORT_RST->OUTSET.reg = TSP_PIN_RST;

  for (uint32_t i=0; i<10*250; i++)
    __asm__ __volatile__("");//delay about 10ms
    
  //datasheet SSD1331 init sequence
  const uint8_t init[42] = {0xAE, 0xA1, 0x00, 0xA2, 0x00, 0xA4, 0xA8, 0x3F,
                            0xAD, 0x8E, 0xB0, 0x0B, 0xB1, 0x31, 0xB3, 0xF0, 0x8A, 0x64, 0x8B, 0x78,
                            0x8C, 0x64, 0xBB, 0x3A, 0xBE, 0x3E, 0x81, 0x91, 0x82, 0x50, 0x83, 0x7D,
                            0xA0/*remap*/, (1 << 5) | (1 << 2), 0x87/*brightness*/, 5, 0x15/*x*/, 0, 95, 0x75/*y*/, 0, 63
                           };

  TSstartCommand();
  for (uint32_t i = 0; i < 42; i++)
    TSwrite(init[i]);
  TSendTransfer();
  memset(TS_buf,0,96*64);
}

void TSsetXY(int x, int y) {
  TS_x=x;
  TS_y=y;
}

void TSwriteBuff(void) {
  TSstartData();
  for (uint32_t i = 0; i < 96*64; i++)
    TSwrite(TS_buf[i]);
  TSendTransfer();
}

void TSwritech(char c) {
  if (c < ' ' || c > '}+2')return;
  c -= ' ';
  uint32_t i,h,px;
  for (i = 0; i < 5; i++) {
    h = Font5x7[c * 5 + i];
    for (px = 0; px < 8; px++) {
      if ((0x01 << px) & h) {
        TS_buf[(96 * (TS_y + px)) + TS_x] = 0xFF;
      } else {
        TS_buf[(96 * (TS_y + px)) + TS_x] = 0x00;
      }
    }
    TS_x++;
    if (TS_x > 95)TS_x = 0;
  }
  TS_x++;
  if (TS_x > 95)TS_x = 0;
  //need to clear column of pixels after character if overwriting text
}

void TSprint(char * s) {
  while (*s) {
    TSwritech(*s);
    s++;
  }
}