#include "card_layout.h"

bool computeCardLayout(const char* steps, CardLayout& out) {
  int x = 0, y = 0;
  int minX = 0, maxX = 0, minY = 0, maxY = 0;
  int px[MAX_CARD_STEPS], py[MAX_CARD_STEPS];
  uint8_t n = 0;
  uint8_t seg = 0;
  for (; steps[n]; n++) {
    if (n >= MAX_CARD_STEPS) return false;
    char c = steps[n];
    switch (c) {
      case 'U': y--; break;
      case 'D': y++; break;
      case 'L': x--; break;
      case 'R': x++; break;
      default: return false;
    }
    if (n > 0 && c != steps[n - 1]) seg++;
    px[n] = x; py[n] = y;
    out.dir[n] = c;
    out.segment[n] = seg;
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
  }
  if (n == 0) return false;
  out.nSteps = n;
  out.startX = (int8_t)(0 - minX);
  out.startY = (int8_t)(0 - minY);
  for (uint8_t i = 0; i < n; i++) {
    out.cellX[i] = (int8_t)(px[i] - minX);
    out.cellY[i] = (int8_t)(py[i] - minY);
  }
  out.gridW = (uint8_t)(maxX - minX + 1);
  out.gridH = (uint8_t)(maxY - minY + 1);
  return true;
}
