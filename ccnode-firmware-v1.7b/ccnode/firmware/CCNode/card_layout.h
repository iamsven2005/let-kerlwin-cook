// Turns a movement card ("UUR") into grid cells. Pure C++ (host-testable).
#pragma once
#include <stdint.h>
#include "protocol.h"

struct CardLayout {
  uint8_t nSteps;
  int8_t  startX, startY;                   // grid coords of the start cell (0,0 = top-left of bbox)
  int8_t  cellX[MAX_CARD_STEPS];            // grid coords of each step's block
  int8_t  cellY[MAX_CARD_STEPS];
  uint8_t segment[MAX_CARD_STEPS];          // straight-run index of each step (0..3)
  char    dir[MAX_CARD_STEPS];              // 'U','D','L','R'
  uint8_t gridW, gridH;                     // bounding box in cells (incl. start cell), <= 5
};

// Returns false for an invalid card. Directions are absolute: U = up the screen.
bool computeCardLayout(const char* steps, CardLayout& out);
