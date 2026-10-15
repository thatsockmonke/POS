/* Native platform fixture for the SAME core used by the firmware. Never flashed. */
#include "pos_install.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *root;
    const char *interrupt;
    uint64_t available;
    int fail_write, fail_select;
} context_t;

static int free_space(void *opaque, const char *root, uint64_t *available)
{
    (void)root;
    *available = ((context_t *)opaque)->available;
    return 0;
}

static int check_image(void *opaque, const char *path, const pos_package_t *package)
{
    (void)opaque;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char bytes[288];
    int bad = fread(bytes, 1, sizeof(bytes), f) != sizeof(bytes);
    fclose(f);
    return !bad && bytes[0] == 0xe9 && bytes[12] == 9 && bytes[13] == 0 &&
        !memcmp(bytes + 32, "\x32\x54\xcd\xab", 4) &&
        memchr(bytes + 48, 0, 32) && !strcmp((char *)bytes + 48, package->version) &&
        memchr(bytes + 80, 0, 32) && !strcmp((char *)bytes + 80, "pos_runtime") ? 0 : -1;
}

static int write_image(void *opaque, const char *path, const pos_package_t *package)
{
    context_t *ctx = opaque;
    char destination[512], hash[65];
    snprintf(destination, sizeof(destination), "%s/.sim-ota.bin", ctx->root);
    FILE *in = fopen(path, "rb"), *out = fopen(destination, "wb");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return -1; }
    unsigned char buffer[8192];
    size_t n;
    int bad = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), in))) {
        if (fwrite(buffer, 1, n, out) != n) bad = 1;
        if (ctx->fail_write) { bad = 1; break; }
    }
    if (ferror(in)) bad = 1;
    if (fclose(in)) bad = 1;
    if (fclose(out)) bad = 1;
    if (pos_hash_file(destination, package->firmware.size, hash) || strcmp(hash, package->firmware.sha256)) bad = 1;
    return bad ? -1 : 0;
}

static int select_image(void *opaque)
{
    context_t *ctx = opaque;
    if (ctx->fail_select) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/.sim-boot-selected", ctx->root);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    return fclose(f);
}

static void progress(void *opaque, const char *phase, const char *detail)
{
    context_t *ctx = opaque;
    printf("%s: %s\n", phase, detail);
    fflush(stdout);
    if (ctx->interrupt && !strcmp(phase, ctx->interrupt)) _exit(99);
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    context_t ctx = {.root = argv[1], .available = 1024ULL * 1024 * 1024};
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--interrupt") && i + 1 < argc) ctx.interrupt = argv[++i];
        else if (!strcmp(argv[i], "--free") && i + 1 < argc) ctx.available = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--fail-write")) ctx.fail_write = 1;
        else if (!strcmp(argv[i], "--fail-select")) ctx.fail_select = 1;
        else return 2;
    }
    pos_platform_t platform = {.ctx = &ctx, .free_bytes = free_space, .check_firmware = check_image,
        .write_firmware = write_image, .select_firmware = select_image, .report = progress};
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", ctx.root,
        !strcmp(argv[2], "install") || !strcmp(argv[2], "preflight") || !strcmp(argv[2], "parse") ?
        "pos-install/manifest.pos" : "POS/System/installed.pos");
    pos_package_t *p = NULL;
    pos_error_t error = {0};
    int result = pos_load_manifest(path, &p, &error);
    if (!result) {
        if (!strcmp(argv[2], "parse")) result = 0;
        else if (!strcmp(argv[2], "preflight")) result = pos_preflight(ctx.root, p, &platform, &error);
        else if (!strcmp(argv[2], "install")) result = pos_install(ctx.root, p, &platform, &error);
        else if (!strcmp(argv[2], "validate")) result = pos_validate_system(ctx.root, p, &error);
        else if (!strcmp(argv[2], "confirm")) result = pos_confirm_system(ctx.root, p, &error);
        else if (!strcmp(argv[2], "cleanup")) result = pos_cleanup_package(ctx.root, p, &error);
        else if (!strcmp(argv[2], "boot")) {
            char hash[65];
            snprintf(path, sizeof(path), "%s/.sim-ota.bin", ctx.root);
            result = pos_validate_system(ctx.root, p, &error);
            if (!result) result = pos_hash_file(path, p->firmware.size, hash) || strcmp(hash, p->firmware.sha256);
            if (!result) result = pos_confirm_system(ctx.root, p, &error);
            if (!result) result = pos_cleanup_package(ctx.root, p, &error);
        } else result = -1;
    }
    if (result) fprintf(stderr, "%s\n", *error.message ? error.message : "Simulated boot/image validation failed");
    pos_package_free(p);
    return result ? 1 : 0;
}
