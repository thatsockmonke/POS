#pragma once
#include <stddef.h>
#include <stdint.h>

#define POS_MODEL "m5stack-cardputer-adv"
#define POS_MAX_FILES 64
#define POS_PATH_MAX 160
#define POS_MANIFEST_MAX 32768
#define POS_COPY_BUFFER 8192
#define POS_VERSION_MAX 31
#define POS_FW_LIMIT 0x170000u

typedef struct {
    char path[POS_PATH_MAX];
    uint32_t size;
    char sha256[65];
} pos_file_t;

typedef struct {
    char version[32];
    pos_file_t firmware;
    pos_file_t files[POS_MAX_FILES];
    size_t count;
    char id[65];
    char *manifest;
    size_t manifest_size;
} pos_package_t;

typedef struct {
    void *ctx;
    /* All hooks return 0 on success. Never select firmware from write_firmware. */
    int (*free_bytes)(void *ctx, const char *root, uint64_t *free_bytes);
    int (*check_firmware)(void *ctx, const char *path, const pos_package_t *package);
    int (*write_firmware)(void *ctx, const char *path, const pos_package_t *package);
    int (*select_firmware)(void *ctx);
    void (*report)(void *ctx, const char *phase, const char *detail);
    int (*progress)(void *ctx, const char *phase, const char *detail, uint64_t done, uint64_t total);
} pos_platform_t;

typedef struct { char message[256]; } pos_error_t;

int pos_load_manifest(const char *path, pos_package_t **out, pos_error_t *error);
void pos_package_free(pos_package_t *package);
/* Preflight performs no filesystem or flash writes. */
int pos_preflight(const char *root, const pos_package_t *package,
                  const pos_platform_t *platform, pos_error_t *error);
int pos_check_package(const char *root, const pos_package_t *package,
                      const pos_platform_t *platform, pos_error_t *error);
int pos_install(const char *root, const pos_package_t *package,
                const pos_platform_t *platform, pos_error_t *error);
int pos_repair_system(const char *root,const pos_package_t *package,
                      const pos_platform_t *platform,pos_error_t *error);
int pos_validate_system(const char *root, const pos_package_t *package, pos_error_t *error);
int pos_confirm_system(const char *root, const pos_package_t *package, pos_error_t *error);
/* Call only AFTER system validation and platform firmware confirmation. */
int pos_cleanup_package(const char *root, const pos_package_t *package, pos_error_t *error);
int pos_hash_file(const char *path, uint32_t size, char hex[65]);
int pos_hash_file_progress(const char *path, uint32_t size, char hex[65],
                          int (*progress)(void *, uint64_t, uint64_t), void *ctx);
int pos_hash_data(const void *data, size_t size, char hex[65]);

int pos_parse_manifest(const char *,size_t,pos_package_t **,pos_error_t *);

/* Compare authenticated package content independently of JSON whitespace/order. */
int pos_package_equivalent(const pos_package_t *,const pos_package_t *);
