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

#include <stddef.h>
#include <stdint.h>

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
int  sto_list(const char *dir, void (*cb)(const char *name, uint32_t size, void *ctx), void *ctx);
int  sto_probe(void);                 /* self-test: write+read+unlink probe file */

#endif /* HAL_STORAGE_H */
