#include "pos_network.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#else
#include <openssl/evp.h>
#endif

void pos_secure_zero(void *data, size_t size) {
  volatile unsigned char *p = data;
  while (size--)
    *p++ = 0;
}
bool pos_wifi_profile_valid(const pos_wifi_profile_t *p) {
  if (!p || !memchr(p->ssid, 0, sizeof(p->ssid)) || !*p->ssid ||
      !memchr(p->password, 0, sizeof(p->password)))
    return false;
  size_t n = strlen(p->password);
  if (n && n < 8)
    return false;
  if (n == 64 && strspn(p->password, "0123456789abcdefABCDEF") != 64)
    return false;
  return true;
}
enum { HEADER = 38, PAYLOAD = 393, ITERATIONS = 100000 };
static int vault_crypt(bool encrypt, const char *password,
                       const uint8_t *header, const uint8_t *input,
                       uint8_t *output, uint8_t *tag) {
  uint8_t key[32];
  int result = -1;
#ifdef ESP_PLATFORM
  if (mbedtls_pkcs5_pbkdf2_hmac_ext(
          MBEDTLS_MD_SHA256, (const unsigned char *)password, strlen(password),
          header + 8, 16, ITERATIONS, 32, key))
    goto done;
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  if (!mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256)) {
    result =
        encrypt
            ? mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, PAYLOAD,
                                        header + 24, 12, header, HEADER, input,
                                        output, 16, tag)
            : mbedtls_gcm_auth_decrypt(&gcm, PAYLOAD, header + 24, 12, header,
                                       HEADER, tag, 16, input, output);
  }
  mbedtls_gcm_free(&gcm);
#else
  if (PKCS5_PBKDF2_HMAC(password, strlen(password), header + 8, 16, ITERATIONS,
                        EVP_sha256(), 32, key) != 1)
    goto done;
  EVP_CIPHER_CTX *gcm = EVP_CIPHER_CTX_new();
  int n = 0, tail = 0;
  if (!gcm)
    goto done;
  if (encrypt) {
    if (EVP_EncryptInit_ex(gcm, EVP_aes_256_gcm(), NULL, key, header + 24) ==
            1 &&
        EVP_EncryptUpdate(gcm, NULL, &n, header, HEADER) == 1 &&
        EVP_EncryptUpdate(gcm, output, &n, input, PAYLOAD) == 1 &&
        EVP_EncryptFinal_ex(gcm, output + n, &tail) == 1 &&
        EVP_CIPHER_CTX_ctrl(gcm, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1)
      result = 0;
  } else {
    if (EVP_DecryptInit_ex(gcm, EVP_aes_256_gcm(), NULL, key, header + 24) ==
            1 &&
        EVP_DecryptUpdate(gcm, NULL, &n, header, HEADER) == 1 &&
        EVP_DecryptUpdate(gcm, output, &n, input, PAYLOAD) == 1 &&
        EVP_CIPHER_CTX_ctrl(gcm, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1 &&
        EVP_DecryptFinal_ex(gcm, output + n, &tail) == 1)
      result = 0;
  }
  EVP_CIPHER_CTX_free(gcm);
#endif
done:
  pos_secure_zero(key, sizeof(key));
  return result;
}
int pos_wifi_vault_encrypt(const pos_wifi_profiles_t *p, const char *pass,
                           const uint8_t salt[16], const uint8_t nonce[12],
                           uint8_t out[POS_WIFI_VAULT_BYTES]) {
  if (!p || p->count > POS_WIFI_PROFILES || !pass || strlen(pass) < 12 ||
      strlen(pass) > 64)
    return -1;
  uint8_t plain[PAYLOAD] = {0};
  plain[0] = p->count;
  for (unsigned i = 0; i < p->count; ++i) {
    if (!pos_wifi_profile_valid(&p->networks[i])) {
      pos_secure_zero(plain, sizeof(plain));
      return -1;
    }
    memcpy(plain + 1 + i * 98, p->networks[i].ssid,
           strlen(p->networks[i].ssid) + 1);
    memcpy(plain + 1 + i * 98 + 33, p->networks[i].password,
           strlen(p->networks[i].password) + 1);
  }
  memcpy(out, "POSWIFI1", 8);
  memcpy(out + 8, salt, 16);
  memcpy(out + 24, nonce, 12);
  out[36] = PAYLOAD & 255;
  out[37] = PAYLOAD >> 8;
  int result =
      vault_crypt(true, pass, out, plain, out + HEADER, out + HEADER + PAYLOAD);
  pos_secure_zero(plain, sizeof(plain));
  return result;
}
int pos_wifi_vault_unlock(const uint8_t in[POS_WIFI_VAULT_BYTES],
                          const char *pass, pos_wifi_profiles_t *p) {
  if (!pass || strlen(pass) < 12 || strlen(pass) > 64 ||
      memcmp(in, "POSWIFI1", 8) || in[36] != (PAYLOAD & 255) ||
      in[37] != (PAYLOAD >> 8))
    return -1;
  uint8_t plain[PAYLOAD], tag[16];
  memcpy(tag, in + HEADER + PAYLOAD, 16);
  int result = vault_crypt(false, pass, in, in + HEADER, plain, tag);
  pos_wifi_profiles_t checked = {0};
  if (!result && plain[0] <= POS_WIFI_PROFILES) {
    checked.count = plain[0];
    for (unsigned i = 0; i < checked.count; ++i) {
      memcpy(checked.networks[i].ssid, plain + 1 + i * 98, 33);
      memcpy(checked.networks[i].password, plain + 1 + i * 98 + 33, 65);
      if (!pos_wifi_profile_valid(&checked.networks[i]))
        result = -1;
    }
    if (!result)
      *p = checked;
  } else
    result = -1;
  pos_secure_zero(plain, sizeof(plain));
  pos_secure_zero(&checked, sizeof(checked));
  return result;
}
bool pos_https_url_valid(const char *url) {
  if (!url || strncmp(url, "https://", 8) || strlen(url) > 511 || !url[8])
    return false;
  const char *end = url + 8 + strcspn(url + 8, "/?#");
  if (end == url + 8 || *(url + 8) == ':')
    return false;
  for (const char *p = url + 8; p < end; ++p)
    if (*p == '@')
      return false;
  for (const char *p = url; *p; ++p)
    if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '\\')
      return false;
  return true;
}
bool pos_transfer_name_valid(const char *name) {
  return name && strlen(name) <= 120 && !strchr(name, '/') &&
         pos_fs_archive_path(name) && *name != '.' && !strstr(name, ".part");
}
static int failed(pos_error_t *error, const char *message) {
  if (error)
    snprintf(error->message, sizeof(error->message), "%s", message);
  return -1;
}
typedef struct {
  pos_io_progress_t progress;
  void *ctx;
} transfer_progress_t;
static int verify_progress(void *opaque, uint64_t done, uint64_t total) {
  transfer_progress_t *p = opaque;
  return p->progress
             ? p->progress(p->ctx, "Verifying downloaded file", done, total)
             : 0;
}
int pos_receive_file(const char *root, const char *relative, uint64_t expected,
                     const char *sha, pos_stream_read_t reader,
                     void *reader_ctx, pos_io_progress_t progress, void *ctx,
                     pos_error_t *error) {
  if (!pos_fs_archive_path(relative) || !pos_fs_user_path(relative) ||
      !reader || (!strncasecmp(relative, "POS/Users/settings.", 19)) ||
      (expected != UINT64_MAX && expected > UINT32_MAX) ||
      (sha && *sha &&
       (strlen(sha) != 64 || strspn(sha, "0123456789abcdefABCDEF") != 64)))
    return failed(error, "Invalid file destination, size or SHA-256.");
  char target[POS_FS_PATH], temporary[POS_FS_PATH];
  if (pos_fs_join(target, root, relative))
    return -1;
  struct stat st;
  if (!pos_fs_lstat(target, &st) || errno != ENOENT)
    return failed(
        error, "Destination exists or is unavailable. Choose a new filename.");
  FILE *out = NULL;
  for (unsigned i = 0; i < 100; ++i) {
    if (snprintf(temporary, sizeof(temporary), "%s.part-%u", target, i) >=
        (int)sizeof(temporary))
      return -1;
    out = fopen(temporary, "wbx");
    if (out)
      break;
    if (errno != EEXIST)
      break;
  }
  if (!out)
    return failed(error, "Cannot create transfer staging file.");
  uint8_t *buffer = malloc(4096);
  if (!buffer) {
    fclose(out);
    return failed(error,
                  "Not enough memory for transfer. Staging file retained.");
  }
  uint64_t total = 0;
  uint32_t written_crc = 0;
  int n, bad = 0;
  while ((n = reader(reader_ctx, buffer, 4096)) > 0) {
    total += n;
    if (n > 4096 || total > UINT32_MAX ||
        (expected != UINT64_MAX && total > expected) ||
        fwrite(buffer, 1, n, out) != (size_t)n ||
        (progress && progress(ctx, "Transferring file", total,
                              expected == UINT64_MAX ? 0 : expected))) {
      bad = 1;
      break;
    }
    written_crc = pos_crc32(written_crc, buffer, n);
  }
  free(buffer);
  if (n < 0 || (expected != UINT64_MAX && total != expected) || fflush(out) ||
      fsync(fileno(out)))
    bad = 1;
  if (fclose(out))
    bad = 1;
  char hash[65];
  uint32_t checked_crc;
  transfer_progress_t verification = {progress, ctx};
  if (!bad && (pos_fs_crc_file_progress(temporary, total, &checked_crc,
                                        verify_progress, &verification) ||
               checked_crc != written_crc ||
               pos_hash_file_progress(temporary, total, hash, verify_progress,
                                      &verification) ||
               (sha && *sha && strcasecmp(sha, hash))))
    bad = 1;
  if (bad)
    return failed(error, "Transfer incomplete or verification failed. Partial "
                         "file retained; destination untouched.");
  if (pos_fs_publish_new(temporary, target))
    return failed(error,
                  "Could not publish verified file. Staging file retained.");
  return 0;
}
