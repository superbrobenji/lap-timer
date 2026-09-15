#ifndef CORE_CFG_H
#define CORE_CFG_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CFG_VERSION 1
enum { CFG_UNITS_KMH = 0, CFG_UNITS_MPH = 1 };
enum { CFG_MODE_LAP = 0, CFG_MODE_DRAG = 1 };
#define CFG_MAX_DEFAULT_LAYOUTS 8
#define CFG_MAX_BENCHES 4

typedef struct {
    uint8_t version;
    uint8_t units;                 /* CFG_UNITS_* */
    uint8_t mode;                  /* CFG_MODE_* */
    struct {
        uint16_t min_lap_s, max_lap_s, gate_rearm_m;
        uint8_t  pit_speed_kmh, pit_time_s;
        uint8_t  n_default_layout;
        struct { uint16_t venue, layout; } default_layout[CFG_MAX_DEFAULT_LAYOUTS];
    } lap;
    struct {
        uint16_t benches_kmh[CFG_MAX_BENCHES], benches_mph[CFG_MAX_BENCHES];
        uint8_t  n_kmh, n_mph;
        bool     rollout;
        uint8_t  launch_g_e2;
    } drag;
    struct { uint16_t pit_after_s, park_after_s, shutdown_mv, conn_idle_s; } power;
    struct { bool live_clock; uint8_t full_every; uint8_t rotation; bool invert; } display;
    struct { uint16_t adc_mv[2], true_mv[2]; } battery;   /* two-point calibration; identity when adc==true */
    struct { char name[16]; uint16_t adv_s; } ble;
    struct { uint8_t fused_hz; } log;
    struct { uint8_t dyn_model, rate_hz; } gps;
    struct { uint8_t mot_thr, mot_dur_ms; } imu;
} cfg_t;

int cfg_defaults(cfg_t *c);
int cfg_validate(cfg_t *c);                       /* clamps; returns number of corrected fields */
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap);   /* merge; 0 ok / -1 error */
int cfg_to_json(const cfg_t *c, char *out, size_t cap);                             /* bytes written or -1 */
int cfg_migrate(cfg_t *c, uint8_t from_version);                                    /* 0 ok / -1 unknown version */
#endif
