#include "HelmetBle.h"
#include "stble/STBLE.h"
#include <string.h>
#include <stddef.h>

static_assert(offsetof(evt_gatt_attr_modified_IDB05A1, att_data) == 7,
              "BlueNRG event layout must match the 7-byte wire header");
static_assert(sizeof(evt_le_connection_complete) == 18,
              "BlueNRG connection event must be packed");

namespace {
char identity = 'L';
bool ready = false, connected = false, subscribed = false;
bool advertise = true, restartNeeded = false;
uint16_t serviceHandle = 0, writeHandle = 0, notifyHandle = 0;
uint8_t rx[20], rxLength = 0, lastError = 0, failures = 0;
uint32_t nextAttempt = 0, recoveries = 0;

bool checked(uint8_t status) {
  lastError = status;
  return status == BLE_STATUS_SUCCESS;
}

void uuid(uint8_t *out, uint8_t kind) {
  const uint8_t base[] = {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,
                         0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e};
  memcpy(out, base, 16); out[12] = kind;
}

bool initialise() {
  connected = subscribed = false; rxLength = 0; advertise = true;
  HCI_Init(); BNRG_SPI_Init(); BlueNRG_RST();
  // Distinct development addresses. Configure more identities before adding boards.
  uint8_t address[] = {static_cast<uint8_t>(identity),0x48,0x00,0xe1,0x80,0x02};
  if (!checked(aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET,
                 CONFIG_DATA_PUBADDR_LEN, address))) return false;
  if (!checked(aci_gatt_init())) return false;
  uint16_t gapService, nameHandle, appearance;
  if (!checked(aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, 7,
                       &gapService, &nameHandle, &appearance))) return false;
  char name[] = "HelmetL"; name[6] = identity;
  if (!checked(aci_gatt_update_char_value(gapService, nameHandle, 0, 7,
                         reinterpret_cast<uint8_t *>(name)))) return false;
  uint8_t id[16]; uuid(id, 1);
  if (!checked(aci_gatt_add_serv(UUID_TYPE_128, id, PRIMARY_SERVICE, 7,
                               &serviceHandle))) return false;
  uuid(id, 2);
  if (!checked(aci_gatt_add_char(serviceHandle, UUID_TYPE_128, id, 20,
            CHAR_PROP_WRITE_WITHOUT_RESP, ATTR_PERMISSION_NONE,
            GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 1, &writeHandle))) return false;
  uuid(id, 3);
  if (!checked(aci_gatt_add_char(serviceHandle, UUID_TYPE_128, id, 20,
            CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
            16, 1, &notifyHandle))) return false;
  if (!checked(aci_hal_set_tx_power_level(1, 3))) return false;
  failures = 0;
  return true;
}
}

void bleBegin(char board) {
  identity = board;
  ready = initialise();
  nextAttempt = millis() + 3000;
}

void blePoll() {
  const uint32_t now = millis();
  if (!ready || restartNeeded) {
    if (static_cast<int32_t>(now - nextAttempt) < 0) return;
    ++recoveries; restartNeeded = false;
    ready = initialise(); nextAttempt = millis() + 3000;
    return;
  }
  HCI_Process(); // patched driver: foreground polling only; bounded read attempts
  if (advertise && static_cast<int32_t>(now - nextAttempt) >= 0) {
    char name[] = {AD_TYPE_COMPLETE_LOCAL_NAME,'H','e','l','m','e','t',identity};
    if (!checked(hci_le_set_scan_resp_data(0, NULL))) {
      restartNeeded = true; nextAttempt = millis() + 3000; return;
    }
    // Match PUBLIC_ADDR to CONFIG_DATA_PUBADDR above.
    if (checked(aci_gap_set_discoverable(ADV_IND,160,320,PUBLIC_ADDR,
                 NO_WHITE_LIST_USE,sizeof(name),name,0,NULL,0,0))) advertise = false;
    else if (++failures >= 3) restartNeeded = true;
    nextAttempt = millis() + 1000;
  }
}

bool bleSend(const char *text) {
  if (!ready || restartNeeded || !connected || !subscribed) return false;
  size_t n = strlen(text);
  if (!n || n > 20) return false;
  uint8_t status = aci_gatt_update_char_value(serviceHandle, notifyHandle, 0,
                         static_cast<uint8_t>(n), text);
  lastError = status;
  if (status == BLE_STATUS_SUCCESS) { failures = 0; return true; }
  // Buffer congestion drops this sample; retry only at the next scheduled TX.
  if (status != BLE_STATUS_INSUFFICIENT_RESOURCES && ++failures >= 3) {
    restartNeeded = true; nextAttempt = millis() + 3000;
  }
  return false;
}

bool bleTakeMessage(uint8_t *out, uint8_t &length) {
  if (!rxLength) return false;
  length = rxLength; memcpy(out, rx, length); rxLength = 0; return true;
}
bool bleConnected() { return connected && ready && !restartNeeded; }
bool bleSubscribed() { return subscribed && bleConnected(); }
bool bleReady() { return ready && !restartNeeded; }
uint8_t bleLastError() { return lastError; }
uint32_t bleRecoveries() { return recoveries; }

// No sends, rendering, parsing, serial output, or nested HCI calls in callbacks.
extern "C" void HCI_Event_CB(void *packet) {
  const hci_uart_pckt *uart = static_cast<const hci_uart_pckt *>(packet);
  if (uart->type != HCI_EVENT_PKT) return;
  const hci_event_pckt *event = reinterpret_cast<const hci_event_pckt *>(uart->data);
  if (event->evt == EVT_DISCONN_COMPLETE && event->plen >= 4) {
    connected = subscribed = false; rxLength = 0;
    advertise = true; nextAttempt = millis(); return;
  }
  if (event->evt == EVT_LE_META_EVENT && event->plen >= 1 + EVT_LE_CONN_COMPLETE_SIZE) {
    const evt_le_meta_event *meta = reinterpret_cast<const evt_le_meta_event *>(event->data);
    if (meta->subevent == EVT_LE_CONN_COMPLETE) {
      const evt_le_connection_complete *cc =
          reinterpret_cast<const evt_le_connection_complete *>(meta->data);
      if (cc->status == 0) {
        connected = true; subscribed = false; rxLength = 0; advertise = false;
      } else { advertise = true; nextAttempt = millis() + 1000; }
    }
    return;
  }
  if (event->evt != EVT_VENDOR || event->plen < 2) return;
  const evt_blue_aci *blue = reinterpret_cast<const evt_blue_aci *>(event->data);
  if (blue->ecode != EVT_BLUE_GATT_ATTRIBUTE_MODIFIED) return;
  // IDB05A1 fixed fields: conn_handle(2), attr_handle(2), data_length(1),
  // offset(2), then att_data. Vendor event code adds two more bytes.
  if (event->plen < 9) return;
  const evt_gatt_attr_modified_IDB05A1 *modified =
      reinterpret_cast<const evt_gatt_attr_modified_IDB05A1 *>(blue->data);
  const uint8_t n = modified->data_length;
  if (modified->offset != 0 || n > event->plen - 9) return;
  if (modified->attr_handle == notifyHandle + 2 && n == 2) {
    subscribed = (modified->att_data[0] & 1) != 0;
  } else if (modified->attr_handle == writeHandle + 1 && n > 0 && n <= sizeof(rx)) {
    memcpy(rx, modified->att_data, n); rxLength = n; // bounded newest-message slot
  }
}
