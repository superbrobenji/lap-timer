/* hal/storage.h -- storage HAL contract (spec §5.1, called from logger and cmd).
 *
 * The declarations below are the §5.1 storage.h slice verbatim; the include guard, the
 * <stdint.h>/<stddef.h> includes (size_t, fixed-width types) and the STO_* open-flag values
 * are added here so this is a self-contained, compilable header. The §5.1 excerpt names the
 * flags (STO_RD, STO_WR|STO_APPEND|STO_CREATE) but leaves their values to the header; they are
 * stable HAL-abstract bits the driver maps onto POSIX open() flags. All functions return int
 * (0 = OK, negative = -errno-style) unless noted; each is called from one task only (logger,
 * plus cmd for read-only listing/export in 3.5) and is not reentrant. The concrete backend is
 * components/drivers/storage_${STORAGE} (storage_internal = LittleFS, §13.1).
 *
 * Paths are backend-relative: "/sessions/<id>.log", "/sessions/<id>.sum", "/tracks/user.bin".
 * The driver maps them onto its mount point (storage_internal -> /lfs/...). Callers never embed
 * the mount base, so app code is backend-agnostic (storage_sd resolves the same paths on SD).
 */
#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>

/* Longest full path any caller can build ("/lfs" + backend-relative path); shared by the impl's
 * full_path() and by sto_iter_t.full below so the iterator's state can live on the header side. */
enum { STO_PATHMAX = 128 };

/* open() flag bits for sto_open (HAL-abstract; the driver maps them to O_* flags). */
enum {
    STO_RD     = 0x01,   /* read */
    STO_WR     = 0x02,   /* write */
    STO_APPEND = 0x04,   /* append; without it, STO_CREATE truncates an existing file */
    STO_CREATE = 0x08,   /* create if absent */
};

typedef struct { uint32_t total_kb, free_kb; uint8_t degraded; } sto_info_t;
typedef int sto_file_t;

int  sto_mount(void);                 /* ladder inside driver: mount -> retry -> format (internal) / degrade (sd) */
int  sto_info(sto_info_t *out);
int  sto_open(const char *path, int flags, sto_file_t *out);   /* flags: STO_RD, STO_WR|STO_APPEND|STO_CREATE */
int  sto_write(sto_file_t f, const void *buf, size_t n);
int  sto_read(sto_file_t f, void *buf, size_t n, size_t *n_read);
int  sto_seek(sto_file_t f, uint32_t offset);
int  sto_sync(sto_file_t f);
int  sto_close(sto_file_t f);
int  sto_rename(const char *from, const char *to);              /* atomic on LittleFS */
int  sto_unlink(const char *path);

/* rule 9: no function pointers -- sto_list is a pull iterator (sto_list_open/_next/_close)
 * instead of a per-entry callback. STO_NAME_MAX comfortably covers the longest name any caller
 * builds today (a session id is "S%05u_%03u" = 10 chars + ".log"/".sum" = 14 chars, +NUL). */
enum { STO_NAME_MAX = 32 };
typedef struct { char name[STO_NAME_MAX]; uint32_t size; } sto_entry_t;

/* Opaque-ish iterator state for sto_list_open/_next/_close: O(1) RAM, one entry read at a time
 * (rather than array-fill's O(entries) scratch buffer). `full` and `iters` are implementation
 * detail the caller never touches directly. */
typedef struct { DIR *d; char full[STO_PATHMAX]; int iters; } sto_iter_t;

/* Opens dir for streaming iteration via sto_list_next. Returns 0 on success, -1 on failure
 * (bad path or opendir() failure); *it is only valid after a 0 return. */
int  sto_list_open(sto_iter_t *it, const char *dir);
/* Advances to the next entry (skipping "." and ".."), filling *out on success. Returns 1 when
 * *out was filled, 0 when the directory (or the rule-2 STO_LIST_MAX_ENTRIES scan cap) is
 * exhausted, or -1 on error. Callers loop `while (sto_list_next(&it, &e) == 1) { ... }`. */
int  sto_list_next(sto_iter_t *it, sto_entry_t *out);
/* Closes the iterator opened by sto_list_open. Safe to call once after sto_list_next returns
 * 0 or -1 (and safe to call on an iterator that failed to open, since it->d is checked). */
void sto_list_close(sto_iter_t *it);
int  sto_probe(void);                 /* self-test: write+read+unlink probe file */

#endif /* HAL_STORAGE_H */
