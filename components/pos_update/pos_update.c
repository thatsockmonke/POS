#include "pos_update.h"
#include "cJSON.h"
#include "pos_network.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "mbedtls/ecdsa.h"
#include "mbedtls/pk.h"
#else
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif
static int fail(pos_error_t *e, const char *message) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", message);
  return -1;
}
static int key_action(const char *pem, const unsigned char *body, size_t length,
                      const unsigned char *sig, size_t siglen,
                      char fingerprint[65]) {
  if (!pem || strlen(pem) > 512)
    return -1;
  unsigned char der[256];
  int bytes = -1, result = -1;
#ifdef ESP_PLATFORM
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  if (mbedtls_pk_parse_public_key(&key, (const unsigned char *)pem,
                                  strlen(pem) + 1) ||
      !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA) ||
      mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key)) !=
          MBEDTLS_ECP_DP_SECP256R1)
    goto done;
  bytes = mbedtls_pk_write_pubkey_der(&key, der, sizeof(der));
  if (bytes <= 0)
    goto done;
  if (fingerprint)
    result = pos_hash_data(der + sizeof(der) - bytes, bytes, fingerprint);
  else {
    char hex[65];
    unsigned char hash[32];
    if (pos_hash_data(body, length, hex))
      goto done;
    for (unsigned i = 0; i < 32; ++i) {
      unsigned n;
      if (sscanf(hex + 2 * i, "%2x", &n) != 1)
        goto done;
      hash[i] = n;
    }
    result = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, 32, sig, siglen)
                 ? -1
                 : 0;
  }
done:
  mbedtls_pk_free(&key);
#else
  BIO *bio = BIO_new_mem_buf(pem, -1);
  EVP_PKEY *key = bio ? PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL) : NULL;
  BIO_free(bio);
  char group[64];
  size_t n = 0;
  if (!key || !EVP_PKEY_is_a(key, "EC") ||
      EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group,
                                     sizeof(group), &n) != 1 ||
      strcmp(group, "prime256v1"))
    goto done;
  unsigned char *write = der;
  bytes = i2d_PUBKEY(key, NULL);
  if (bytes <= 0 || bytes > (int)sizeof(der) ||
      i2d_PUBKEY(key, &write) != bytes)
    goto done;
  if (fingerprint)
    result = pos_hash_data(der, bytes, fingerprint);
  else {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx && EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, key) == 1 &&
        EVP_DigestVerify(ctx, sig, siglen, body, length) == 1)
      result = 0;
    EVP_MD_CTX_free(ctx);
  }
done:
  EVP_PKEY_free(key);
#endif
  return result;
}
int pos_update_key_fingerprint(const char *pem, char fingerprint[65]) {
  return key_action(pem, NULL, 0, NULL, 0, fingerprint);
}
static bool version(const char *s, uint32_t out[3], const char **pre) {
  if (!s || strlen(s) > 31)
    return false;
  const char *p = s;
  for (unsigned i = 0; i < 3; ++i) {
    if (!isdigit((unsigned char)*p))
      return false;
    const char *begin = p;
    uint64_t n = 0;
    while (isdigit((unsigned char)*p)) {
      n = n * 10 + *p++ - '0';
      if (n > UINT32_MAX)
        return false;
    }
    if (p - begin > 1 && *begin == '0')
      return false;
    out[i] = n;
    if (i < 2 && *p++ != '.')
      return false;
  }
  *pre = NULL;
  if (!*p)
    return true;
  if (*p++ != '-' || !*p)
    return false;
  *pre = p;
  while (*p) {
    const char *start = p;
    bool numeric = true;
    while (*p && *p != '.') {
      if (!isalnum((unsigned char)*p) && *p != '-')
        return false;
      if (!isdigit((unsigned char)*p))
        numeric = false;
      ++p;
    }
    if (p == start || (numeric && p - start > 1 && *start == '0'))
      return false;
    if (*p == '.') {
      ++p;
      if (!*p)
        return false;
    }
  }
  return true;
}
int pos_version_compare(const char *a, const char *b, int *result) {
  uint32_t x[3], y[3];
  const char *p, *q;
  if (!result || !version(a, x, &p) || !version(b, y, &q))
    return -1;
  for (unsigned i = 0; i < 3; ++i)
    if (x[i] != y[i]) {
      *result = x[i] < y[i] ? -1 : 1;
      return 0;
    }
  if (!p || !q) {
    *result = p ? -1 : q ? 1 : 0;
    return 0;
  }
  while (*p && *q) {
    size_t n = strcspn(p, "."), m = strcspn(q, ".");
    bool pn = strspn(p, "0123456789") == n, qn = strspn(q, "0123456789") == m;
    int c = 0;
    if (pn != qn)
      c = pn ? -1 : 1;
    else if (pn && n != m)
      c = n < m ? -1 : 1;
    else {
      c = strncmp(p, q, n < m ? n : m);
      if (!c && n != m)
        c = n < m ? -1 : 1;
    }
    if (c) {
      *result = c < 0 ? -1 : 1;
      return 0;
    }
    p += n;
    q += m;
    if (*p)
      ++p;
    if (*q)
      ++q;
  }
  *result = *p ? 1 : *q ? -1 : 0;
  return 0;
}
static const char *string(const cJSON *j, const char *name) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, name);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
void pos_update_free(pos_update_t *u) {
  if (u) {
    pos_package_free(u->package);
    free(u);
  }
}
int pos_update_load(const char *file, const char *key, const char *current,
                    uint32_t floor, pos_update_t **out, pos_error_t *error) {
  *out = NULL;
  FILE *f = fopen(file, "rb");
  if (!f)
    return fail(error, "No readable update manifest.");
  char *text = malloc(POS_UPDATE_MAX + 1);
  if (!text) {
    fclose(f);
    return fail(error, "Not enough memory for update manifest.");
  }
  size_t n = fread(text, 1, POS_UPDATE_MAX + 1, f);
  int bad = ferror(f);
  fclose(f);
  cJSON *envelope = NULL, *j = NULL;
  pos_update_t *u = NULL;
  int result = -1;
  if (bad || !n || n > POS_UPDATE_MAX || !pos_fs_json_safe(text, n))
    goto invalid;
  text[n] = 0;
  envelope = cJSON_ParseWithOpts(text, NULL, true);
  free(text);
  text = NULL;
  const char *body = string(envelope, "payload"),
             *signature = string(envelope, "signature");
  if (!cJSON_IsObject(envelope) || cJSON_GetArraySize(envelope) != 2 || !body ||
      !signature || strlen(signature) < 16 || strlen(signature) > 144 ||
      strlen(signature) % 2 ||
      strspn(signature, "0123456789abcdef") != strlen(signature))
    goto invalid;
  unsigned char sig[72];
  size_t siglen = strlen(signature) / 2;
  for (size_t i = 0; i < siglen; ++i) {
    unsigned v;
    if (sscanf(signature + 2 * i, "%2x", &v) != 1)
      goto invalid;
    sig[i] = v;
  }
  if (key_action(key, (const unsigned char *)body, strlen(body), sig, siglen,
                 NULL)) {
    fail(error,
         "Update signature is invalid or the signing key is not trusted.");
    goto done;
  }
  if (!pos_fs_json_safe(body, strlen(body)))
    goto invalid;
  j = cJSON_ParseWithOpts(body, NULL, true);
  const char *release = string(j, "release"),
             *minimum = string(j, "minimum_version"),
             *model = string(j, "model"), *kind = string(j, "kind"),
             *base = string(j, "base_url");
  cJSON *format = cJSON_GetObjectItemCaseSensitive(j, "format"),
        *sequence = cJSON_GetObjectItemCaseSensitive(j, "sequence"),
        *package = cJSON_GetObjectItemCaseSensitive(j, "package");
  int compare;
  if (!cJSON_IsObject(j) || cJSON_GetArraySize(j) != 8 ||
      !cJSON_IsNumber(format) || format->valuedouble != 1 || !model ||
      strcmp(model, POS_MODEL) || !release || !minimum ||
      pos_version_compare(release, minimum, &compare) || compare < 0 || !kind ||
      (strcmp(kind, "firmware") && strcmp(kind, "assets")) || !base ||
      !pos_https_url_valid(base) || strchr(base, '?') || strlen(base) > 350 ||
      !cJSON_IsNumber(sequence) || sequence->valuedouble < 1 ||
      sequence->valuedouble > UINT32_MAX ||
      sequence->valuedouble != (uint32_t)sequence->valuedouble ||
      !cJSON_IsObject(package))
    goto invalid;
  u = calloc(1, sizeof(*u));
  if (!u) {
    fail(error, "Not enough memory for update metadata.");
    goto done;
  }
  if (pos_hash_data(body, strlen(body), u->id))
    goto invalid;
  strcpy(u->release, release);
  strcpy(u->minimum, minimum);
  strcpy(u->base_url, base);
  u->sequence = sequence->valuedouble;
  u->firmware = !strcmp(kind, "firmware");
  char *manifest = cJSON_PrintUnformatted(package);
  if (!manifest)
    goto invalid;
  int parsed =
      pos_parse_manifest(manifest, strlen(manifest), &u->package, error);
  free(manifest);
  if (parsed)
    goto done;
  if (u->sequence <= floor) {
    fail(error,
         "This update is already installed or has an older signed sequence.");
    result = 1;
    goto done;
  }
  if (pos_version_compare(current, u->minimum, &compare))
    goto invalid;
  if (compare < 0) {
    fail(error, "This version cannot upgrade directly. Back up your filesystem "
                "and perform a clean installation.");
    result = -2;
    goto done;
  }
  if (u->firmware) {
    if (strcmp(u->release, u->package->version))
      goto invalid;
    if (pos_version_compare(u->release, current, &compare))
      goto invalid;
    if (compare <= 0) {
      fail(error, "POS is up to date; firmware downgrades are refused.");
      result = 1;
      goto done;
    }
  } else if (strcmp(u->package->version, current)) {
    fail(error,
         "This asset update requires a different resident firmware version.");
    goto done;
  }
  *out = u;
  u = NULL;
  result = 0;
  goto done;
invalid:
  fail(error, "Invalid update format, model, version, paths, sizes or signed "
              "metadata.");
done:
  free(text);
  cJSON_Delete(j);
  cJSON_Delete(envelope);
  pos_update_free(u);
  return result;
}
