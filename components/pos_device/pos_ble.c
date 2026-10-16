#include "pos_ble.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdatomic.h>
#include <string.h>

/* UUIDs: 73f5ae10/11/12-5c12-4b83-9d48-bae023504f53. */
#define UUID(last)                                                             \
  BLE_UUID128_INIT(0x53, 0x4f, 0x50, 0x23, 0xe0, 0xba, 0x48, 0x9d, 0x83, 0x4b, \
                   0x12, 0x5c, last, 0xae, 0xf5, 0x73)
static const ble_uuid128_t service_uuid = UUID(0x10), rx_uuid = UUID(0x11),
                           tx_uuid = UUID(0x12);
static SemaphoreHandle_t gate;
static QueueHandle_t incoming;
static pos_ble_status_t state;
static bool initialized, receiving_mode;
static atomic_bool closing, write_done, read_done;
static atomic_int write_error, read_error;
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE, connection_generation;
static atomic_bool reset_receive;
static uint16_t rx_value, tx_value, service_end, service_start;
static uint8_t own_address, reply[20] = {'W'}, last_read[20];
static size_t reply_size = 5, last_read_size;
static char root_path[POS_FS_PATH], device_name[25];
static struct {
  ble_addr_t address;
  pos_ble_peer_t info;
} peers[8];
static unsigned peer_count;
typedef struct {
  uint8_t bytes[160];
  uint16_t length;
  unsigned generation;
} frame_t;
static pos_share_assembly_t assembly;
static pos_share_receive_t receive_file;
static int fail(pos_error_t *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return -1;
}
static void lock(void) { xSemaphoreTake(gate, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(gate); }
static uint64_t milliseconds(void) { return esp_timer_get_time() / 1000; }
static void error_text(const char *text) {
  lock();
  snprintf(state.error, sizeof(state.error), "%s", text);
  unlock();
}
static void response(uint8_t status, uint32_t offset) {
  lock();
  reply[0] = status;
  for (unsigned i = 0; i < 4; ++i)
    reply[1 + i] = offset >> (8 * i);
  reply_size = 5;
  state.offset = offset;
  unlock();
}
static uint32_t offset_of(const uint8_t *p) {
  return p[1] | (uint32_t)p[2] << 8 | (uint32_t)p[3] << 16 |
         (uint32_t)p[4] << 24;
}
static int access_characteristic(uint16_t conn, uint16_t attribute,
                                 struct ble_gatt_access_ctxt *context,
                                 void *arg) {
  (void)conn;
  (void)attribute;
  if (atomic_load(&closing))
    return BLE_ATT_ERR_UNLIKELY;
  if ((uintptr_t)arg == 1 && context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    frame_t frame = {0};
    unsigned n = OS_MBUF_PKTLEN(context->om);
    if (!n || n > sizeof(frame.bytes) ||
        os_mbuf_copydata(context->om, 0, n, frame.bytes))
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    frame.length = n;
    frame.generation = atomic_load(&connection_generation);
    return xQueueSend(incoming, &frame, 0) == pdTRUE
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if ((uintptr_t)arg == 2 && context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    lock();
    int result = os_mbuf_append(context->om, reply, reply_size);
    unlock();
    return result ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &service_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {.uuid = &rx_uuid.u,
              .access_cb = access_characteristic,
              .arg = (void *)1,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                       BLE_GATT_CHR_F_WRITE_AUTHEN},
             {.uuid = &tx_uuid.u,
              .access_cb = access_characteristic,
              .arg = (void *)2,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                       BLE_GATT_CHR_F_READ_AUTHEN},
             {0}}},
    {0}};
static int gap(struct ble_gap_event *event, void *arg);
static void advertise(void) {
  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = (ble_uuid128_t *)&service_uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  struct ble_hs_adv_fields scan = {.name = (uint8_t *)device_name,
                                   .name_len = strlen(device_name),
                                   .name_is_complete = 1};
  struct ble_gap_adv_params parameters = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                          .disc_mode = BLE_GAP_DISC_MODE_GEN};
  if (ble_gap_adv_set_fields(&fields) || ble_gap_adv_rsp_set_fields(&scan) ||
      ble_gap_adv_start(own_address, NULL, BLE_HS_FOREVER, &parameters, gap,
                        NULL))
    error_text("BLE advertising failed.");
}
static int characteristic(uint16_t conn, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg) {
  (void)arg;
  if (!error->status && chr) {
    if (!ble_uuid_cmp(&chr->uuid.u, &rx_uuid.u))
      rx_value = chr->val_handle;
    if (!ble_uuid_cmp(&chr->uuid.u, &tx_uuid.u))
      tx_value = chr->val_handle;
  } else if (error->status == BLE_HS_EDONE) {
    if (rx_value && tx_value) {
      if (ble_gap_security_initiate(conn))
        error_text("Cannot start secure pairing.");
    } else
      error_text("Peer has no POS transfer characteristics.");
  } else
    error_text("BLE characteristic discovery failed.");
  return 0;
}
static int service_found(uint16_t conn, const struct ble_gatt_error *error,
                         const struct ble_gatt_svc *svc, void *arg) {
  (void)arg;
  if (!error->status && svc) {
    service_start = svc->start_handle;
    service_end = svc->end_handle;
  } else if (error->status == BLE_HS_EDONE) {
    if (service_start)
      ble_gattc_disc_all_chrs(conn, service_start, service_end, characteristic,
                              NULL);
    else
      error_text("POS transfer service not found.");
  } else
    error_text("BLE service discovery failed.");
  return 0;
}
static int exchanged_mtu(uint16_t conn, const struct ble_gatt_error *error,
                         uint16_t mtu, void *arg) {
  (void)error;
  (void)mtu;
  (void)arg;
  return ble_gattc_disc_svc_by_uuid(conn, &service_uuid.u, service_found, NULL);
}
static int gap(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (!event->connect.status) {
      connection = event->connect.conn_handle;
      atomic_fetch_add(&connection_generation, 1);
      atomic_store(&reset_receive, true);
      lock();
      state.connected = true;
      state.secure = false;
      state.complete = false;
      state.offered = false;
      state.receiving = false;
      unlock();
      if (!receiving_mode) {
        service_start = service_end = rx_value = tx_value = 0;
        if (ble_gattc_exchange_mtu(connection, exchanged_mtu, NULL))
          ble_gattc_disc_svc_by_uuid(connection, &service_uuid.u, service_found,
                                     NULL);
      }
    } else
      error_text("Connection failed; select the peer and retry.");
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    connection = BLE_HS_CONN_HANDLE_NONE;
    atomic_fetch_add(&connection_generation, 1);
    atomic_store(&reset_receive, true);
    lock();
    state.connected = false;
    state.secure = false;
    state.pairing = false;
    unlock();
    if (receiving_mode && !atomic_load(&closing))
      advertise();
    break;
  case BLE_GAP_EVENT_ENC_CHANGE: {
    struct ble_gap_conn_desc description;
    bool secure =
        !event->enc_change.status &&
        !ble_gap_conn_find(event->enc_change.conn_handle, &description) &&
        description.sec_state.encrypted && description.sec_state.authenticated;
    lock();
    state.secure = secure;
    state.pairing = false;
    unlock();
    if (!secure)
      error_text("Authenticated encryption required; pairing failed.");
    break;
  }
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
      lock();
      state.passkey = event->passkey.params.numcmp;
      state.pairing = true;
      unlock();
    } else {
      error_text("Peer must support numeric-comparison pairing.");
      ble_gap_terminate(connection, BLE_ERR_AUTH_FAIL);
    }
    break;
  case BLE_GAP_EVENT_DISC: {
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                event->disc.length_data))
      break;
    bool compatible = false;
    for (unsigned i = 0; i < fields.num_uuids128; ++i)
      if (!ble_uuid_cmp(&fields.uuids128[i].u, &service_uuid.u))
        compatible = true;
    lock();
    unsigned index = peer_count;
    for (unsigned i = 0; i < peer_count; ++i)
      if (!memcmp(&peers[i].address, &event->disc.addr, sizeof(ble_addr_t)))
        index = i;
    if (index < peer_count || (compatible && index < 8)) {
      if (index == peer_count) {
        peers[index].address = event->disc.addr;
        snprintf(peers[index].info.name, 25, "POS peer %u", ++peer_count);
      }
      peers[index].info.rssi = event->disc.rssi;
      if (fields.name && fields.name_len)
        snprintf(peers[index].info.name, 25, "%.*s", fields.name_len,
                 fields.name);
    }
    unlock();
    break;
  }
  case BLE_GAP_EVENT_DISC_COMPLETE:
    lock();
    state.scanning = false;
    unlock();
    break;
  default:
    break;
  }
  return 0;
}
static void synchronized(void) {
  if (ble_hs_id_infer_auto(0, &own_address)) {
    error_text("BLE address unavailable.");
    return;
  }
  lock();
  state.ready = true;
  unlock();
  if (receiving_mode)
    advertise();
}
static void host_task(void *unused) {
  (void)unused;
  nimble_port_run();
  nimble_port_freertos_deinit();
}
int pos_ble_start(const char *root, const char *name, bool receiver,
                  pos_error_t *e) {
  if (initialized)
    return fail(e, "A previous BLE session is still active. Restart POS before "
                   "trying again.");
  if (strlen(root) >= sizeof(root_path))
    return -1;
  gate = xSemaphoreCreateMutex();
  incoming = xQueueCreate(8, sizeof(frame_t));
  if (!gate || !incoming) {
    if (gate)
      vSemaphoreDelete(gate);
    if (incoming)
      vQueueDelete(incoming);
    gate = NULL;
    incoming = NULL;
    return fail(e, "Not enough memory for BLE.");
  }
  memset(&state, 0, sizeof(state));
  memset(&receive_file, 0, sizeof(receive_file));
  peer_count = 0;
  memset(&assembly, 0, sizeof(assembly));
  atomic_store(&reset_receive, false);
  strcpy(root_path, root);
  snprintf(device_name, sizeof(device_name), "%s", name);
  receiving_mode = receiver;
  connection = BLE_HS_CONN_HANDLE_NONE;
  atomic_store(&closing, false);
  reply[0] = 'W';
  reply_size = 5;
  if (nimble_port_init() != ESP_OK) {
    vQueueDelete(incoming);
    vSemaphoreDelete(gate);
    gate = NULL;
    incoming = NULL;
    return fail(e, "BLE controller could not start; check free heap.");
  }
  initialized = true;
  ble_hs_cfg.sync_cb = synchronized;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set(device_name);
  ble_att_set_preferred_mtu(185);
  if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) {
    nimble_port_deinit();
    initialized = false;
    vQueueDelete(incoming);
    vSemaphoreDelete(gate);
    gate = NULL;
    incoming = NULL;
    return fail(e, "Cannot register BLE transfer service.");
  }
  nimble_port_freertos_init(host_task);
  return 0;
}
void pos_ble_stop(void) {
  if (!initialized)
    return;
  atomic_store(&closing, true);
  pos_share_cancel(&receive_file);
  if (connection != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
  ble_gap_disc_cancel();
  ble_gap_conn_cancel();
  ble_gap_adv_stop();
  if (nimble_port_stop() != 0) {
    error_text(
        "BLE shutdown failed; restart POS. Session resources retained safely.");
    return;
  }
  if (nimble_port_deinit() != ESP_OK) {
    error_text("BLE deinitialization failed; restart POS.");
    return;
  }
  initialized = false;
  vQueueDelete(incoming);
  vSemaphoreDelete(gate);
  incoming = NULL;
  gate = NULL;
  memset(&state, 0, sizeof(state));
}
pos_ble_status_t pos_ble_status(void) {
  pos_ble_status_t result = {0};
  if (gate) {
    lock();
    result = state;
    unlock();
  }
  return result;
}
int pos_ble_scan(pos_error_t *e) {
  if (!initialized || !pos_ble_status().ready)
    return fail(e, "BLE is still starting.");
  lock();
  peer_count = 0;
  state.scanning = true;
  unlock();
  struct ble_gap_disc_params parameters = {
      .passive = 0, .itvl = 0x30, .window = 0x30, .filter_duplicates = 1};
  return ble_gap_disc(own_address, 5000, &parameters, gap, NULL)
             ? fail(e, "BLE scan failed.")
             : 0;
}
unsigned pos_ble_peers(pos_ble_peer_t result[8]) {
  lock();
  unsigned count = peer_count;
  for (unsigned i = 0; i < count; ++i)
    result[i] = peers[i].info;
  unlock();
  return count;
}
int pos_ble_connect(unsigned peer, pos_error_t *e) {
  ble_addr_t address;
  lock();
  bool valid = peer < peer_count;
  if (valid)
    address = peers[peer].address;
  unlock();
  if (!valid)
    return -1;
  ble_gap_disc_cancel();
  return ble_gap_connect(own_address, &address, 15000, NULL, gap, NULL)
             ? fail(e, "Cannot connect to peer.")
             : 0;
}
int pos_ble_pair(bool accept, pos_error_t *e) {
  struct ble_sm_io action = {.action = BLE_SM_IOACT_NUMCMP,
                             .numcmp_accept = accept};
  lock();
  state.pairing = false;
  unlock();
  return ble_sm_inject_io(connection, &action)
             ? fail(e, "Pairing confirmation failed.")
             : 0;
}
typedef struct {
  pos_io_progress_t callback;
  void *ctx;
  unsigned generation;
} verify_context_t;
static int verify_connected(void *opaque, const char *phase, uint64_t done,
                            uint64_t total) {
  verify_context_t *v = opaque;
  if (!pos_ble_status().connected ||
      atomic_load(&connection_generation) != v->generation)
    return -1;
  return v->callback ? v->callback(v->ctx, phase, done, total) : 0;
}
void pos_ble_poll(void) { pos_ble_poll_progress(NULL, NULL); }
void pos_ble_poll_progress(pos_io_progress_t progress, void *ctx) {
  if (!initialized || !receiving_mode)
    return;
  if (atomic_exchange(&reset_receive, false)) {
    pos_share_cancel(&receive_file);
    memset(&assembly, 0, sizeof(assembly));
  }
  pos_ble_status_t current = pos_ble_status();
  if (!current.connected) {
    pos_share_cancel(&receive_file);
    return;
  }
  if (!current.secure)
    return;
  verify_context_t verification = {progress, ctx,
                                   atomic_load(&connection_generation)};
  frame_t frame;
  pos_error_t e = {0};
  while (xQueueReceive(incoming, &frame, 0) == pdTRUE) {
    if (frame.generation != atomic_load(&connection_generation))
      continue;
    const uint8_t *p = frame.bytes;
    if (p[0] == 'O' && frame.length >= 3 && frame.length <= 20 &&
        !receive_file.file) {
      pos_share_offer_t offer;
      int result = pos_share_assemble(&assembly, p, frame.length, &offer);
      if (result < 0) {
        response('E', 0);
        continue;
      }
      if (result == 1) {
        lock();
        state.offer = offer;
        state.offered = true;
        unlock();
        response('P', 0);
      } else
        response('M', assembly.sequence);
    } else if (p[0] == 'D' && frame.length > 5 && receive_file.file) {
      if (pos_share_write(&receive_file, offset_of(p), p + 5, frame.length - 5,
                          &e)) {
        error_text(e.message);
        response('E', receive_file.offset);
      } else
        response('K', receive_file.offset);
    } else if (p[0] == 'F' && frame.length == 1 && receive_file.file) {
      response('V', receive_file.offset);
      if (pos_share_finish(&receive_file, verify_connected, &verification,
                           &e)) {
        error_text(e.message);
        response('E', receive_file.offset);
      } else {
        lock();
        state.complete = true;
        state.receiving = false;
        unlock();
        response('Z', receive_file.offset);
      }
    } else if (p[0] == 'C') {
      pos_share_cancel(&receive_file);
      lock();
      state.receiving = false;
      state.offered = false;
      unlock();
      response('E', receive_file.offset);
    } else
      response('E', receive_file.offset);
  }
}
int pos_ble_accept(bool accept, pos_error_t *e) {
  return pos_ble_accept_progress(accept, NULL, NULL, e);
}
int pos_ble_accept_progress(bool accept, pos_io_progress_t progress, void *ctx,
                            pos_error_t *e) {
  pos_ble_status_t s = pos_ble_status();
  if (!s.offered || !s.secure)
    return fail(e, "No authenticated file offer.");
  lock();
  state.offered = false;
  unlock();
  if (!accept) {
    response('E', 0);
    return 0;
  }
  verify_context_t verification = {progress, ctx,
                                   atomic_load(&connection_generation)};
  int result = pos_share_accept_progress(&receive_file, root_path, &s.offer,
                                         verify_connected, &verification, e);
  if (result < 0) {
    response('E', 0);
    return -1;
  }
  lock();
  state.receiving = result == 0;
  state.complete = result == 1;
  unlock();
  response(result == 1 ? 'Z' : 'A', receive_file.offset);
  return 0;
}
static int wrote(uint16_t conn, const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attribute, void *arg) {
  (void)conn;
  (void)attribute;
  (void)arg;
  atomic_store(&write_error, error->status);
  atomic_store(&write_done, true);
  return 0;
}
static int read_value(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attribute, void *arg) {
  (void)conn;
  (void)arg;
  int status = error->status;
  if (!status && attribute) {
    size_t length = OS_MBUF_PKTLEN(attribute->om);
    if (length > sizeof(last_read) ||
        os_mbuf_copydata(attribute->om, 0, length, last_read))
      status = -1;
    else
      last_read_size = length;
  }
  atomic_store(&read_error, status);
  atomic_store(&read_done, true);
  return 0;
}
static bool cancelled(pos_io_progress_t p, void *ctx, const char *phase,
                      uint32_t done, uint32_t total) {
  return !pos_ble_status().connected || (p && p(ctx, phase, done, total));
}
static int write_frame(const uint8_t *data, size_t length, pos_io_progress_t p,
                       void *ctx, uint32_t done, uint32_t total) {
  atomic_store(&write_done, false);
  atomic_store(&write_error, 0);
  if (ble_gattc_write_flat(connection, rx_value, data, length, wrote, NULL))
    return -1;
  uint64_t until = milliseconds() + 15000;
  while (!atomic_load(&write_done)) {
    if (milliseconds() > until ||
        cancelled(p, ctx, "Sending BLE file", done, total))
      return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return atomic_load(&write_error) ? -1 : 0;
}
static int read_response(pos_io_progress_t p, void *ctx, uint32_t done,
                         uint32_t total) {
  atomic_store(&read_done, false);
  atomic_store(&read_error, 0);
  last_read_size = 0;
  if (ble_gattc_read(connection, tx_value, read_value, NULL))
    return -1;
  uint64_t until = milliseconds() + 15000;
  while (!atomic_load(&read_done)) {
    if (milliseconds() > until ||
        cancelled(p, ctx, "Waiting for BLE peer", done, total))
      return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return atomic_load(&read_error) || last_read_size != 5 ? -1 : 0;
}
static int wait_response(char expected, uint32_t offset,
                         pos_io_progress_t progress, void *ctx,
                         uint32_t total) {
  uint64_t until = milliseconds() + 300000;
  while (milliseconds() < until) {
    if (read_response(progress, ctx, offset, total))
      return -1;
    if (last_read[0] == 'E')
      return -1;
    if (last_read[0] == expected && offset_of(last_read) == offset)
      return 0;
    if (cancelled(progress, ctx,
                  last_read[0] == 'P' ? "Peer must accept file"
                                      : "Waiting for BLE acknowledgement",
                  offset, total))
      return -1;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return -1;
}
int pos_ble_send(const char *relative, pos_io_progress_t progress, void *ctx,
                 pos_error_t *e) {
  if (!pos_ble_status().secure || !rx_value || !tx_value)
    return fail(e, "Connect and confirm matching pairing numbers first.");
  pos_share_offer_t offer;
  verify_context_t preparation = {progress, ctx,
                                  atomic_load(&connection_generation)};
  if (pos_share_offer_progress(root_path, relative, &offer, verify_connected,
                               &preparation, e))
    return -1;
  if (offer.size > INT32_MAX)
    return fail(
        e,
        "BLE resume supports files below 2 GiB. Use Wi-Fi for larger files.");
  uint8_t wire[POS_SHARE_OFFER_BYTES];
  pos_share_encode(&offer, wire);
  for (unsigned at = 0, sequence = 0; at < sizeof(wire); ++sequence) {
    uint8_t frame[20] = {'O', sequence};
    unsigned n = sizeof(wire) - at;
    if (n > 18)
      n = 18;
    memcpy(frame + 2, wire + at, n);
    if (write_frame(frame, n + 2, progress, ctx, 0, offer.size))
      return fail(
          e, "BLE offer could not be sent. Receiver partial files retained.");
    at += n;
    if (at < sizeof(wire) &&
        wait_response('M', sequence + 1, progress, ctx, offer.size))
      return fail(e, "BLE offer was not acknowledged.");
  }
  uint64_t until = milliseconds() + 300000;
  bool accepted = false;
  uint32_t offset = 0;
  while (milliseconds() < until) {
    if (read_response(progress, ctx, 0, offer.size))
      break;
    if (last_read[0] == 'Z' && offset_of(last_read) == offer.size)
      return 0;
    if (last_read[0] == 'A') {
      offset = offset_of(last_read);
      accepted = offset <= offer.size;
      break;
    }
    if (last_read[0] == 'E' ||
        cancelled(progress, ctx, "Waiting for file acceptance", 0, offer.size))
      break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!accepted)
    return fail(e, "File offer declined, timed out or cancelled.");
  char path[POS_FS_PATH];
  pos_fs_join(path, root_path, relative);
  FILE *f = fopen(path, "rb");
  if (!f || offset > INT32_MAX || fseek(f, offset, SEEK_SET)) {
    if (f)
      fclose(f);
    return fail(e, "Cannot read/resume source file.");
  }
  unsigned mtu = ble_att_mtu(connection);
  unsigned chunk = mtu > 8 ? mtu - 8 : 15;
  if (chunk > 128)
    chunk = 128;
  int result = 0;
  while (offset < offer.size) {
    uint8_t frame[133] = {'D'};
    for (unsigned i = 0; i < 4; ++i)
      frame[i + 1] = offset >> (8 * i);
    size_t n = offer.size - offset;
    if (n > chunk)
      n = chunk;
    if (fread(frame + 5, 1, n, f) != n) {
      result = -1;
      break;
    }
    bool acknowledged = false;
    for (unsigned attempt = 0; attempt < 3 && !acknowledged; ++attempt)
      if (!write_frame(frame, n + 5, progress, ctx, offset, offer.size) &&
          !wait_response('K', offset + n, progress, ctx, offer.size))
        acknowledged = true;
    if (!acknowledged) {
      result = -1;
      break;
    }
    offset += n;
  }
  fclose(f);
  if (!result) {
    uint8_t finish = 'F';
    if (write_frame(&finish, 1, progress, ctx, offset, offer.size) ||
        wait_response('Z', offset, progress, ctx, offer.size))
      result = -1;
  }
  return result ? fail(e, "BLE transfer incomplete. Retry the same file to "
                          "resume the receiver's partial copy.")
                : 0;
}
