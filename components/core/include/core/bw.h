#ifndef CORE_BW_H
#define CORE_BW_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Little-endian byte writer. On overflow nothing is written and the flag is set. */
typedef struct { uint8_t *p; size_t cap; size_t len; bool overflow; } bw_t;

static inline void bw_init(bw_t *w, uint8_t *buf, size_t cap) { w->p = buf; w->cap = cap; w->len = 0; w->overflow = false; }
static inline size_t bw_len(const bw_t *w) { return w->len; }
static inline bool bw_overflow(const bw_t *w) { return w->overflow; }
static inline void bw_bytes(bw_t *w, const void *src, size_t n)
{
    if (w->len + n > w->cap) { w->overflow = true; return; }
    memcpy(w->p + w->len, src, n); w->len += n;
}
static inline void bw_u8(bw_t *w, uint8_t v) { bw_bytes(w, &v, 1); }
static inline void bw_u16(bw_t *w, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bw_bytes(w, b, 2); }
static inline void bw_u32(bw_t *w, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; bw_bytes(w, b, 4); }
static inline void bw_u64(bw_t *w, uint64_t v) { bw_u32(w, (uint32_t)v); bw_u32(w, (uint32_t)(v >> 32)); }
static inline void bw_i16(bw_t *w, int16_t v) { bw_u16(w, (uint16_t)v); }
static inline void bw_i32(bw_t *w, int32_t v) { bw_u32(w, (uint32_t)v); }
static inline void bw_i64(bw_t *w, int64_t v) { bw_u64(w, (uint64_t)v); }

/* Little-endian byte reader. On underflow returns 0 and sets the flag. */
typedef struct { const uint8_t *p; size_t len; size_t pos; bool underflow; } br_t;

static inline void br_init(br_t *r, const uint8_t *buf, size_t len) { r->p = buf; r->len = len; r->pos = 0; r->underflow = false; }
static inline size_t br_remaining(const br_t *r) { return r->len - r->pos; }
static inline bool br_underflow(const br_t *r) { return r->underflow; }
static inline bool br_bytes(br_t *r, void *dst, size_t n)
{
    if (r->pos + n > r->len) { r->underflow = true; memset(dst, 0, n); return false; }
    memcpy(dst, r->p + r->pos, n); r->pos += n; return true;
}
static inline uint8_t br_u8(br_t *r) { uint8_t v; br_bytes(r, &v, 1); return v; }
static inline uint16_t br_u16(br_t *r) { uint8_t b[2]; br_bytes(r, b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }
static inline uint32_t br_u32(br_t *r) { uint8_t b[4]; br_bytes(r, b, 4); return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static inline uint64_t br_u64(br_t *r) { uint64_t lo = br_u32(r); uint64_t hi = br_u32(r); return lo | (hi << 32); }
static inline int16_t br_i16(br_t *r) { return (int16_t)br_u16(r); }
static inline int32_t br_i32(br_t *r) { return (int32_t)br_u32(r); }
static inline int64_t br_i64(br_t *r) { return (int64_t)br_u64(r); }
#endif
