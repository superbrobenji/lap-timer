/* devcontroller/components/logstore/logstore.c -- IDF/LittleFS glue for logstore: the bounded,
 * rotating black-box log on B's own `logs` partition (Plan 5.5 Task 5:
 * docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md). Mounts (if not already mounted
 * by main.c) and manages the `logs` LittleFS partition, opens/appends to the current log file,
 * rolls to a new file at a fixed per-file byte cap, and enforces the total `cap_bytes` passed to
 * logstore_init by deleting the oldest files -- via the PURE decision functions in
 * host/logstore_rot.c (logstore_should_rotate / logstore_pick_drop), never re-deriving that logic
 * here. See include/logstore.h for the pinned public interface (unchanged by this file: Task 2's
 * scaffold already fixed the signatures every consumer, including this implementation, builds
 * against).
 *
 * On-disk layout: one file per rotation, "/logs/log_<8-digit-id>.bin", ids monotonically
 * increasing (scanned back from existing filenames at init, so a reboot resumes past the highest
 * id already on disk rather than colliding/overwriting). Each file starts with an 8-byte
 * logstore_file_hdr_t (magic + created_unix, so logstore_list can report `created` without
 * relying on LittleFS mtimes, which the VFS layer does not reliably populate). Each appended
 * record is framed as a 13-byte logstore_rec_hdr_t (a monotonic-us timestamp + the record's
 * seq/flags/type/len) immediately followed by `len` payload bytes -- "seq|flags|type|len|data"
 * plus the timestamp the task brief asks for.
 */
#include "logstore.h"

#include "logstore_rot.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "logstore";

#define LOGS_BASE_PATH    "/logs"   /* must match main.c's LOGS_BASE; mounted read-write there */
#define LOGS_PART_LABEL   "logs"    /* partitions.csv label; must match main.c's LOGS_PART */
#define LOGSTORE_PATH_MAX 64        /* "/logs/log_" + 8 digits + ".bin" + NUL, with headroom */

/* Not pinned by any spec section -- a pragmatic per-file rotation size chosen so the 900 KB
 * cap_bytes main.c passes in yields a handful of files (~14), enough rotation granularity that
 * dropping the single oldest file is a small, bounded step relative to the total cap. */
#define LOGSTORE_MAX_FILE_BYTES (64u * 1024u)

#define LOGSTORE_FILE_MAGIC 0x53474f4cu /* "LOGS" (LE) */

/* Matches logstore_entry_t.id[] / logstore_file_info_t.id[] (include/logstore.h,
 * include/logstore_rot.h); the _Static_assert below turns a future size change on either struct
 * into a compile error here rather than a silent truncation of s_cur_id. */
#define LOGSTORE_ID_BUF 24
_Static_assert(sizeof(((logstore_entry_t *)0)->id) == LOGSTORE_ID_BUF,
               "LOGSTORE_ID_BUF must match logstore_entry_t.id[]");
_Static_assert(sizeof(((logstore_file_info_t *)0)->id) == LOGSTORE_ID_BUF,
               "LOGSTORE_ID_BUF must match logstore_file_info_t.id[]");

/* Precedes every log file's first record. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t created_unix;   /* time(NULL) when the file was opened -- see the caveat in
                               * open_new_file(): without NTP sync this is boot-relative, not a
                               * real epoch stamp, but it is what open_new_file() can produce
                               * offline (this device is AP-only, no WiFi STA/NTP). */
} logstore_file_hdr_t;

/* Precedes every appended record's `len` payload bytes. */
typedef struct __attribute__((packed)) {
    uint64_t ts_us;   /* esp_timer_get_time() at append time: monotonic microseconds since boot */
    uint16_t seq;
    uint8_t  flags;
    uint8_t  type;
    uint8_t  len;
} logstore_rec_hdr_t;

static bool     s_ready;
static uint32_t s_cap_bytes;
static uint32_t s_next_id = 1;
static int      s_cur_fd = -1;
static uint32_t s_cur_bytes;                  /* bytes written to the current file so far */
static char     s_cur_id[LOGSTORE_ID_BUF];
static int64_t  s_last_fsync_us;              /* time of the last fsync of the live file (M7) */

/* Worst-case on-disk size of one whole log file: header + a file grown to the per-file cap. The
 * total-cap reservation must hold back this much for the file about to be written -- not just the
 * first record -- or steady state overfills the partition and every append ENOSPC-wedges (M6). */
#define LOGSTORE_FILE_WORST (sizeof(logstore_file_hdr_t) + LOGSTORE_MAX_FILE_BYTES)

/* ---- small POSIX helpers (mirrors components/drivers/storage_internal/storage_internal.c's
 * style: this is a separate ESP-IDF project with no shared HAL, so the same small primitives are
 * reimplemented directly against the LittleFS-backed VFS here). ---- */

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            ESP_LOGE(TAG, "write: %s", strerror(errno));
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int build_path(const char *id, char *out, size_t cap)
{
    if (!id) return -1;
    int n = snprintf(out, cap, "%s/%s.bin", LOGS_BASE_PATH, id);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* "log_00000042.bin" -> 42; false (and *out_id left untouched) for anything else so a scan just
 * ignores filenames it doesn't own (a stray probe file, "." / "..", etc). */
static bool parse_log_filename(const char *name, uint32_t *out_id)
{
    if (strncmp(name, "log_", 4) != 0) return false;
    const char *num = name + 4;
    char *end = NULL;
    unsigned long v = strtoul(num, &end, 10);
    if (end == num) return false;
    if (strcmp(end, ".bin") != 0) return false;
    *out_id = (uint32_t)v;
    return true;
}

/* Scans LOGS_BASE_PATH for existing "log_<id>.bin" files, filling `out[]` (oldest-first, by id --
 * logstore_pick_drop's contract) with up to `max` of them and always computing the true highest
 * id seen across the WHOLE directory (not just the first `max`), so a caller can resume
 * allocating ids past whatever is already on disk even when `max` is smaller than the real file
 * count. Returns the number of entries written into out[] (<= max). Bounded by
 * LOGSTORE_ROT_MAX_FILES*4 readdir() calls (rule 2: never scan an unbounded/corrupted directory
 * to exhaustion). */
static int scan_existing(logstore_file_info_t *out, int max, uint32_t *out_next_id)
{
    assert(out != NULL);
    assert(max > 0 && max <= LOGSTORE_ROT_MAX_FILES);

    uint32_t ids[LOGSTORE_ROT_MAX_FILES];
    DIR *d = opendir(LOGS_BASE_PATH);
    if (!d) {
        if (out_next_id) *out_next_id = 1;
        return 0;
    }

    int n = 0;
    uint32_t max_id_seen = 0;
    int iters = 0;
    struct dirent *ent;
    while (iters++ < LOGSTORE_ROT_MAX_FILES * 4 && (ent = readdir(d)) != NULL) {
        uint32_t id_num;
        if (!parse_log_filename(ent->d_name, &id_num)) continue;
        if (id_num > max_id_seen) max_id_seen = id_num;
        if (n >= max) continue;   /* still keep scanning: need the true max id across all files */

        char path[LOGSTORE_PATH_MAX];
        int pn = snprintf(path, sizeof path, "%s/%s", LOGS_BASE_PATH, ent->d_name);
        uint32_t sz = 0;
        struct stat st;
        if (pn > 0 && (size_t)pn < sizeof path && stat(path, &st) == 0)
            sz = (uint32_t)st.st_size;

        /* insertion sort by id ascending, bounded by n < max <= LOGSTORE_ROT_MAX_FILES */
        int pos = n;
        while (pos > 0 && ids[pos - 1] > id_num) {
            ids[pos] = ids[pos - 1];
            out[pos] = out[pos - 1];
            pos--;
        }
        ids[pos] = id_num;
        snprintf(out[pos].id, sizeof out[pos].id, "log_%08u", (unsigned)id_num);
        out[pos].bytes = sz;
        n++;
    }
    closedir(d);
    if (out_next_id) *out_next_id = max_id_seen + 1;
    return n;
}

/* Deletes whatever logstore_pick_drop says must go so that the existing files on disk, plus
 * `incoming_bytes` about to be written, fit within s_cap_bytes. */
static void enforce_cap(uint32_t incoming_bytes)
{
    logstore_file_info_t files[LOGSTORE_ROT_MAX_FILES];
    int n = scan_existing(files, LOGSTORE_ROT_MAX_FILES, NULL);
    if (n <= 0) return;

    int drop_idx[LOGSTORE_ROT_MAX_FILES];
    int n_drop = logstore_pick_drop(files, n, incoming_bytes, s_cap_bytes, drop_idx,
                                     LOGSTORE_ROT_MAX_FILES);
    for (int i = 0; i < n_drop; i++) {
        char path[LOGSTORE_PATH_MAX];
        if (build_path(files[drop_idx[i]].id, path, sizeof path) != 0) continue;
        if (unlink(path) != 0 && errno != ENOENT)
            ESP_LOGW(TAG, "unlink %s: %s", path, strerror(errno));
        else
            ESP_LOGI(TAG, "rotated out %s (%u B)", files[drop_idx[i]].id,
                     (unsigned)files[drop_idx[i]].bytes);
    }
}

/* Allocates the next id, creates+truncates its file, writes the file header, and makes it
 * s_cur_fd/s_cur_id/s_cur_bytes. Any previous s_cur_fd must already be closed by the caller. */
static esp_err_t open_new_file(void)
{
    snprintf(s_cur_id, sizeof s_cur_id, "log_%08u", (unsigned)s_next_id);
    s_next_id++;

    char path[LOGSTORE_PATH_MAX];
    if (build_path(s_cur_id, path, sizeof path) != 0) return ESP_ERR_INVALID_ARG;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "open %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    logstore_file_hdr_t hdr = {
        .magic        = LOGSTORE_FILE_MAGIC,
        .created_unix = (uint32_t)time(NULL),
    };
    if (write_all(fd, &hdr, sizeof hdr) != 0) {
        close(fd);
        return ESP_FAIL;
    }
    s_cur_fd    = fd;
    s_cur_bytes = (uint32_t)sizeof hdr;
    ESP_LOGI(TAG, "opened %s (id=%s)", path, s_cur_id);
    return ESP_OK;
}

static uint32_t read_created_unix(const char *id)
{
    char path[LOGSTORE_PATH_MAX];
    if (build_path(id, path, sizeof path) != 0) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    logstore_file_hdr_t hdr = { 0, 0 };
    ssize_t r = read(fd, &hdr, sizeof hdr);
    close(fd);
    if (r != (ssize_t)sizeof hdr || hdr.magic != LOGSTORE_FILE_MAGIC) return 0;
    return hdr.created_unix;
}

esp_err_t logstore_init(size_t cap_bytes)
{
    assert(cap_bytes > 0);
    assert(cap_bytes <= UINT32_MAX);
    if (cap_bytes == 0 || cap_bytes > UINT32_MAX) return ESP_ERR_INVALID_ARG;

    s_ready        = false;
    s_cap_bytes    = (uint32_t)cap_bytes;
    s_cur_fd       = -1;
    s_cur_bytes    = 0;
    s_next_id      = 1;
    s_last_fsync_us = 0;   /* the first append fsyncs, making the new file visible promptly */

    if (!esp_littlefs_mounted(LOGS_PART_LABEL)) {
        esp_vfs_littlefs_conf_t conf = {
            .base_path              = LOGS_BASE_PATH,
            .partition_label        = LOGS_PART_LABEL,
            .partition              = NULL,
            .format_if_mount_failed = true,
            .read_only              = false,
            .dont_mount             = false,
            .grow_on_mount          = false,
        };
        esp_err_t e = esp_vfs_littlefs_register(&conf);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "mount %s: %s", LOGS_PART_LABEL, esp_err_to_name(e));
            return e;
        }
        ESP_LOGI(TAG, "mounted %s at %s", LOGS_PART_LABEL, LOGS_BASE_PATH);
    }

    /* Resume past whatever ids already exist (a prior boot's files) rather than colliding with
     * them; also drop anything already over cap_bytes (e.g. cap_bytes shrank across an upgrade). */
    logstore_file_info_t files[LOGSTORE_ROT_MAX_FILES];
    int n = scan_existing(files, LOGSTORE_ROT_MAX_FILES, &s_next_id);
    (void)n;
    /* Reserve the whole worst-case file we are about to open (M6), so the resumed set plus the new
     * file cannot exceed the cap even after the new file grows to LOGSTORE_MAX_FILE_BYTES. */
    enforce_cap((uint32_t)LOGSTORE_FILE_WORST);

    esp_err_t oe = open_new_file();
    if (oe != ESP_OK) return oe;

    s_ready = true;
    return ESP_OK;
}

int logstore_append(const lt_stream_rec_t *rec)
{
    assert(rec != NULL);
    if (!s_ready || !rec) return -1;
    assert(rec->len <= LT_REC_MAX);
    if (rec->len > LT_REC_MAX) return -1;

    uint32_t incoming = (uint32_t)(sizeof(logstore_rec_hdr_t) + rec->len);

    if (logstore_should_rotate(0, s_cur_bytes, incoming, s_cap_bytes, LOGSTORE_MAX_FILE_BYTES)) {
        if (s_cur_fd >= 0) {
            (void)fsync(s_cur_fd);   /* commit the file we are closing before it becomes read-only */
            close(s_cur_fd);
            s_cur_fd = -1;
        }
        /* Reserve the whole worst-case future file (header + per-file cap), not just this one
         * record: the new file grows to LOGSTORE_MAX_FILE_BYTES unchecked, so reserving only the
         * first record lets steady state overrun the partition and ENOSPC-wedge (M6). */
        enforce_cap((uint32_t)LOGSTORE_FILE_WORST);
        if (open_new_file() != ESP_OK) {
            s_ready = false;   /* out of files/space: stop accepting appends until re-init */
            return -1;
        }
    }

    logstore_rec_hdr_t hdr = {
        .ts_us = (uint64_t)esp_timer_get_time(),
        .seq   = rec->seq,
        .flags = rec->flags,
        .type  = rec->type,
        .len   = rec->len,
    };
    if (write_all(s_cur_fd, &hdr, sizeof hdr) != 0) return -1;
    if (rec->len > 0 && write_all(s_cur_fd, rec->data, rec->len) != 0) return -1;

    s_cur_bytes += incoming;

    /* Commit the live file at most ~once per second (M7): LittleFS persists size/metadata only on
     * sync/close, so without this GET /api/logs lists the current file at its 8 B header, reads of
     * it return stale/empty content, and a power cut (the very event a black box is for) loses up
     * to a whole file. Time-gated so a high append rate can't turn every record into a flash sync. */
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_fsync_us >= 1000000) {   /* >= 1 s */
        (void)fsync(s_cur_fd);
        s_last_fsync_us = now_us;
    }
    return 0;
}

int logstore_list(logstore_entry_t *out, int max)
{
    assert(out != NULL || max <= 0);
    if (max <= 0) return 0;

    logstore_file_info_t files[LOGSTORE_ROT_MAX_FILES];
    int n = scan_existing(files, LOGSTORE_ROT_MAX_FILES, NULL);   /* oldest-first */

    int want = (max < n) ? max : n;
    for (int i = 0; i < want; i++) {
        int j = n - 1 - i;   /* reverse: newest first, per logstore_entry_t's contract */
        snprintf(out[i].id, sizeof out[i].id, "%s", files[j].id);
        out[i].size         = files[j].bytes;
        out[i].created_unix = read_created_unix(files[j].id);
    }
    return want;
}

int logstore_open_read(const char *id, logstore_file_t *out)
{
    assert(id != NULL);
    assert(out != NULL);
    if (!id || !out) return -1;

    char path[LOGSTORE_PATH_MAX];
    if (build_path(id, path, sizeof path) != 0) return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "open_read %s: %s", path, strerror(errno));
        return -1;
    }
    *out = fd;
    return 0;
}
