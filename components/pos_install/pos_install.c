#include "pos_install.h"
#include "cJSON.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define FULL_PATH 512
#define OWNER ".pos-transaction"
#define BOOT_OK "System/.boot-ok"
#define INSTALLED "System/installed.pos"

static int fail(pos_error_t *error, const char *format, ...)
{
    if (error) {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return -1;
}

static int path_join(char out[FULL_PATH], const char *a, const char *b)
{
    int n = snprintf(out, FULL_PATH, "%s%s%s", a, *b ? "/" : "", b);
    return n >= 0 && n < FULL_PATH ? 0 : -1;
}

static int present(const char *path)
{
    struct stat st;
    if (!stat(path, &st)) return 1;
    return errno == ENOENT ? 0 : -1;
}

static bool keys(const cJSON *object, const char *const *allowed, size_t count)
{
    if (!cJSON_IsObject(object) || (size_t)cJSON_GetArraySize(object) != count) return false;
    for (size_t i = 0; i < count; ++i) {
        int found = 0;
        for (const cJSON *item = object->child; item; item = item->next)
            if (item->string && !strcmp(item->string, allowed[i])) ++found;
        if (found != 1) return false;
    }
    return true;
}

static const char *string_field(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static bool safe_version(const char *s)
{
    if (!s || !isdigit((unsigned char)*s) || strlen(s) > POS_VERSION_MAX) return false;
    for (; *s; ++s)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') ||
              (*s >= 'A' && *s <= 'Z') || *s == '.' || *s == '-' || *s == '_')) return false;
    return true;
}

static bool safe_path(const char *path)
{
    if (!path || strncmp(path, "System/", 7) || strlen(path) >= POS_PATH_MAX) return false;
    const char *directories[]={"System/Apps","System/Assets","System/Fonts","System/Sounds","System/Config"};
    for(size_t i=0;i<sizeof(directories)/sizeof(*directories);++i)
        if(!strcasecmp(path,directories[i]))return false;
    if (!strcasecmp(path, INSTALLED) || !strcasecmp(path, BOOT_OK)) return false;
    const char *segment = path;
    int depth = 0;
    while (*segment) {
        const char *end = strchr(segment, '/');
        size_t n = end ? (size_t)(end - segment) : strlen(segment);
        if (!n || n > 48 || ++depth > 8 || segment[0] == '.' || segment[n - 1] == '.') return false;
        for (size_t i = 0; i < n; ++i) {
            char c = segment[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        }
        if (n >= 5 && !strncasecmp(segment + n - 5, ".part", 5)) return false;
        size_t stem = 0;
        while (stem < n && segment[stem] != '.') ++stem;
        if (stem == 3 && (!strncasecmp(segment, "con", 3) || !strncasecmp(segment, "prn", 3) ||
                          !strncasecmp(segment, "aux", 3) || !strncasecmp(segment, "nul", 3))) return false;
        if (stem == 4 && (!strncasecmp(segment, "com", 3) || !strncasecmp(segment, "lpt", 3)) &&
            segment[3] >= '1' && segment[3] <= '9') return false;
        if (!end) break;
        segment = end + 1;
        if (!*segment) return false;
    }
    return true;
}

static bool conflicts(const char *a, const char *b)
{
    size_t n = strlen(a), m = strlen(b), min = n < m ? n : m;
    return !strncasecmp(a, b, min) && (n == m || (n < m ? b[n] == '/' : a[m] == '/'));
}

static bool file_record(const cJSON *object, pos_file_t *file, bool firmware)
{
    const char *const fields[] = {"path", "size", "sha256"};
    if (!keys(object, fields, 3)) return false;
    const char *path = string_field(object, "path"), *sha = string_field(object, "sha256");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(object, "size");
    if (!path || (firmware ? strcmp(path, "firmware.bin") != 0 : !safe_path(path)) ||
        !sha || strlen(sha) != 64 || !cJSON_IsNumber(size) || !isfinite(size->valuedouble) ||
        size->valuedouble < 0 || size->valuedouble > UINT32_MAX ||
        floor(size->valuedouble) != size->valuedouble) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f'))) return false;
    file->size = (uint32_t)size->valuedouble;
    if (firmware && (file->size < 288 || file->size > POS_FW_LIMIT)) return false;
    strcpy(file->path, path);
    strcpy(file->sha256, sha);
    return true;
}

int pos_parse_manifest(const char *text, size_t size, pos_package_t **out, pos_error_t *error)
{
    *out = NULL;
    if (!text || !size || size > POS_MANIFEST_MAX || memchr(text, 0, size))
        return fail(error, "Invalid manifest length");
    pos_package_t *p = calloc(1, sizeof(*p));
    if (p) p->manifest = malloc(size + 1);
    if (!p || !p->manifest) { pos_package_free(p); return fail(error, "Not enough heap for manifest"); }
    memcpy(p->manifest, text, size);
    p->manifest[size] = 0;
    p->manifest_size = size;
    if (strstr(p->manifest, "\\u0000")) goto invalid;
    /* Reject deeply nested input before cJSON can consume the embedded task stack. */
    bool quoted = false, escaped = false;
    int depth = 0;
    for (size_t i = 0; i < p->manifest_size; ++i) {
        char c = p->manifest[i];
        if (escaped) { escaped = false; continue; }
        if (quoted && c == '\\') { escaped = true; continue; }
        if (c == '"') quoted = !quoted;
        if (!quoted && (c == '{' || c == '[') && ++depth > 8) goto invalid;
        if (!quoted && (c == '}' || c == ']')) --depth;
    }
    cJSON *json = cJSON_ParseWithLengthOpts(p->manifest, p->manifest_size + 1, NULL, true);
    const char *const fields[] = {"format", "model", "version", "firmware", "files"};
    if (!keys(json, fields, 5)) { cJSON_Delete(json); goto invalid; }
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(json, "format");
    const char *model = string_field(json, "model"), *version = string_field(json, "version");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(json, "files");
    if (!cJSON_IsNumber(format) || format->valuedouble != 1 || !model || strcmp(model, POS_MODEL) ||
        !safe_version(version) || !cJSON_IsArray(files) || cJSON_GetArraySize(files) < 1 ||
        cJSON_GetArraySize(files) > POS_MAX_FILES ||
        !file_record(cJSON_GetObjectItemCaseSensitive(json, "firmware"), &p->firmware, true)) {
        cJSON_Delete(json); goto invalid;
    }
    strcpy(p->version, version);
    p->count = (size_t)cJSON_GetArraySize(files);
    for (size_t i = 0; i < p->count; ++i) {
        if (!file_record(cJSON_GetArrayItem(files, (int)i), &p->files[i], false) ||
            conflicts(p->files[i].path, INSTALLED) || conflicts(p->files[i].path, BOOT_OK)) {
            cJSON_Delete(json); goto invalid;
        }
        for (size_t j = 0; j < i; ++j)
            if (conflicts(p->files[i].path, p->files[j].path)) { cJSON_Delete(json); goto invalid; }
    }
    cJSON_Delete(json);
    if (pos_hash_data(p->manifest, p->manifest_size, p->id)) goto invalid;
    *out = p;
    return 0;
invalid:
    pos_package_free(p);
    return fail(error, "Invalid POS package: format, model, version, paths, sizes or hashes");
}
int pos_load_manifest(const char *path, pos_package_t **out, pos_error_t *error)
{
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(error, "Missing or unreadable POS manifest: %s", path);
    char *text = malloc(POS_MANIFEST_MAX + 1);
    if (!text) { fclose(f); return fail(error, "Not enough heap for manifest"); }
    size_t size = fread(text, 1, POS_MANIFEST_MAX + 1, f);
    int bad = ferror(f);
    if (fclose(f)) bad = 1;
    int result = bad ? fail(error, "Cannot read POS manifest") : pos_parse_manifest(text, size, out, error);
    free(text);
    return result;
}


void pos_package_free(pos_package_t *package)
{
    if (package) { free(package->manifest); free(package); }
}

static int verify_file(const char *base, const pos_file_t *file, pos_error_t *error)
{
    char path[FULL_PATH], hash[65];
    if (path_join(path, base, file->path) || pos_hash_file(path, file->size, hash) || strcmp(hash, file->sha256))
        return fail(error, "Missing, unreadable, wrong size or SHA-256 mismatch: %s", file->path);
    return 0;
}

static bool marker_matches(const char *base, const char *name, const char *id)
{
    char path[FULL_PATH], buffer[66];
    if (path_join(path, base, name)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buffer, 1, sizeof(buffer), f);
    bool ok = n == 64 && !ferror(f) && !memcmp(buffer, id, 64);
    if (fclose(f)) ok = false;
    return ok;
}

static int destination(const char *root, const pos_package_t *p, char base[FULL_PATH], bool *committed,
                       pos_error_t *error)
{
    char boot[FULL_PATH];
    if (path_join(base, root, "POS") || path_join(boot, base, BOOT_OK)) return fail(error, "Root path too long");
    int exists = present(base);
    if (exists < 0) return fail(error, "Cannot inspect existing POS tree");
    *committed = exists == 1;
    if (*committed) {
        if (!marker_matches(base, OWNER, p->id) || present(boot) != 0)
            return fail(error, "Existing POS installation preserved. Use Repair or Back Up & Reinstall in recovery");
        return 0;
    }
    if (path_join(base, root, "POS.installing")) return fail(error, "Root path too long");
    exists = present(base);
    if (exists < 0 || (exists && !marker_matches(base, OWNER, p->id)))
        return fail(error, "Unknown/interrupted POS.installing tree preserved; move it aside on a computer");
    return 0;
}

int pos_check_package(const char *root, const pos_package_t *p, const pos_platform_t *platform, pos_error_t *error)
{
    if (!p || !platform || !platform->free_bytes || !platform->check_firmware ||
        !platform->write_firmware || !platform->select_firmware) return fail(error, "Invalid installer platform");
    char source[FULL_PATH], firmware[FULL_PATH];
    if (path_join(source, root, "pos-install") ||
        path_join(firmware, source, p->firmware.path)) return -1;
    if (verify_file(source, &p->firmware, error) ||
        platform->check_firmware(platform->ctx, firmware, p))
        return fail(error, "Firmware is damaged, incompatible or not the manifest's POS runtime version");
    if (path_join(source, root, "pos-install/payload")) return fail(error, "Root path too long");
    uint64_t need = POS_MANIFEST_MAX + 10*65536; /* conservative FAT allocation / standard-directory overhead */
    for (size_t i = 0; i < p->count; ++i) {
        if (platform->progress && platform->progress(platform->ctx,"verify",p->files[i].path,i,p->count))
            return fail(error,"Verification cancelled; no install writes made");
        if (verify_file(source, &p->files[i], error)) return -1;
        size_t directories = 0;
        for (const char *s = p->files[i].path; *s; ++s) directories += *s == '/';
        need += (uint64_t)p->files[i].size + (directories + 2) * 65536;
    }
    uint64_t available;
    if (platform->free_bytes(platform->ctx, root, &available)) return fail(error, "Cannot determine SD free space");
    if (available < need) return fail(error, "Insufficient storage detected: need %llu bytes; available %llu",
                                    (unsigned long long)need, (unsigned long long)available);
    return 0;
}

int pos_preflight(const char *root,const pos_package_t *p,const pos_platform_t *platform,pos_error_t *error)
{
    char base[FULL_PATH];bool committed;
    if(!p||destination(root,p,base,&committed,error))return -1;
    return pos_check_package(root,p,platform,error);
}

static int make_dirs(const char *path)
{
    char copy[FULL_PATH];
    if (strlen(path) >= sizeof(copy)) return -1;
    strcpy(copy, path);
    for (char *s = copy + 1; *s; ++s) if (*s == '/') {
        *s = 0;
        struct stat st;
        if (mkdir(copy, 0775) && (errno != EEXIST || stat(copy, &st) || !S_ISDIR(st.st_mode))) return -1;
        *s = '/';
    }
    return 0;
}

static int sync_close(FILE *file)
{
    int result = fflush(file);
    if (!result && fsync(fileno(file))) result = -1;
    if (fclose(file)) result = -1;
    return result;
}

static int atomic_write(const char *base, const char *relative, const void *data, size_t size)
{
    char path[FULL_PATH], temp[FULL_PATH];
    if (path_join(path, base, relative) || snprintf(temp, sizeof(temp), "%s.part", path) >= (int)sizeof(temp) ||
        make_dirs(path)) return -1;
    char existing[65], expected[65];
    if (pos_hash_data(data, size, expected)) return -1;
    if (!pos_hash_file(path, (uint32_t)size, existing) && !strcmp(existing, expected)) return 0;
    FILE *f = fopen(temp, "wb");
    if (!f) return -1;
    int result = fwrite(data, 1, size, f) == size ? 0 : -1;
    result |= sync_close(f);
    if (result || pos_hash_file(temp, (uint32_t)size, existing) || strcmp(existing, expected)) return -1;
    /* FatFs cannot rename over a file. Only owned, unconfirmed transaction metadata
       is replaced; the verified .part and original package survive the gap. */
    if (unlink(path) && errno != ENOENT) return -1;
    if (rename(temp, path)) return -1;
    return 0;
}

static int copy_file(const char *source, const char *base, const pos_file_t *file,
                     const pos_platform_t *platform,pos_error_t *error)
{
    char from[FULL_PATH], to[FULL_PATH], temp[FULL_PATH];
    if (path_join(from, source, file->path) || path_join(to, base, file->path) ||
        snprintf(temp, sizeof(temp), "%s.part", to) >= (int)sizeof(temp) || make_dirs(to))
        return fail(error, "Cannot create destination for %s", file->path);
    char hash[65];
    if (!pos_hash_file(to, file->size, hash) && !strcmp(hash, file->sha256)) return 0;
    FILE *in = fopen(from, "rb");
    if (!in) return fail(error, "Cannot open source %s", file->path);
    FILE *out = fopen(temp, "wb");
    unsigned char *buffer = malloc(POS_COPY_BUFFER);
    int result = !out || !buffer ? -1 : 0;
    size_t n;
    uint64_t total = 0;
    while (!result && (n = fread(buffer, 1, POS_COPY_BUFFER, in)) != 0) {
        total += n;
        if (total > file->size || fwrite(buffer, 1, n, out) != n) result = -1;
        if(platform->progress && platform->progress(platform->ctx,"copy",file->path,total,file->size))result=-1;
    }
    if (ferror(in) || total != file->size) result = -1;
    if (fclose(in)) result = -1;
    if (out) result |= sync_close(out);
    free(buffer);
    /* Reread the destination: validates storage writes, not just source reads. */
    if (!result && (pos_hash_file(temp, file->size, hash) || strcmp(hash, file->sha256))) result = -1;
    if (!result && unlink(to) && errno != ENOENT) result = -1;
    if (!result && rename(temp, to)) result = -1;
    if (result) return fail(error, "Copy/verification failed for %s; original payload retained", file->path);
    return 0;
}

static void report(const pos_platform_t *platform, const char *phase, const char *detail)
{
    if (platform->report) platform->report(platform->ctx, phase, detail);
}

int pos_install(const char *root, const pos_package_t *p, const pos_platform_t *platform, pos_error_t *error)
{
    if (pos_preflight(root, p, platform, error)) return -1;
    report(platform, "validated", p->version);
    char base[FULL_PATH], source[FULL_PATH], final[FULL_PATH];
    bool committed;
    if (destination(root, p, base, &committed, error) || path_join(source, root, "pos-install/payload") ||
        path_join(final, root, "POS")) return -1;
    if (mkdir(base, 0775) && errno != EEXIST) return fail(error, "Cannot create staging directory");
    if (atomic_write(base, OWNER, p->id, 64)) return fail(error, "Cannot save transaction marker");
    const char *directories[]={"System/Apps/","System/Assets/","System/Fonts/","System/Sounds/","System/Config/"};
    for(size_t i=0;i<sizeof(directories)/sizeof(*directories);++i){char path[FULL_PATH];
        if(path_join(path,base,directories[i])||make_dirs(path))return fail(error,"Cannot initialize system directories");}
    for (size_t i = 0; i < p->count; ++i) {
        report(platform, "copy", p->files[i].path);
        if (copy_file(source, base, &p->files[i], platform,error)) return -1;
    }
    if (atomic_write(base, INSTALLED, p->manifest, p->manifest_size)) return fail(error, "Cannot save installed manifest");
    for (size_t i = 0; i < p->count; ++i)
        if (verify_file(base, &p->files[i], error)) return -1;
    report(platform, "staged", p->id);
    if (!committed && rename(base, final)) return fail(error, "Cannot commit staged POS tree; staging retained");
    report(platform, "sd_committed", p->version);
    if (path_join(source, root, "pos-install/firmware.bin")) return fail(error, "Root path too long");
    if (platform->write_firmware(platform->ctx, source, p))
        return fail(error, "Firmware write/readback failed; installer and package retained. Retry is safe");
    report(platform, "firmware_written", p->version);
    if (platform->select_firmware(platform->ctx)) return fail(error, "Cannot select runtime boot partition");
    report(platform, "boot_selected", "Power off, then power on. Keep the SD card inserted; leave installer files until a verified boot");
    return 0;
}

int pos_repair_system(const char *root,const pos_package_t *p,const pos_platform_t *platform,pos_error_t *error)
{
    char base[FULL_PATH],boot[FULL_PATH];
    if(pos_check_package(root,p,platform,error))return -1;
    if(path_join(base,root,"POS")||!marker_matches(base,OWNER,p->id)||path_join(boot,base,BOOT_OK))
        return fail(error,"Repair requires the exact original package; existing installation preserved");
    if(unlink(boot)&&errno!=ENOENT)return fail(error,"Cannot mark repair pending");
    return pos_install(root,p,platform,error);
}

int pos_validate_system(const char *root, const pos_package_t *p, pos_error_t *error)
{
    char base[FULL_PATH];
    if (path_join(base, root, "POS") || !marker_matches(base, OWNER, p->id))
        return fail(error, "POS Boot Manager: No bootable POS system drive found");
    for (size_t i = 0; i < p->count; ++i)
        if (verify_file(base, &p->files[i], error)) return -1;
    return 0;
}

int pos_confirm_system(const char *root, const pos_package_t *p, pos_error_t *error)
{
    char base[FULL_PATH];
    if (pos_validate_system(root, p, error) || path_join(base, root, "POS")) return -1;
    if (atomic_write(base, BOOT_OK, p->id, 64)) return fail(error, "Cannot persist successful boot marker");
    return 0;
}

static void prune_parents(char *path, const char *stop)
{
    char *slash;
    while ((slash = strrchr(path, '/')) && (size_t)(slash - path) > strlen(stop)) {
        *slash = 0;
        if (rmdir(path)) break; /* unknown files are never removed */
    }
}

int pos_cleanup_package(const char *root, const pos_package_t *p, pos_error_t *error)
{
    if (pos_validate_system(root, p, error)) return -1;
    char base[FULL_PATH], source[FULL_PATH], path[FULL_PATH], hash[65];
    if (path_join(base, root, "POS") || !marker_matches(base, BOOT_OK, p->id))
        return fail(error, "Installer cleanup refused before successful boot");
    if (path_join(source, root, "pos-install") || path_join(path, source, "manifest.pos")) return -1;
    if (present(path) == 0) return 0;
    if (pos_hash_file(path, (uint32_t)p->manifest_size, hash) || strcmp(hash, p->id))
        return fail(error, "Different installer package preserved");
    /* Validate all remaining known files BEFORE deleting any. Missing files allow interrupted cleanup to resume. */
    for (size_t i = 0; i <= p->count; ++i) {
        const pos_file_t *file = i == p->count ? &p->firmware : &p->files[i];
        char payload[FULL_PATH];
        if (path_join(payload, source, i == p->count ? "" : "payload") || path_join(path, payload, file->path)) return -1;
        int exists = present(path);
        if (exists < 0 || (exists && verify_file(payload, file, error)))
            return fail(error, "Changed or unreadable installer files preserved");
    }
    for (size_t i = 0; i <= p->count; ++i) {
        const pos_file_t *file = i == p->count ? &p->firmware : &p->files[i];
        char payload[FULL_PATH];
        if (path_join(payload, source, i == p->count ? "" : "payload") || path_join(path, payload, file->path)) return -1;
        if (unlink(path) && errno != ENOENT) return fail(error, "Installer cleanup incomplete; retry on next boot");
        prune_parents(path, source);
    }
    if (path_join(path, source, "manifest.pos") || unlink(path)) return fail(error, "Cannot finish installer cleanup");
    rmdir(source); /* best effort: leave all unlisted files untouched */
    return 0;
}
