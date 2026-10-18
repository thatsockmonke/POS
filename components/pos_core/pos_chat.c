#include "pos_chat.h"
#include "cJSON.h"
#include "pos_appdata.h"
#include "pos_fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int pos_chat_contact(const char *root, uint32_t peer, unsigned channel,
                     pos_chat_contact_t *contact, bool save) {
  char app[40];
  snprintf(app, sizeof(app), "chat-%08x-%u", (unsigned)peer, channel);
  pos_appdata_t *data = calloc(1, sizeof(*data));
  if (!data)
    return -1;
  pos_error_t error = {0};
  int r = pos_appdata_load(root, app, 1, NULL, NULL, data, &error);
  if (r < 0) {
    free(data);
    return -1;
  }
  if (save) {
    cJSON *j = cJSON_CreateObject();
    bool good =
        j && cJSON_AddStringToObject(j, "alias", contact->alias) &&
        cJSON_AddStringToObject(j, "quick", contact->quick) &&
        cJSON_AddBoolToObject(j, "favourite", contact->favourite) &&
        cJSON_AddNumberToObject(j, "read", contact->read_offset) &&
        cJSON_PrintPreallocated(j, data->json, sizeof(data->json), false);
    cJSON_Delete(j);
    data->schema = 1;
    r = good ? pos_appdata_save(root, app, data, &error) : -1;
  } else {
    memset(contact, 0, sizeof(*contact));
    if (r == 0) {
      cJSON *j = cJSON_Parse(data->json),
            *a = cJSON_GetObjectItemCaseSensitive(j, "alias"),
            *q = cJSON_GetObjectItemCaseSensitive(j, "quick"),
            *f = cJSON_GetObjectItemCaseSensitive(j, "favourite"),
            *n = cJSON_GetObjectItemCaseSensitive(j, "read");
      bool good = cJSON_IsString(a) &&
                  strlen(a->valuestring) < sizeof(contact->alias) &&
                  cJSON_IsString(q) &&
                  strlen(q->valuestring) < sizeof(contact->quick) &&
                  cJSON_IsBool(f) && cJSON_IsNumber(n) && n->valuedouble >= 0 &&
                  n->valuedouble <= UINT32_MAX;
      if (good) {
        strcpy(contact->alias, a->valuestring);
        strcpy(contact->quick, q->valuestring);
        contact->favourite = cJSON_IsTrue(f);
        contact->read_offset = (uint32_t)n->valuedouble;
      } else
        r = -1;
      cJSON_Delete(j);
    }
  }
  free(data);
  return r < 0 ? -1 : 0;
}
static uint32_t id(cJSON *j, const char *name, bool bang) {
  cJSON *s = cJSON_GetObjectItemCaseSensitive(j, name);
  if (!cJSON_IsString(s))
    return 0;
  const char *p = s->valuestring;
  if (bang) {
    if (*p != '!')
      return 0;
    ++p;
  }
  if (strlen(p) != 8 || strspn(p, "0123456789abcdefABCDEF") != 8)
    return 0;
  return (uint32_t)strtoul(p, NULL, 16);
}
int pos_chat_read(const char *root, uint32_t self, uint32_t peer,
                  unsigned channel, pos_chat_message_t messages[16],
                  unsigned *count, uint32_t *end_offset, unsigned *unread) {
  char path[POS_FS_PATH], line[1024];
  *count = *unread = 0;
  *end_offset = 0;
  pos_chat_contact_t contact;
  if (pos_chat_contact(root, peer, channel, &contact, false))
    return -1;
  if (pos_fs_join(path, root, "POS/Users/Owner/Radio/meshtastic.jsonl"))
    return -1;
  FILE *f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT ? 0 : -1;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return -1;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return -1;
  }
  if (contact.read_offset > (uint32_t)size)
    contact.read_offset = 0;
  if (size > 65536) {
    fseek(f, size - 65536, SEEK_SET);
    while (fgets(line, sizeof(line), f) && !strchr(line, '\n')) {
    }
  } else
    rewind(f);
  while (fgets(line, sizeof(line), f)) {
    if (!strchr(line, '\n')) {
      while (fgets(line, sizeof(line), f) && !strchr(line, '\n')) {
      }
      continue;
    }
    long end = ftell(f);
    if (end >= 0)
      *end_offset = (uint32_t)end;
    cJSON *j = pos_fs_json_safe(line, strlen(line))
                   ? cJSON_ParseWithOpts(line, NULL, true)
                   : NULL,
          *e = cJSON_GetObjectItemCaseSensitive(j, "event"),
          *t = cJSON_GetObjectItemCaseSensitive(j, "text"),
          *ch = cJSON_GetObjectItemCaseSensitive(j, "channel");
    uint32_t from = id(j, "from", true), to = id(j, "to", true),
             packet = id(j, "id", false);
    bool incoming = from != self;
    bool thread = peer == UINT32_MAX ? to == UINT32_MAX && cJSON_IsNumber(ch) &&
                                           ch->valuedouble == channel
                                     : (from == peer && to == self) ||
                                           (from == self && to == peer);
    if (cJSON_IsString(e) && packet) {
      if (thread && cJSON_IsString(t) &&
          strlen(t->valuestring) <= POS_MESH_PAYLOAD &&
          (!strcmp(e->valuestring, "received") ||
           !strcmp(e->valuestring, "queued"))) {
        bool duplicate = false;
        for (unsigned i = 0; i < *count; ++i)
          if (messages[i].id == packet && messages[i].from == from)
            duplicate = true;
        if (duplicate) {
          cJSON_Delete(j);
          continue;
        }
        if (*count == 16) {
          memmove(messages, messages + 1, 15 * sizeof(*messages));
          --*count;
        }
        pos_chat_message_t *m = &messages[(*count)++];
        memset(m, 0, sizeof(*m));
        m->id = packet;
        m->from = from;
        m->to = to;
        m->incoming = incoming;
        m->unread = incoming && end > contact.read_offset;
        strcpy(m->text, t->valuestring);
        snprintf(m->status, sizeof(m->status), "%s", e->valuestring);
      } else if (!incoming) {
        for (unsigned i = 0; i < *count; ++i)
          if (messages[i].id == packet && !messages[i].incoming)
            snprintf(messages[i].status, sizeof(messages[i].status), "%.19s",
                     e->valuestring);
      }
    }
    cJSON_Delete(j);
  }
  bool bad = ferror(f);
  fclose(f);
  for (unsigned i = 0; i < *count; ++i)
    if (messages[i].unread)
      ++*unread;
  return bad ? -1 : 0;
}
