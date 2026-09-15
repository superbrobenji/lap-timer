#ifndef CORE_TB_H
#define CORE_TB_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t  half_min[2];          /* minimum offset seen in each half window */
    int64_t  half_start_mono[2];   /* mono_us when each half started */
    bool     half_valid[2];
    int      cur;                  /* index of the half currently being filled */
    uint32_t fixes;
    int64_t  filt_offset_us;       /* min over valid halves */
    int64_t  pps_offset_us;
    bool     pps_valid;
} tb_t;

int64_t tb_days_from_civil(int y, unsigned m, unsigned d);
int64_t tb_gps_us_from_utc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, int32_t nano);

void    tb_init(tb_t *t);
/* serial_time_us = len*10/baud of the message just received; subtracted from the arrival stamp */
void    tb_on_fix(tb_t *t, int64_t fix_gps_us, int64_t arrival_mono_us, int64_t serial_time_us);
void    tb_on_pps(tb_t *t, int64_t edge_mono_us, int64_t top_of_second_gps_us);
int64_t tb_mono_to_gps(const tb_t *t, int64_t mono_us);
bool    tb_locked(const tb_t *t);
uint8_t tb_quality(const tb_t *t);     /* 0 unlocked, 1 min-filter, 2 PPS */
#endif
