#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Meshtastic LoRa v2 wire protocol. No phone, MQTT or remote-admin server. */
#define POS_MESH_WIRE 255
#define POS_MESH_PAYLOAD 233
#define POS_MESH_CHANNELS 8
#define POS_MESH_NODES 32
#define POS_MESH_QUEUE 12
#define POS_MESH_BROADCAST UINT32_MAX
enum {
  POS_MESH_TEXT = 1,
  POS_MESH_POSITION = 3,
  POS_MESH_USER = 4,
  POS_MESH_ROUTING = 5,
  POS_MESH_REPLY = 32
};
enum {
  POS_MESH_LONG_FAST,
  POS_MESH_LONG_SLOW,
  POS_MESH_VERY_LONG_SLOW,
  POS_MESH_MEDIUM_SLOW,
  POS_MESH_MEDIUM_FAST,
  POS_MESH_SHORT_SLOW,
  POS_MESH_SHORT_FAST,
  POS_MESH_LONG_MODERATE,
  POS_MESH_SHORT_TURBO,
  POS_MESH_LONG_TURBO
};
enum { POS_MESH_US = 1, POS_MESH_EU868 = 3 };
typedef struct {
  bool enabled;
  char name[12];
  uint8_t key[32], key_len; /* 0, 1 (public preset), 16 or 32 bytes */
} pos_mesh_channel_t;
typedef struct {
  uint8_t region, preset, hops;
  uint16_t slot; /* 0 = primary-name hash, otherwise 1-based physical slot */
  bool relay;
  char long_name[40], short_name[5];
  pos_mesh_channel_t channels[POS_MESH_CHANNELS];
} pos_mesh_config_t;
typedef struct {
  uint32_t frequency_hz, bandwidth_hz;
  uint8_t sf, cr, ldro;
} pos_mesh_modem_t;
typedef struct {
  uint32_t to, from, id;
  uint8_t flags, channel, next_hop, relay;
} pos_mesh_header_t;
typedef struct {
  unsigned port;
  uint8_t payload[POS_MESH_PAYLOAD];
  size_t length;
  bool want_response;
  uint32_t request_id, reply_id;
} pos_mesh_data_t;
typedef struct {
  uint32_t id;
  char long_name[40], short_name[5];
  uint8_t public_key[32];
  bool has_key, key_changed, has_position;
  int32_t latitude, longitude;
  uint64_t seen_ms;
  uint32_t position_time;
  int16_t rssi;
  int8_t snr, hops;
} pos_mesh_node_t;
typedef enum {
  POS_MESH_RX,
  POS_MESH_SENT,
  POS_MESH_RELAYED,
  POS_MESH_ACKED,
  POS_MESH_NAK,
  POS_MESH_TIMEOUT,
  POS_MESH_FORWARDED
} pos_mesh_event_t;
typedef struct {
  pos_mesh_header_t header;
  pos_mesh_data_t data;
  uint8_t channel_index;
  bool pki;
} pos_mesh_message_t;
typedef struct {
  uint8_t wire[POS_MESH_WIRE], length, attempts, priority;
  bool used, own, reliable;
  uint64_t due_ms, expires_ms;
} pos_mesh_tx_t;
typedef struct {
  uint32_t from, id;
  uint64_t at_ms;
  uint8_t hops;
} pos_mesh_seen_t;
typedef struct {
  uint32_t sent, received, relayed, duplicates, acked, failed, rejected,
      queue_full;
  uint32_t last_id;
  pos_mesh_event_t last_event;
} pos_mesh_stats_t;
/* Conservative 36 seconds per rolling hour, including TX failures and relays.
 * 61 minute buckets never discard airtime before it is a full hour old. */
typedef struct {
  uint32_t milliseconds[61];
  uint64_t minute;
} pos_mesh_airtime_t;
bool pos_mesh_airtime_allow(pos_mesh_airtime_t *, uint64_t now_ms,
                            uint32_t airtime_ms);
void pos_mesh_airtime_charge(pos_mesh_airtime_t *, uint64_t now_ms,
                             uint32_t airtime_ms);
/* RX must return 0 only when a delivered text has been safely retained.
 * Its failure suppresses ACK and duplicate marking so a retry can succeed. */
typedef int (*pos_mesh_sink_t)(void *, pos_mesh_event_t,
                               const pos_mesh_message_t *);
typedef struct {
  uint32_t node;
  pos_mesh_config_t config;
  uint8_t private_key[32], public_key[32];
  pos_mesh_node_t nodes[POS_MESH_NODES];
  pos_mesh_tx_t queue[POS_MESH_QUEUE];
  pos_mesh_seen_t seen[128];
  unsigned seen_next;
  int active_tx;
  pos_mesh_stats_t stats;
  uint64_t last_reply_ms;
  void *context;
  uint32_t (*next_id)(void *), (*random)(void *);
  pos_mesh_sink_t sink;
} pos_mesh_t;
void pos_mesh_defaults(pos_mesh_config_t *);
int pos_mesh_config_valid(const pos_mesh_config_t *);
/* Strict hex/Base64 PSK and Meshtastic ChannelSet URL import. Never enables
 * transmission or automatic location sharing; unsupported radio overrides fail.
 */
int pos_mesh_key_parse(const char *, pos_mesh_channel_t *);
int pos_mesh_import_url(const char *, pos_mesh_config_t *);
int pos_mesh_modem(const pos_mesh_config_t *, pos_mesh_modem_t *);
uint8_t pos_mesh_channel_hash(const pos_mesh_config_t *, unsigned);
int pos_mesh_key(const pos_mesh_config_t *, unsigned, uint8_t key[32]);
int pos_mesh_header(const void *, size_t, pos_mesh_header_t *);
size_t pos_mesh_data_encode(const pos_mesh_data_t *, uint8_t *, size_t);
int pos_mesh_data_decode(const void *, size_t, pos_mesh_data_t *);
int pos_mesh_ctr(const uint8_t *, size_t, uint32_t from, uint32_t id, uint8_t *,
                 size_t);
int pos_mesh_public(const uint8_t private_key[32], uint8_t public_key[32]);
int pos_mesh_pki(bool encrypt, const uint8_t private_key[32],
                 const uint8_t peer_key[32], uint32_t from, uint32_t id,
                 uint32_t extra_nonce, const uint8_t *, size_t, uint8_t *);
int pos_mesh_init(pos_mesh_t *, uint32_t node, const pos_mesh_config_t *,
                  const uint8_t private_key[32]);
/* Encode/decode are available for independent protocol interoperability tests.
 */
size_t pos_mesh_encode(pos_mesh_t *, const pos_mesh_message_t *, uint8_t *);
int pos_mesh_decode(pos_mesh_t *, const void *, size_t, pos_mesh_message_t *);
int pos_mesh_send(pos_mesh_t *, unsigned channel, uint32_t to, unsigned port,
                  const void *, size_t, bool want_response, uint64_t now);
int pos_mesh_announce(pos_mesh_t *, uint32_t to, bool discover, uint64_t now);
int pos_mesh_position(pos_mesh_t *, unsigned channel, uint32_t to, int32_t lat,
                      int32_t lon, uint32_t utc, uint64_t now);
int pos_mesh_receive(pos_mesh_t *, const void *, size_t, int rssi, int snr,
                     uint64_t now);
/* Peek is non-destructive; report completion only after the actual radio TX.
 * Busy channel leaves the item pending. -1 reports queue expiry, 0 idle. */
int pos_mesh_due(pos_mesh_t *, uint64_t now, const uint8_t **wire, size_t *);
void pos_mesh_tx_result(pos_mesh_t *, bool success, uint64_t now);
void pos_mesh_cancel(pos_mesh_t *);
