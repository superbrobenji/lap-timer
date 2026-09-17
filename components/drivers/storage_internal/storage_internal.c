/* storage_internal.c -- hal/storage.h over LittleFS (spec §13.1).
 *
 * Backs the §5.1 storage contract with the joltwallet/littlefs managed component mounted at
 * /lfs on the `storage` partition (partitions.csv: data/spiffs subtype id 0x82, which LittleFS
 * reuses). The §13.1 LittleFS geometry (read/prog 128, cache 512, lookahead 128, block_cycles
 * 512; block_size 4096 is fixed by the component for the ESP32) is set through the CONFIG_
 * LITTLEFS_* options in sdkconfig.defaults, so it is not repeated in the conf struct here;
 * only format_if_mount_failed is a struct field, and it is false because the mount ladder --
 * not the component -- decides when to format.
 *
 * Layering: the driver reports the ladder result through sto_mount's return value and never
 * touches sys_flags or the NVS counters (no dependency on components/app). The caller (boot
 * step 7 in app_main) owns SYS_STORAGE_DEAD / the E_STO_* error-ring entries / LT_CTR_STO_FORMAT.
 * Symbols are the plain sto_* names the app links against; main REQUIRES this component (the
 * §4.1 swappable interface-name registration is a 3.4 concern, exactly as for board_devkit_v1).
 */
#include "hal/storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "sto";

#define LFS_BASE   "/lfs"
#define LFS_PART   "storage"           /* partitions.csv label */
#define STO_PATHMAX 128

static const esp_vfs_littlefs_conf_t s_conf = {
    .base_path = LFS_BASE,
    .partition_label = LFS_PART,
    .partition = NULL,
    .format_if_mount_failed = false,   /* the ladder decides (§13.1) */
    .read_only = false,
    .dont_mount = false,
    .grow_on_mount = false,
};

/* Map a backend-relative path ("/sessions/x.log") onto the mount point ("/lfs/sessions/x.log"). */
static int full_path(const char *path, char *out, size_t cap)
{
    if (!path) return -1;
    int n = snprintf(out, cap, "%s%s", LFS_BASE, path);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

int sto_mount(void)
{
    esp_err_t e = esp_vfs_littlefs_register(&s_conf);          /* attempt 1 */
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "mount failed (%s); retrying", esp_err_to_name(e));
        e = esp_vfs_littlefs_register(&s_conf);                /* attempt 2 (retry) */
    }
    int formatted = 0;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "mount failed again (%s); formatting", esp_err_to_name(e));
        esp_err_t fe = esp_littlefs_format(LFS_PART);
        if (fe == ESP_OK) {
            e = esp_vfs_littlefs_register(&s_conf);            /* attempt 3 (post-format) */
            formatted = 1;
        } else {
            ESP_LOGE(TAG, "format failed (%s)", esp_err_to_name(fe));
        }
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "storage DEAD (%s)", esp_err_to_name(e));
        return -1;                                             /* caller -> SYS_STORAGE_DEAD */
    }

    /* §13.1: create the session/track directories at mount if missing (EEXIST is fine). */
    if (mkdir(LFS_BASE "/sessions", 0777) != 0 && errno != EEXIST)
        ESP_LOGW(TAG, "mkdir sessions: %s", strerror(errno));
    if (mkdir(LFS_BASE "/tracks", 0777) != 0 && errno != EEXIST)
        ESP_LOGW(TAG, "mkdir tracks: %s", strerror(errno));

    size_t total = 0, used = 0;
    esp_err_t ie = esp_littlefs_info(LFS_PART, &total, &used);
    if (ie != ESP_OK) ESP_LOGW(TAG, "esp_littlefs_info: %s", esp_err_to_name(ie));
    ESP_LOGI(TAG, "mounted %s at %s: %u KB total, %u KB free%s", LFS_PART, LFS_BASE,
             (unsigned)(total / 1024u), (unsigned)((total - used) / 1024u),
             formatted ? " (formatted)" : "");
    return formatted ? 1 : 0;                                  /* 1 => caller logs E_STO_FORMAT + counter */
}

int sto_info(sto_info_t *out)
{
    if (!out) return -1;
    size_t total = 0, used = 0;
    esp_err_t e = esp_littlefs_info(LFS_PART, &total, &used);
    if (e != ESP_OK) return -1;
    out->total_kb = (uint32_t)(total / 1024u);
    out->free_kb  = (uint32_t)((total - used) / 1024u);
    out->degraded = 0;                          /* internal LittleFS is never "degraded" (an SD concept) */
    return 0;
}

int sto_open(const char *path, int flags, sto_file_t *out)
{
    if (!out) return -1;
    char full[STO_PATHMAX];
    if (full_path(path, full, sizeof full) != 0) return -1;

    int of;
    if ((flags & STO_WR) && (flags & STO_RD)) of = O_RDWR;
    else if (flags & STO_WR)                  of = O_WRONLY;
    else                                       of = O_RDONLY;
    if (flags & STO_CREATE) of |= O_CREAT;
    if (flags & STO_APPEND) of |= O_APPEND;
    /* create-without-append means "start fresh" (the .sum.tmp rewrite path) -> truncate. */
    if ((flags & STO_CREATE) && !(flags & STO_APPEND)) of |= O_TRUNC;

    int fd = open(full, of, 0644);
    if (fd < 0) { ESP_LOGE(TAG, "open %s: %s", full, strerror(errno)); return -1; }
    *out = fd;
    return 0;
}

int sto_write(sto_file_t f, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(f, p + off, n - off);
        if (w < 0) { ESP_LOGE(TAG, "write: %s", strerror(errno)); return -1; }
        off += (size_t)w;
    }
    return 0;
}

int sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read)
{
    ssize_t r = read(f, buf, n);
    if (r < 0) { if (n_read) *n_read = 0; return -1; }
    if (n_read) *n_read = (size_t)r;            /* 0 => EOF */
    return 0;
}

int sto_seek(sto_file_t f, uint32_t offset)
{
    off_t r = lseek(f, (off_t)offset, SEEK_SET);
    return (r < 0) ? -1 : 0;
}

int sto_sync(sto_file_t f)
{
    return (fsync(f) != 0) ? -1 : 0;            /* §13.1: sto_sync maps to fsync */
}

int sto_close(sto_file_t f)
{
    return (close(f) != 0) ? -1 : 0;
}

int sto_rename(const char *from, const char *to)
{
    char f_full[STO_PATHMAX], t_full[STO_PATHMAX];
    if (full_path(from, f_full, sizeof f_full) != 0) return -1;
    if (full_path(to, t_full, sizeof t_full) != 0) return -1;
    if (rename(f_full, t_full) != 0) {          /* atomic on LittleFS (§13.1) */
        ESP_LOGE(TAG, "rename %s -> %s: %s", f_full, t_full, strerror(errno));
        return -1;
    }
    return 0;
}

int sto_unlink(const char *path)
{
    char full[STO_PATHMAX];
    if (full_path(path, full, sizeof full) != 0) return -1;
    if (unlink(full) != 0) { if (errno == ENOENT) return 0; return -1; }
    return 0;
}

int sto_list(const char *dir, void (*cb)(const char *name, uint32_t size, void *ctx), void *ctx)
{
    char full[STO_PATHMAX];
    if (full_path(dir, full, sizeof full) != 0) return -1;
    DIR *d = opendir(full);
    if (!d) return -1;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;                            /* skip "." and ".." */
        uint32_t sz = 0;
        if (cb) {
            char item[STO_PATHMAX];
            int m = snprintf(item, sizeof item, "%s/%s", full, ent->d_name);
            struct stat st;
            if (m > 0 && (size_t)m < sizeof item && stat(item, &st) == 0)
                sz = (uint32_t)st.st_size;
            cb(ent->d_name, sz, ctx);
        }
        count++;
    }
    closedir(d);
    return count;
}

int sto_probe(void)
{
    static const char *PROBE = "/.probe";       /* -> /lfs/.probe */
    uint8_t wr[256], rd[256];
    for (size_t i = 0; i < sizeof wr; i++) wr[i] = (uint8_t)(i * 7u + 1u);

    sto_file_t f;
    if (sto_open(PROBE, STO_WR | STO_CREATE, &f) != 0) return -1;
    int rc = sto_write(f, wr, sizeof wr);
    if (rc == 0) rc = sto_sync(f);
    sto_close(f);
    if (rc != 0) { sto_unlink(PROBE); return -1; }

    if (sto_open(PROBE, STO_RD, &f) != 0) { sto_unlink(PROBE); return -1; }
    size_t got = 0;
    rc = sto_read(f, rd, sizeof rd, &got);
    sto_close(f);
    sto_unlink(PROBE);
    if (rc != 0 || got != sizeof rd || memcmp(wr, rd, sizeof wr) != 0) return -1;
    return 0;
}
