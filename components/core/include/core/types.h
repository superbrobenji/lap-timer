#ifndef CORE_TYPES_H
#define CORE_TYPES_H
#include <stdint.h>
#include <stdbool.h>

#define LAP_MAX_SECTORS 8
#define DRAG_MAX_GATES  16

/* GPS fix as delivered by the driver (spec §5.1) */
typedef struct {
    int64_t  gps_us;      /* UTC microseconds since Unix epoch; 0 if time invalid */
    int64_t  mono_us;     /* arrival time of the last byte of the message */
    int32_t  lat_e7;      /* degrees * 1e7 */
    int32_t  lon_e7;
    int32_t  alt_mm;      /* height above MSL */
    int32_t  gspeed_mms;  /* Doppler ground speed, mm/s */
    int32_t  head_e5;     /* heading of motion, degrees * 1e5, 0..36e6 */
    uint32_t hacc_mm;
    uint32_t sacc_mms;
    uint16_t pdop_e2;
    uint8_t  fix_type;    /* 0 none, 2 2D, 3 3D */
    uint8_t  sats;
    uint8_t  flags;       /* GPS_FLAG_* */
    uint8_t  valid;       /* set by the pipeline validity rule (§6.5) */
} gps_fix_t;
#define GPS_FLAG_FIXOK 0x01
#define GPS_FLAG_TIME  0x02
#define GPS_FLAG_DATE  0x04

typedef struct {
    int64_t mono_us;
    int16_t ax, ay, az;   /* raw LSB, ±16 g  → 2048 LSB/g */
    int16_t gx, gy, gz;   /* raw LSB, ±2000 dps → 16.4 LSB/dps */
} imu_raw_t;

typedef struct {
    int64_t mono_us;
    int64_t gps_us;       /* tb_mono_to_gps(mono_us) */
    float   g_lon, g_lat, g_comb;   /* g; +lat = right */
    float   lean_deg;               /* + = right */
    float   yaw_dps;                /* earth frame, + = left turn */
    uint8_t flags;                  /* FUS_* */
} fused_sample_t;
#define FUS_LEAN_VALID 0x01
#define FUS_ORIENT_OK  0x02
#define FUS_STILL      0x04
#define FUS_DISAGREE   0x08
#define FUS_CLAMPED    0x10
#define FUS_BIAS_STALE 0x20
#define FUS_SUSPECT    0x40

typedef struct {
    uint16_t max_speed_cms, min_speed_cms;
    int16_t  max_lean_l_cdeg, max_lean_r_cdeg;
    int16_t  max_glat_e3, max_gacc_e3, max_gbrake_e3;
} lap_stats_t;                       /* 14 bytes packed on the wire */

typedef struct {
    uint16_t    lap_no;
    int64_t     start_gps_us;
    uint32_t    time_ms;
    uint8_t     flags;               /* LAP_F_* */
    uint8_t     n_sectors;           /* number of splits = sector gates + 1 */
    uint32_t    sector_ms[LAP_MAX_SECTORS + 1];
    lap_stats_t stats;
} lap_result_t;
#define LAP_F_GPS_LOST    0x01
#define LAP_F_PIT         0x02
#define LAP_F_INCOMPLETE  0x04
#define LAP_F_OUT_LAP     0x08
#define LAP_F_INTERRUPTED 0x10
#define LAP_F_TOO_LONG    0x20
#define LAP_F_VALID       0x40

typedef struct { uint8_t gate_id; uint32_t time_ms; uint16_t speed_cms; uint32_t dist_cm; uint8_t hit; } drag_gate_res_t;
typedef struct {
    uint16_t        run_no;
    int64_t         t0_gps_us;
    uint8_t         flags;           /* DRAG_F_* */
    uint8_t         n_gates;
    uint16_t        trap_cms;
    drag_gate_res_t gates[DRAG_MAX_GATES];
} drag_result_t;
#define DRAG_F_ROLLOUT 0x01
#define DRAG_F_QUARTER 0x02          /* 1/4 mile reached */
#endif
