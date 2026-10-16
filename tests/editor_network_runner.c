#include "cJSON.h"
#include "pos_editor.h"
#include "pos_network.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  FILE *f;
  long fail_after;
  long total;
} stream_t;
static int stream_read(void *opaque, void *buffer, size_t capacity) {
  stream_t *s = opaque;
  if (s->fail_after >= 0 && s->total >= s->fail_after)
    return -1;
  int n = fread(buffer, 1, capacity, s->f);
  s->total += n;
  return ferror(s->f) ? -1 : n;
}
static int cancelled(void *ctx, const char *phase, uint64_t done,
                     uint64_t total) {
  (void)phase;
  (void)total;
  return done >= (uint64_t)*(long *)ctx;
}
static cJSON *read_json(const char *path) {
  FILE *f = fopen(path, "rb");
  assert(f);
  assert(!fseek(f, 0, SEEK_END));
  long length = ftell(f);
  assert(length >= 0 && length < 1000000);
  rewind(f);
  char *text = calloc(1, length + 1);
  assert(text);
  assert(fread(text, 1, length, f) == (size_t)length);
  fclose(f);
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
  free(text);
  assert(j);
  return j;
}
static int operations(pos_editor_t *e, const char *path, pos_error_t *error) {
  cJSON *ops = read_json(path), *op;
  int result = 0;
  cJSON_ArrayForEach(op, ops) {
    cJSON *action = cJSON_GetObjectItemCaseSensitive(op, "action");
    if (cJSON_IsString(action)) {
      if (!strcmp(action->valuestring, "undo") ||
          !strcmp(action->valuestring, "redo")) {
        if (pos_editor_undo(e, !strcmp(action->valuestring, "redo"), error)) {
          result = -1;
          break;
        }
        continue;
      }
      if (!strcmp(action->valuestring, "copy")) {
        cJSON *start = cJSON_GetObjectItemCaseSensitive(op, "start"),
              *end = cJSON_GetObjectItemCaseSensitive(op, "end");
        assert(cJSON_IsNumber(start) && cJSON_IsNumber(end));
        if (pos_editor_copy_selection(e, start->valuedouble, end->valuedouble,
                                      NULL, NULL, error)) {
          result = -1;
          break;
        }
        continue;
      }
      if (!strcmp(action->valuestring, "paste")) {
        cJSON *at = cJSON_GetObjectItemCaseSensitive(op, "at"),
              *remove = cJSON_GetObjectItemCaseSensitive(op, "remove");
        assert(cJSON_IsNumber(at) && cJSON_IsNumber(remove));
        e->cursor = at->valuedouble;
        if (pos_editor_paste(e, remove->valuedouble, NULL, NULL, error)) {
          result = -1;
          break;
        }
        continue;
      }
    }
    cJSON *at = cJSON_GetObjectItemCaseSensitive(op, "at"),
          *remove = cJSON_GetObjectItemCaseSensitive(op, "remove"),
          *text = cJSON_GetObjectItemCaseSensitive(op, "text");
    assert(cJSON_IsNumber(at) && cJSON_IsNumber(remove) &&
           cJSON_IsString(text));
    e->cursor = at->valuedouble;
    if (pos_editor_replace(e, remove->valuedouble, text->valuestring,
                           strlen(text->valuestring), error)) {
      result = -1;
      break;
    }
  }
  cJSON_Delete(ops);
  return result;
}
int main(int argc, char **argv) {
  if (argc < 3)
    return 2;
  const char *mode = argv[1], *root = argv[2];
  pos_error_t error = {0};
  if (!strcmp(mode, "vault")) {
    pos_wifi_profiles_t profiles = {0}, decoded = {0};
    profiles.count = 4;
    for (unsigned i = 0; i < 4; ++i) {
      snprintf(profiles.networks[i].ssid, 33, "Network %u", i);
      strcpy(profiles.networks[i].password, "secret-network-password");
    }
    uint8_t salt[16], nonce[12], blob[POS_WIFI_VAULT_BYTES];
    for (unsigned i = 0; i < 16; ++i)
      salt[i] = i;
    for (unsigned i = 0; i < 12; ++i)
      nonce[i] = 32 + i;
    assert(!pos_wifi_vault_encrypt(&profiles, "correct horse battery staple",
                                   salt, nonce, blob));
    assert(
        !pos_wifi_vault_unlock(blob, "correct horse battery staple", &decoded));
    assert(!memcmp(&profiles, &decoded, sizeof(profiles)));
    assert(pos_wifi_vault_unlock(blob, "wrong password with length", &decoded));
    const unsigned corrupt[] = {0, 8, 24, 36, 100, 446};
    for (unsigned i = 0; i < 6; ++i) {
      blob[corrupt[i]] ^= 1;
      assert(pos_wifi_vault_unlock(blob, "correct horse battery staple",
                                   &decoded));
      blob[corrupt[i]] ^= 1;
    }
    assert(pos_wifi_vault_encrypt(&profiles, "too short", salt, nonce, blob));
    char output[POS_FS_PATH];
    pos_fs_join(output, root, "vault.bin");
    assert(!pos_fs_write_new(output, blob, sizeof(blob)));
    puts("vault roundtrip, tamper rejection and password validation passed");
    return 0;
  }
  if (!strcmp(mode, "policy")) {
    assert(pos_https_url_valid("https://example.com/a?q=1"));
    assert(pos_https_url_valid(
        "https://example.com/user@host?q=email@example.com"));
    const char *urls[] = {"http://example.com",
                          "https://",
                          "https:///file",
                          "https://user:pass@example.com",
                          "https://example.com/\r\nHeader: x",
                          "https://example.com\\evil"};
    for (unsigned i = 0; i < 6; ++i)
      assert(!pos_https_url_valid(urls[i]));
    assert(pos_transfer_name_valid("my notes.txt"));
    const char *names[] = {"../escape",  "a/b",  "CON.txt", ".secret",
                           "bad.part-0", "a\\b", "x\r\n"};
    for (unsigned i = 0; i < 7; ++i)
      assert(!pos_transfer_name_valid(names[i]));
    return 0;
  }
  if (!strcmp(mode, "receive")) {
    assert(argc == 9);
    FILE *f = fopen(argv[4], "rb");
    assert(f);
    stream_t stream = {f, strtol(argv[7], NULL, 10), 0};
    long cancel = strtol(argv[8], NULL, 10);
    int result = pos_receive_file(
        root, argv[3],
        !strcmp(argv[5], "unknown") ? UINT64_MAX : strtoull(argv[5], NULL, 10),
        !strcmp(argv[6], "-") ? NULL : argv[6], stream_read, &stream,
        cancel < 0 ? NULL : cancelled, &cancel, &error);
    fclose(f);
    if (result)
      fprintf(stderr, "%s\n", error.message);
    return result ? 1 : 0;
  }
  pos_editor_t *e = calloc(1, sizeof(*e));
  assert(e);
  int result = 0;
  if (!strcmp(mode, "open")) {
    assert(argc == 5);
    result = pos_editor_open(e, root, argv[3], NULL, NULL, &error);
    if (!result)
      result = operations(e, argv[4], &error);
  } else {
    assert(argc >= 4);
    result = pos_editor_recover(e, root, argv[3], &error);
    if (!result && !strcmp(mode, "edit")) {
      assert(argc == 5);
      result = operations(e, argv[4], &error);
    }
    if (!result && !strcmp(mode, "save")) {
      assert(argc == 5);
      result = pos_editor_save(e, argv[4], !strcmp(argv[4], e->source), NULL,
                               NULL, &error);
    }
    if (!result && !strcmp(mode, "read")) {
      assert(argc == 6);
      uint32_t offset = strtoul(argv[4], NULL, 10);
      size_t capacity = strtoul(argv[5], NULL, 10);
      assert(capacity <= 8192);
      unsigned char buffer[8192];
      size_t n;
      result = pos_editor_read(e, offset, buffer, capacity, &n);
      if (!result)
        assert(fwrite(buffer, 1, n, stdout) == n);
      free(e);
      return result ? 1 : 0;
    }
    if (!result && (!strcmp(mode, "find") || !strcmp(mode, "line"))) {
      uint32_t at;
      result =
          !strcmp(mode, "find")
              ? pos_editor_find(e, argv[4], strtoul(argv[5], NULL, 10), &at,
                                NULL, NULL)
              : pos_editor_line(e, strtoul(argv[4], NULL, 10), &at, NULL, NULL);
      if (!result)
        printf("%u\n", (unsigned)at);
      free(e);
      return result ? 1 : 0;
    }
    if (!result && !strcmp(mode, "discard"))
      pos_editor_discard(e);
  }
  if (result)
    fprintf(stderr, "%s\n", error.message);
  else
    printf("%s\n", *e->directory ? e->directory + strlen(root) + 1 : "");
  free(e);
  return result ? 1 : 0;
}
