/* ----------------------------------------------------------------------------
 *         TinyScreen Display Capability
 * ----------------------------------------------------------------------------
 * Written by Ben Rose for TinyCircuits
 *
 * All rights reserved.
 * ----------------------------------------------------------------------------
 */
 
#ifndef _TINYSCREEN_H_
#define _TINYSCREEN_H_

void TSstartCommand(void);
void TSstartData(void);
void TSendTransfer(void);
void TSwrite(uint32_t dt);
void TSinit(void);
void TSsetXY(int x, int y);
void TSwritech(char c);
void TSprint(char * s);

#endif //_TINYSCREEN_H_