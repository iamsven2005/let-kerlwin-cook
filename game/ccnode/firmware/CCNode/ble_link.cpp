// BLE link for the ST BlueNRG-MS BLE TinyShield (ASD2116).
// Based on TinyCircuits' STBLE "UARTPassThrough" example (UART.ino), with:
//  - unique advertised name, service UUID in the scan response
//  - HELLO sync triggered by every CCCD enable (subscribe / re-subscribe)
//  - a small TX queue with retry on BLE_STATUS_INSUFFICIENT_RESOURCES
//  - a connection-interval update request (7.5-15 ms)
//  - ACI calls for sending are made from the main loop
#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "src/STBLE/STBLE.h"   // bundled STBLE 0.1.2 with the IRQ-masking patch (src/STBLE/PATCHES.md)
#include "ble_link.h"
#include "config.h"

// The vendored library references this symbol for its optional CSV trace.
volatile uint32_t ms_counter = 0;

namespace {
  BleLink::Callbacks cb;
  char     devName[20];
  uint8_t  devNameLen = 0;

  uint16_t servHandle = 0, rxCharHandle = 0, txCharHandle = 0;
  uint16_t connHandle = 0;
  bool     isConnected = false;
  bool     isReady = false;
  bool     needAdvertise = true;
  uint32_t lastAdvTry = 0;
  bool     advertising = false;
  bool     advSlow = false;
  uint32_t advStartMs = 0;
  uint32_t connectedAt = 0;
  bool     paramRequested = false;
  bool     warnedNoCccd = false;
  uint8_t  ownAddr[6] = { 0 };
  bool     addrSetOk = false;
  uint16_t connInterval = 0;
  uint32_t dropped = 0;

  // TX queue (one entry = one notification, max 20 bytes incl. '\n')
  char     txq[TX_QUEUE_LEN][21];
  uint8_t  txLen[TX_QUEUE_LEN];
  uint8_t  txHead = 0, txCount = 0;

  // Nordic UART Service UUIDs (little-endian byte order for the stack)
  // 6E400001-B5A3-F393-E0A9-E50E24DCCA9E service
  // 6E400002-...  RX: hub writes here     (TinyCircuits calls this "TX char")
  // 6E400003-...  TX: node notifies here  (TinyCircuits calls this "RX char")
  void nusUuid(uint8_t uuid[16], uint8_t which) {
    const uint8_t be[16] = { 0x6E, 0x40, 0x00, which, 0xB5, 0xA3, 0xF3, 0x93,
                             0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E };
    for (int i = 0; i < 16; i++) uuid[i] = be[15 - i];
  }

  void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
  void logf(const char* fmt, ...) {
    if (!cb.log) return;
    char b[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    cb.log(b);
  }

  void clearQueue() { txHead = txCount = 0; }

  // Called on every CCCD enable: the brief wants the full HELLO sync repeated
  // on each (re-)subscribe, so this fires even if already subscribed.
  void onSubscribed() {
    isReady = true;
    if (cb.onReady) cb.onReady();
  }

  tBleStatus addUartService() {
    tBleStatus ret;
    uint8_t uuid[16];
    nusUuid(uuid, 0x01);
    ret = aci_gatt_add_serv(UUID_TYPE_128, uuid, PRIMARY_SERVICE, 7, &servHandle);
    if (ret != BLE_STATUS_SUCCESS) return ret;
    nusUuid(uuid, 0x02);
    ret = aci_gatt_add_char(servHandle, UUID_TYPE_128, uuid, 20,
                            CHAR_PROP_WRITE_WITHOUT_RESP | CHAR_PROP_WRITE, ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &rxCharHandle);
    if (ret != BLE_STATUS_SUCCESS) return ret;
    nusUuid(uuid, 0x03);
    ret = aci_gatt_add_char(servHandle, UUID_TYPE_128, uuid, 20,
                            CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, 0, 16, 1, &txCharHandle);
    return ret;
  }

  bool setConnectable(uint16_t intervalMs) {
    // Advertising data: flags (added by the stack) + complete local name.
    // The 128-bit service UUID does not fit next to the name in 31 bytes, so it
    // goes in the scan response. Hubs should filter on the "CCNODE-" name prefix.
    char adName[21];
    adName[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(adName + 1, devName, devNameLen);
    uint8_t scanRsp[18];
    scanRsp[0] = 17;
    scanRsp[1] = AD_TYPE_128_BIT_SERV_UUID_CMPLT_LIST;
    nusUuid(scanRsp + 2, 0x01);
    hci_le_set_scan_resp_data(sizeof(scanRsp), scanRsp);
    tBleStatus ret = aci_gap_set_discoverable(ADV_IND,
                        (uint16_t)((intervalMs * 1000UL) / 625), (uint16_t)((intervalMs * 1000UL) / 625),
                        STATIC_RANDOM_ADDR, NO_WHITE_LIST_USE,   // our fixed static random address (set in begin())
                        devNameLen + 1, adName, 0, NULL, 0, 0);
    if (ret != BLE_STATUS_SUCCESS) { logf("ble: set_discoverable failed 0x%02X", ret); return false; }
    logf("ble: advertising as %s every %u ms", devName, intervalMs);
    return true;
  }
}

// ---------------------------------------------------------------------------
// HCI event callback (called from HCI_Process() in main-loop context)
// ---------------------------------------------------------------------------
void HCI_Event_CB(void* pckt) {
  hci_uart_pckt* hci_pckt = (hci_uart_pckt*)pckt;
  hci_event_pckt* event_pckt = (hci_event_pckt*)hci_pckt->data;
  if (hci_pckt->type != HCI_EVENT_PKT) return;

  switch (event_pckt->evt) {
    case EVT_DISCONN_COMPLETE: {
      bool was = isConnected;
      isConnected = false;
      isReady = false;
      connHandle = 0;
      connInterval = 0;
      clearQueue();
      needAdvertise = true;
      advertising = false;
      lastAdvTry = millis() - BLE_ADV_RETRY_MS;     // re-advertise on the next loop pass (v1.4 A5)
      logf("ble: disconnected");
      if (was && cb.onDisconnect) cb.onDisconnect();
      break;
    }
    case EVT_LE_META_EVENT: {
      evt_le_meta_event* evt = (evt_le_meta_event*)event_pckt->data;
      if (evt->subevent == EVT_LE_CONN_COMPLETE) {
        evt_le_connection_complete* cc = (evt_le_connection_complete*)evt->data;
        if (cc->status == 0) {
          isConnected = true;
          isReady = false;
          connHandle = cc->handle;
          connInterval = cc->interval;
          connectedAt = millis();
          paramRequested = false;
          warnedNoCccd = false;
          needAdvertise = false;
          advertising = false;
          clearQueue();
          logf("ble: connected, interval %u x1.25ms", cc->interval);
        }
      } else if (evt->subevent == EVT_LE_CONN_UPDATE_COMPLETE) {
        evt_le_connection_update_complete* uc = (evt_le_connection_update_complete*)evt->data;
        if (uc->status == 0) connInterval = uc->interval;
        logf("ble: conn update status %u interval %u x1.25ms latency %u", uc->status, uc->interval, uc->latency);
      }
      break;
    }
    case EVT_VENDOR: {
      evt_blue_aci* blue_evt = (evt_blue_aci*)event_pckt->data;
      switch (blue_evt->ecode) {
        case EVT_BLUE_GATT_ATTRIBUTE_MODIFIED: {
          evt_gatt_attr_modified_IDB05A1* evt = (evt_gatt_attr_modified_IDB05A1*)blue_evt->data;
          if (evt->attr_handle == rxCharHandle + 1) {
            if (cb.onRx) cb.onRx(evt->att_data, evt->data_length);
          } else if (evt->attr_handle == txCharHandle + 2) {
            // CCCD of the notify characteristic: bit0 = notifications on
            if (evt->data_length >= 1 && (evt->att_data[0] & 0x01)) { logf("ble: hub subscribed"); onSubscribed(); }
            else { isReady = false; clearQueue(); logf("ble: hub unsubscribed"); }
          }
          break;
        }
        case EVT_BLUE_L2CAP_CONN_UPD_RESP: {
          evt_l2cap_conn_upd_resp* r = (evt_l2cap_conn_upd_resp*)blue_evt->data;
          logf("ble: interval request %s", r->result == 0 ? "accepted" : "rejected by hub");
          break;
        }
        case EVT_BLUE_GATT_TX_POOL_AVAILABLE:
          break;   // flush() retries on the next poll
      }
      break;
    }
  }
}

// ---------------------------------------------------------------------------

namespace BleLink {

void bleAddr(char out[18]);

bool begin(const char* deviceName, const uint8_t staticAddr[6], const Callbacks& c) {
  cb = c;
  strncpy(devName, deviceName, sizeof(devName) - 1);
  devName[sizeof(devName) - 1] = 0;
  devNameLen = (uint8_t)strlen(devName);

  HCI_Init();
  BNRG_SPI_Init();
  BlueNRG_RST();

  if (aci_gatt_init() != BLE_STATUS_SUCCESS) { logf("ble: GATT init failed (module missing?)"); return false; }

  uint16_t gapServ, devNameChar, appearanceChar;
  if (aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, devNameLen, &gapServ, &devNameChar, &appearanceChar) != BLE_STATUS_SUCCESS) {
    logf("ble: GAP init failed");
    return false;
  }
  aci_gatt_update_char_value(gapServ, devNameChar, 0, devNameLen, (uint8_t*)devName);

  // Static random address derived from the node ID (brief v1.5 FW-Q31): the BlueNRG-MS's own
  // stored random address is read-only, so ours is set in the controller after GAP init and
  // used by advertising (own address type STATIC_RANDOM_ADDR). Same address after every power cycle.
  memcpy(ownAddr, staticAddr, 6);
  addrSetOk = hci_le_set_random_address(ownAddr) == BLE_STATUS_SUCCESS;
  {
    char a[18];
    bleAddr(a);
    logf("ble: static random address %s %s", a, addrSetOk ? "set" : "NOT SET (module default in use)");
  }

  if (addUartService() != BLE_STATUS_SUCCESS) { logf("ble: UART service failed"); return false; }

  aci_hal_set_tx_power_level(1, 3);   // high power, level 3 (as in the TinyCircuits example)
  needAdvertise = true;
  lastAdvTry = millis() - BLE_ADV_RETRY_MS;          // advertise on the first loop pass (v1.4 A5)
  logf("ble: stack up");
  return true;
}

void poll(uint32_t nowMs) {
  HCI_Process();

  // v1.4 A5: advertise fast (100 ms) for 30 s after boot or a disconnect, then slow (500 ms).
  if (!isConnected && needAdvertise && (uint32_t)(nowMs - lastAdvTry) >= BLE_ADV_RETRY_MS) {
    lastAdvTry = nowMs;
    if (setConnectable(BLE_ADV_FAST_MS)) { needAdvertise = false; advertising = true; advSlow = false; advStartMs = nowMs; }
  }
  if (!isConnected && advertising && !advSlow && (uint32_t)(nowMs - advStartMs) >= BLE_ADV_FAST_PERIOD_MS) {
    aci_gap_set_non_discoverable();
    if (setConnectable(BLE_ADV_SLOW_MS)) advSlow = true;
    else { advertising = false; needAdvertise = true; }   // retry fast on the next pass
  }

  if (isConnected) {
    uint32_t up = nowMs - connectedAt;
    if (!paramRequested && up >= BLE_CONN_UPDATE_DELAY_MS) {
      paramRequested = true;
      if (connInterval == 0 || connInterval > BLE_CONN_INT_MAX) {
        tBleStatus r = aci_l2cap_connection_parameter_update_request(connHandle, BLE_CONN_INT_MIN, BLE_CONN_INT_MAX,
                                                                     BLE_CONN_LATENCY, BLE_CONN_TIMEOUT);
        logf("ble: requested %u-%u x1.25ms (status 0x%02X)", BLE_CONN_INT_MIN, BLE_CONN_INT_MAX, r);
      }
    }
    if (!isReady && !warnedNoCccd && up >= BLE_SUBSCRIBE_WARN_MS) {
      warnedNoCccd = true;   // HELLO is only ever sent on a CCCD enable (brief section 5)
      logf("ble: connected but hub has not enabled TX notifications");
    }
  }
  flush();
}

bool connected() { return isConnected; }
bool addressSet() { return addrSetOk; }
void bleAddr(char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", ownAddr[5], ownAddr[4], ownAddr[3], ownAddr[2], ownAddr[1], ownAddr[0]);
}
const char* stateName() {
  if (isConnected) return isReady ? "subscribed" : "connected";
  if (advertising) return advSlow ? "adv-slow" : "adv-fast";
  return "idle";
}
bool ready() { return isConnected && isReady; }
uint16_t connIntervalUnits() { return connInterval; }
uint32_t droppedTx() { return dropped; }

bool send(const char* msg) {
  if (!ready()) return false;
  size_t n = strlen(msg);
  if (n > 19) n = 19;                         // 19 chars + '\n' = 20-byte notification
  if (txCount == TX_QUEUE_LEN) {              // link stalled: drop the oldest, keep the newest state
    txHead = (txHead + 1) % TX_QUEUE_LEN;
    txCount--;
    dropped++;
  }
  uint8_t slot = (txHead + txCount) % TX_QUEUE_LEN;
  memcpy(txq[slot], msg, n);
  txq[slot][n] = '\n';
  txLen[slot] = (uint8_t)(n + 1);
  txCount++;
  return true;
}

void flush() {
  while (txCount && ready()) {
    tBleStatus r = aci_gatt_update_char_value(servHandle, txCharHandle, 0, txLen[txHead], (uint8_t*)txq[txHead]);
    if (r == BLE_STATUS_INSUFFICIENT_RESOURCES) return;   // radio buffers full: retry next pass
    if (r != BLE_STATUS_SUCCESS) dropped++;
    txHead = (txHead + 1) % TX_QUEUE_LEN;
    txCount--;
  }
}

}  // namespace BleLink
