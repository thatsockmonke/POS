#include "pos_install.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "mbedtls/sha256.h"
typedef mbedtls_sha256_context hash_ctx;
static int begin(hash_ctx *c)
{
    mbedtls_sha256_init(c);
    int result = mbedtls_sha256_starts(c, 0);
    if (result) mbedtls_sha256_free(c);
    return result;
}
static int update(hash_ctx *c, const void *p, size_t n) { return mbedtls_sha256_update(c, p, n); }
static int end(hash_ctx *c, unsigned char *p) { int r = mbedtls_sha256_finish(c, p); mbedtls_sha256_free(c); return r; }
#else
#include <openssl/evp.h>
typedef EVP_MD_CTX *hash_ctx;
static int begin(hash_ctx *c)
{
    *c = EVP_MD_CTX_new();
    if (!*c) return -1;
    if (EVP_DigestInit_ex(*c, EVP_sha256(), NULL) == 1) return 0;
    EVP_MD_CTX_free(*c);
    return -1;
}
static int update(hash_ctx *c, const void *p, size_t n) { return EVP_DigestUpdate(*c, p, n) != 1; }
static int end(hash_ctx *c, unsigned char *p) { int r = EVP_DigestFinal_ex(*c, p, NULL) != 1; EVP_MD_CTX_free(*c); return r; }
#endif

static void hex_encode(const unsigned char bytes[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 15];
    }
    hex[64] = 0;
}

int pos_hash_data(const void *data, size_t size, char hex[65])
{
    hash_ctx ctx;
    unsigned char digest[32];
    if (begin(&ctx)) return -1;
    int result = update(&ctx, data, size);
    result |= end(&ctx, digest);
    if (!result) hex_encode(digest, hex);
    return result ? -1 : 0;
}

int pos_hash_file(const char *path, uint32_t size, char hex[65])
{
    return pos_hash_file_progress(path, size, hex, NULL, NULL);
}

int pos_hash_file_progress(const char *path, uint32_t size, char hex[65],
                          int (*progress)(void *, uint64_t, uint64_t), void *opaque)
{
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    unsigned char *buffer = malloc(POS_COPY_BUFFER);
    hash_ctx ctx;
    if (!buffer || begin(&ctx)) { free(buffer); fclose(file); return -1; }
    int result = 0;
    uint64_t total = 0;
    size_t n;
    while ((n = fread(buffer, 1, POS_COPY_BUFFER, file)) != 0) {
        total += n;
        if (total > size || update(&ctx, buffer, n) ||
            (progress && progress(opaque, total, size))) { result = -1; break; }
    }
    if (ferror(file) || total != size) result = -1;
    unsigned char digest[32];
    result |= end(&ctx, digest);
    if (fclose(file)) result = -1;
    free(buffer);
    if (!result) hex_encode(digest, hex);
    return result ? -1 : 0;
}
