#include "pos_device.h"
#include "pos_files.h"
#include "pos_mesh_service.h"
#include "pos_network.h"
#include "pos_peripherals.h"
#include "ui.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const preset_names[] = {
    "LongFast",  "LongSlow",  "Unsupported", "MediumSlow", "MediumFast",
    "ShortSlow", "ShortFast", "LongMod",     "ShortTurbo", "LongTurbo"};
static __attribute__((noinline)) bool tx_ready(void) {
  pos_expansion_status_t s = pos_expansion_status();
  if (s.cap_detected && s.lora_enabled && s.radio_ready && s.tx_enabled)
    return true;
  pos_ui_notice("Transmit off", "Enable LoRa features and confirm antenna / "
                                "transmit in Settings > Expansion hardware. "
                                "Protocol/key changes turn TX off.");
  return false;
}
static bool integer(const char *title, unsigned *out, unsigned max) {
  char text[12];
  snprintf(text, sizeof(text), "%u", *out);
  if (!pos_ui_text(title, "Number (0 allowed)", text, sizeof(text), false))
    return false;
  char *end;
  unsigned long n = strtoul(text, &end, 10);
  if (!*text || *end || strspn(text, "0123456789") != strlen(text) || n > max) {
    pos_ui_notice("Invalid number",
                  "Use a whole number in the displayed range.");
    return false;
  }
  *out = n;
  return true;
}
static void apply(pos_mesh_config_t *c, bool enabled) {
  pos_error_t e = {0};
  int r = pos_meshtastic_configure(c, enabled, &e);
  pos_ui_notice(r ? "Not applied" : "Radio settings saved",
                r ? e.message
                  : "Transmit is OFF. Reconfirm antenna / transmit in "
                    "Expansion hardware. "
                    "Peers need the same region, modem preset, frequency slot "
                    "and channel key.");
}
static void channels(const char *root, pos_mesh_config_t *c) {
  for (;;) {
    char labels[8][45];
    const char *items[10];
    for (unsigned i = 0; i < 8; ++i) {
      snprintf(labels[i], sizeof(labels[i]), "%u %s: %s", i + 1,
               c->channels[i].enabled ? "ON" : "off",
               *c->channels[i].name ? c->channels[i].name : "preset name");
      items[i] = labels[i];
    }
    items[8] = "Import channel URL from file";
    items[9] = "Back";
    int at = pos_ui_menu("Meshtastic channels", items, 10);
    if (at < 0 || at == 9)
      return;
    if (at == 8) {
      char relative[241], full[POS_FS_PATH];
      if (!pos_ui_pick_file_from(root, "Meshtastic URL text file", "Documents",
                                 relative) ||
          !pos_fs_user_path(relative) || pos_fs_join(full, root, relative))
        continue;
      char *text = calloc(1, 2049);
      FILE *f = text ? fopen(full, "rb") : NULL;
      bool bad = !f;
      size_t n = 0;
      if (f) {
        n = fread(text, 1, 2048, f);
        bad = ferror(f) || fgetc(f) != EOF;
        fclose(f);
      }
      if (!bad) {
        while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                     text[n - 1] == ' '))
          text[--n] = 0;
        bad = memchr(text, 0, n) != NULL || pos_mesh_import_url(text, c);
      }
      if (text) {
        pos_secure_zero(text, 2049);
        free(text);
      }
      if (bad)
        pos_ui_notice(
            "Import refused",
            "Use a Meshtastic channel URL in a plain text file (up to 2 KiB). "
            "Only US/EU868 preset configurations are supported; custom RF "
            "overrides are refused. Previous settings are retained.");
      else {
        const char *confirm[] = {"Keep current channels",
                                 "Replace with imported channels"};
        if (pos_ui_menu("Import channels / radio settings?", confirm, 2) == 1)
          apply(c, pos_meshtastic_status().enabled);
        (void)pos_meshtastic_settings(c);
      }
      continue;
    }
    pos_mesh_channel_t *ch = &c->channels[at];
    const char *actions[] = {
        "Edit name",          "Set private AES key", "Use default public key",
        "Disable encryption", "Toggle enabled",      "Back"};
    int a = pos_ui_menu("Channel options", actions, 6);
    if (a < 0 || a == 5)
      continue;
    if (a == 0) {
      if (!pos_ui_text("Channel name", "Case-sensitive; empty = preset name",
                       ch->name, sizeof(ch->name), false))
        continue;
    } else if (a == 1) {
      char key[65] = "";
      if (!pos_ui_text("AES channel key", "Hex or Meshtastic Base64 key", key,
                       sizeof(key), true))
        continue;
      if (pos_mesh_key_parse(key, ch)) {
        pos_secure_zero(key, sizeof(key));
        pos_ui_notice("Invalid key",
                      "Use the same Base64 key shown by Meshtastic, or 32/64 "
                      "hex characters for AES-128/256.");
        continue;
      }
      pos_secure_zero(key, sizeof(key));
      ch->enabled = true;
    } else if (a == 2 || a == 3) {
      const char *confirm[] = {"Cancel", "Use public / unencrypted channel"};
      pos_ui_notice(
          "Channel privacy",
          a == 2 ? "Meshtastic's default AES key is public. Anyone using it "
                   "can read and forge channel messages. Private direct "
                   "messages use separate node keys."
                 : "Channel messages will be transmitted without encryption. "
                   "Private direct messages still require node keys.");
      if (pos_ui_menu("Change channel privacy?", confirm, 2) != 1)
        continue;
      memset(ch->key, 0, 32);
      ch->key_len = 1;
      ch->key[0] = a == 2 ? 1 : 0;
      ch->enabled = true;
    } else {
      if (!at) {
        pos_ui_notice("Primary channel",
                      "The primary channel must stay enabled.");
        continue;
      }
      ch->enabled = !ch->enabled;
    }
    apply(c, pos_meshtastic_status().enabled);
    (void)pos_meshtastic_settings(c);
  }
}
void pos_ui_mesh_settings(const char *root) {
  (void)root;
  pos_mesh_config_t *c = calloc(1, sizeof(*c));
  if (!c) {
    pos_ui_notice("Not enough memory", "Close this screen and retry.");
    return;
  }
  while (!pos_home_pending() && !pos_meshtastic_settings(c)) {
    pos_meshtastic_status_t s = pos_meshtastic_status();
    char mode[55], region[40], preset[40], hops[30], relay[35];
    snprintf(mode, sizeof(mode), "Protocol: %s",
             s.enabled ? "Meshtastic" : "POS radio");
    snprintf(region, sizeof(region), "Region: %s",
             c->region == POS_MESH_US ? "US (cap <=923 MHz)" : "EU_868");
    snprintf(preset, sizeof(preset), "Modem: %s", preset_names[c->preset]);
    snprintf(hops, sizeof(hops), "Hop limit: %u", c->hops);
    snprintf(relay, sizeof(relay), "Mesh relaying: %s",
             c->relay ? "on" : "off");
    const char *items[] = {mode,
                           region,
                           preset,
                           "Frequency slot (0 = automatic)",
                           hops,
                           relay,
                           "Node name",
                           "Channels / encryption",
                           "Antenna / transmit settings",
                           "Back"};
    int a = pos_ui_menu("Radio protocol", items, 10);
    if (a < 0 || a == 9)
      break;
    if (a == 8) {
      pos_ui_radio_transmit();
      continue;
    }
    if (a == 0) {
      const char *options[] = {"Meshtastic mesh", "POS radio (legacy)",
                               "Cancel"};
      int choice = pos_ui_menu("Choose protocol", options, 3);
      if (choice == 0 || choice == 1)
        apply(c, choice == 0);
      continue;
    }
    if (a == 1) {
      const char *regions[] = {"US (902-928 MHz)", "EU_868 (869.4-869.65 MHz)",
                               "Cancel"};
      int choice = pos_ui_menu("Use your actual radio region", regions, 3);
      if (choice < 0 || choice == 2)
        continue;
      c->region = choice == 0 ? POS_MESH_US : POS_MESH_EU868;
      c->slot = 0;
    } else if (a == 2) {
      const char *items[10];
      unsigned map[9], n = 0;
      for (unsigned i = 0; i < 10; ++i)
        if (i != 2) {
          items[n] = preset_names[i];
          map[n++] = i;
        }
      items[n] = "Cancel";
      int choice = pos_ui_menu("Modem preset", items, n + 1);
      if (choice < 0 || (unsigned)choice == n)
        continue;
      c->preset = map[choice];
      c->slot = 0;
    } else if (a == 3 || a == 4) {
      unsigned value = a == 3 ? c->slot : c->hops;
      if (!integer(a == 3 ? "Frequency slot (0-208)" : "Hop limit (0-7)",
                   &value, a == 3 ? 208 : 7))
        continue;
      if (a == 3)
        c->slot = value;
      else
        c->hops = value;
    } else if (a == 5)
      c->relay = !c->relay;
    else if (a == 6) {
      if (!pos_ui_text("Node name", "Name seen by Meshtastic peers",
                       c->long_name, sizeof(c->long_name), false) ||
          !pos_ui_text("Short name", "Up to 4 characters", c->short_name,
                       sizeof(c->short_name), false))
        continue;
    } else if (a == 7) {
      channels(root, c);
      continue;
    }
    apply(c, s.enabled);
  }
  pos_secure_zero(c, sizeof(*c));
  free(c);
}
static bool peer(uint32_t *id, bool allow_broadcast) {
  char value[10] = "";
  if (!pos_ui_text("Destination node",
                   allow_broadcast ? "8 hex digits; empty = channel"
                                   : "8 hex digits, optional ! prefix",
                   value, sizeof(value), false))
    return false;
  if (!*value && allow_broadcast) {
    *id = UINT32_MAX;
    return true;
  }
  char *p = value + (*value == '!');
  if (strlen(p) != 8 || strspn(p, "0123456789abcdefABCDEF") != 8 ||
      (*id = strtoul(p, NULL, 16)) < 4 || *id == UINT32_MAX) {
    pos_ui_notice("Invalid node", "Use the !xxxxxxxx node ID shown in "
                                  "Meshtastic or the Nearby nodes list.");
    return false;
  }
  return true;
}
static int channel(void) {
  pos_mesh_config_t *c = malloc(sizeof(*c));
  if (!c)
    return -1;
  if (pos_meshtastic_settings(c)) {
    free(c);
    return -1;
  }
  char names[8][35];
  const char *items[9];
  unsigned indices[8], count = 0;
  for (unsigned i = 0; i < 8; ++i)
    if (c->channels[i].enabled) {
      snprintf(names[count], sizeof(names[count]), "%u: %s", i + 1,
               *c->channels[i].name ? c->channels[i].name
                                    : preset_names[c->preset]);
      items[count] = names[count];
      indices[count++] = i;
    }
  items[count] = "Cancel";
  int a = pos_ui_menu("Choose channel", items, count + 1);
  pos_secure_zero(c, sizeof(*c));
  free(c);
  return a >= 0 && (unsigned)a < count ? (int)indices[a] : -1;
}
static __attribute__((noinline)) void nodes(void) {
  for (;;) {
    char names[32][42];
    const char *items[33];
    unsigned indices[32], count = 0;
    pos_mesh_node_t node;
    for (unsigned i = 0; i < 32; ++i)
      if (!pos_meshtastic_node(i, &node)) {
        snprintf(names[count], sizeof(names[count]), "!%08x %.20s%s",
                 (unsigned)node.id, node.long_name,
                 node.key_changed ? " KEY!" : "");
        items[count] = names[count];
        indices[count++] = i;
      }
    items[count] = "Back";
    int a = pos_ui_menu(count ? "Heard mesh nodes" : "No nodes heard yet",
                        items, count + 1);
    if (a < 0 || (unsigned)a == count)
      return;
    if (pos_meshtastic_node(indices[a], &node))
      continue;
    char text[700], key[65] = "";
    if (node.has_key)
      for (unsigned i = 0; i < 32; ++i)
        snprintf(key + i * 2, 3, "%02x", node.public_key[i]);
    snprintf(text, sizeof(text),
             "!%08x\n%s / %s\nRSSI: %d dBm SNR: %d dB\nHops: %d\nKey: "
             "%s\n%s\nLocation: %s",
             (unsigned)node.id, node.long_name, node.short_name, node.rssi,
             node.snr, node.hops,
             node.key_changed ? "CHANGED; DMs blocked"
             : node.has_key   ? "first observed (unverified)"
                              : "not discovered",
             key, node.has_position ? "shared by peer" : "not shared");
    pos_ui_notice("Mesh node / identity", text);
    const char *actions[] = {"Message this node", "Show shared coordinates",
                             "Forget old public key", "Back"};
    int action = pos_ui_menu("Node actions", actions, 4);
    pos_error_t e = {0};
    if (action == 0 && tx_ready()) {
      char message[161] = "";
      if (pos_ui_text("Private direct message", "Up to 160 characters", message,
                      sizeof(message), false)) {
        int r = pos_meshtastic_send(0, node.id, POS_MESH_TEXT, message,
                                    strlen(message), false, &e);
        pos_ui_notice(r ? "Not queued" : "Direct message queued",
                      r ? e.message
                        : "Public-key encrypted; wait for destination "
                          "acknowledgement. A send is not proof of delivery.");
      }
    } else if (action == 1) {
      if (node.has_position)
        snprintf(text, sizeof(text),
                 "Latitude: %.7f\nLongitude: %.7f\nPeer time: %u\nPosition may "
                 "be old. Signal strength does not establish distance.",
                 node.latitude / 1e7, node.longitude / 1e7,
                 (unsigned)node.position_time);
      else
        strcpy(text,
               "This peer has not shared coordinates during this session.");
      pos_ui_notice("Shared location", text);
    } else if (action == 2) {
      const char *confirm[] = {"Keep existing identity",
                               "Forget key; rediscover peer"};
      pos_ui_notice("Verify identity",
                    "Compare the full public key through another trusted "
                    "channel. Forgetting accepts the next observed key; it "
                    "does not authenticate the person.");
      if (pos_ui_menu("Forget this node key?", confirm, 2) == 1) {
        int r = pos_meshtastic_forget(node.id, &e);
        pos_ui_notice(
            r ? "Not changed" : "Key forgotten",
            r ? e.message
              : "Discover the node again before sending a private message.");
      }
    }
  }
}
void pos_ui_meshtastic(const char *root) {
  while (!pos_home_pending()) {
    pos_meshtastic_status_t s = pos_meshtastic_status();
    if (!s.enabled)
      return;
    char title[45];
    snprintf(title, sizeof(title), "Meshtastic !%08x", (unsigned)s.node);
    const char *items[] = {"Status / latest message",
                           "Channel message",
                           "Direct message",
                           "Nearby nodes",
                           "Announce / discover nodes",
                           "Share GPS location once",
                           "Recent received messages",
                           "Retry saved outbox",
                           "Radio settings",
                           "Back"};
    int a = pos_ui_menu(title, items, 10);
    if (a < 0 || a == 9)
      break;
    pos_error_t e = {0};
    if (a == 0) {
      char body[900];
      static const char *const states[] = {
          "received",        "sent (not delivery)", "relay heard",
          "destination ACK", "destination NAK",     "timed out",
          "forwarded"};
      snprintf(body, sizeof(body),
               "!%08x / %.3f MHz\nNodes: %u Queue: %u\nTX: %u RX: %u Relayed: "
               "%u\nACK: %u Failed: %u\nLast %08x: %s\n%s\n%s\n%s",
               (unsigned)s.node, s.frequency_hz / 1e6, s.nodes, s.queued,
               (unsigned)s.stats.sent, (unsigned)s.stats.received,
               (unsigned)s.stats.relayed, (unsigned)s.stats.acked,
               (unsigned)s.stats.failed, (unsigned)s.stats.last_id,
               states[s.stats.last_event], s.latest, s.error,
               "A quiet mesh does not prove there are no nearby users. Peers "
               "need matching radio settings.");
      pos_ui_notice("Meshtastic status", body);
    } else if (a == 1 || a == 2) {
      if (!tx_ready())
        continue;
      int ch = a == 1 ? channel() : 0;
      if (ch < 0)
        continue;
      uint32_t id = UINT32_MAX;
      if (a == 2 && !peer(&id, false))
        continue;
      char text[161] = "";
      if (!pos_ui_text(a == 1 ? "Channel message" : "Direct message",
                       "Up to 160 characters", text, sizeof(text), false) ||
          !*text)
        continue;
      int r = pos_meshtastic_send(ch, id, POS_MESH_TEXT, text, strlen(text),
                                  false, &e);
      pos_ui_notice(r ? "Not queued" : "Message queued",
                    r        ? e.message
                    : a == 1 ? "Channel broadcast is not individually "
                               "acknowledged. Default LongFast is public."
                             : "Public-key encrypted. Check status for "
                               "destination ACK; delivery is not guaranteed.");
    } else if (a == 3)
      nodes();
    else if (a == 4 && tx_ready()) {
      int r = pos_meshtastic_announce(true, &e);
      pos_ui_notice(r ? "Not queued" : "Discovery queued",
                    r ? e.message
                      : "Announces your node name and public key, requesting "
                        "peers to reply. GPS is not included. Give the mesh "
                        "time; no users may be in range.");
    } else if (a == 5 && tx_ready()) {
      int ch = channel();
      if (ch < 0)
        continue;
      uint32_t id;
      if (!peer(&id, true))
        continue;
      const char *confirm[] = {"Keep location private", "Share exact GPS once"};
      if (pos_ui_menu("Send precise coordinates?", confirm, 2) != 1)
        continue;
      pos_location_t g = pos_expansion_location();
      if (!g.fix) {
        pos_ui_notice("No current fix",
                      "Wait for a valid GPS fix before sharing.");
        continue;
      }
      int r = pos_meshtastic_share(
          ch, id, (int32_t)llround(g.latitude * 1e7),
          (int32_t)llround(g.longitude * 1e7),
          g.utc > 0 && (uint64_t)g.utc <= UINT32_MAX ? g.utc : 0, &e);
      pos_ui_notice(r ? "Not queued" : "Location queued",
                    r ? e.message
                      : "One location packet queued. Automatic GPS sharing and "
                        "replies to position requests remain off.");
    } else if (a == 6)
      pos_ui_mesh_recent(root);
    else if (a == 7 && tx_ready()) {
      int r = pos_meshtastic_retry(&e);
      pos_ui_notice(
          r ? "Outbox" : "Retry queued",
          r ? e.message
            : "Matching saved messages queued with their original packet IDs.");
    } else if (a == 8)
      pos_ui_mesh_settings(root);
  }
}
