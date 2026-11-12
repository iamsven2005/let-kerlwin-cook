/* Native regression tests against the actual bundled hci.c, with fake transport. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/Helmet_Left/src/stble/STBlueNRG/hci.h"
#include "../firmware/Helmet_Left/src/stble/STBlueNRG/hci_const.h"
#include "../firmware/Helmet_Left/src/stble/STBlueNRG/gp_timer.h"

static unsigned reads, callbacks, ticks, pending;
static int mode;
uint32_t millis(void) { return ticks++; }
uint32_t micros(void) { return ticks*1000; }
void delay(unsigned long ms) { ticks += ms; }
void delayMicroseconds(unsigned int us) { ticks += us/1000; }
void pinMode(uint8_t pin,uint8_t value) {(void)pin; (void)value;}
void digitalWrite(uint8_t pin,uint8_t value) {(void)pin; (void)value;}
int digitalRead(uint8_t pin) {(void)pin; return 0;}
void Disable_SPI_IRQ(void) {}
void Enable_SPI_IRQ(void) {}
void Clear_SPI_EXTI_Flag(void) {}
uint8_t BlueNRG_DataPresent(void) { return mode == 1 || mode == 2 || pending > 0; }
int32_t BlueNRG_SPI_Read_All(uint8_t *buffer,uint8_t size) {
  (void)size; ++reads;
  if (mode == 1) return 0; // stuck IRQ, no SPI bytes
  if (mode == 2) { buffer[0] = 4; return 1; } // short frame
  if (mode == 3) { // command-complete event for ogf=1, ocf=2
    const uint8_t reply[] = {4,0x0e,4,1,0x02,0x04,0};
    memcpy(buffer,reply,sizeof(reply)); --pending; return sizeof(reply);
  }
  if (pending) {
    buffer[0] = 4; buffer[1] = 0xff; buffer[2] = 0;
    --pending; return 3;
  }
  return 0;
}
void Hal_Write_Serial(const void *p1,const void *p2,int32_t n1,int32_t n2) {
  (void)p1; (void)p2; (void)n1; (void)n2;
  if (mode == 3) pending = 1;
}
void HCI_Event_CB(void *p) { (void)p; ++callbacks; }

int main(void) {
  HCI_Init(); mode = 1; HCI_Isr();
  assert(reads == 1); // formerly could spin forever
  reads = 0; mode = 2; HCI_Isr(); assert(reads == 1);
  mode = 0; reads = 0; callbacks = 0; pending = 30;
  HCI_Isr(); assert(reads == 8); // bounded work, eight-packet pool
  while (pending || !HCI_Queue_Empty()) HCI_Process();
  assert(callbacks == 30);
  uint8_t result = 99;
  struct hci_request req;
  memset(&req,0,sizeof(req)); req.ogf = 1; req.ocf = 2;
  req.rparam = &result; req.rlen = 1;
  HCI_Init(); mode = 3; ticks = 0;
  assert(hci_send_req(&req,0) == 0 && result == 0);
  HCI_Init(); mode = 0; ticks = 0; pending = 0;
  assert(hci_send_req(&req,0) == -1 && ticks < 1000);
  puts("HCI stuck IRQ, short frame, queue budget, polled response, timeout: PASS");
}
