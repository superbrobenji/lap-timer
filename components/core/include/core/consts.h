#ifndef CORE_CONSTS_H
#define CORE_CONSTS_H
#define EARTH_R_M              6371008.8
#define G_MPS2                 9.80665
#define FUSION_HZ              100
#define LEAN_ALPHA             0.98f
#define LEAN_MAX_DEG           70.0f
#define G_MAX                  3.0f
#define LEAN_DISAGREE_DPS      10.0f
#define LEAN_DISAGREE_S        5
#define LEAN_REF_MIN_SPEED_MPS 3.0f
#define STILL_ACC_VAR          (0.02f * 0.02f)
#define STILL_GYRO_VAR         (2.0f * 2.0f)
#define STILL_WINDOW_S         2
#define BIAS_TEMP_STALE_C      15
#define FWD_LEARN_ACC_MPS2     1.5f
#define FWD_LEARN_MIN_S        1
#define FWD_LEARN_WINDOWS      3
#define TB_WINDOW_S            30
#define TB_LOCK_FIXES          10
#define TB_PPS_DISAGREE_US     50000LL
#define TB_PPS_STALE_US        5000000LL
#define FIX_HACC_MAX_M         15
#define FIX_MIN_SATS           5
#define FIX_MAX_SPEED_MPS      139
#define FIX_MAX_JUMP_MPS       250
#define FIX_LOST_COUNT         3
#define GATE_REARM_DIST_M      50.0
#define GATE_REARM_MIN_S       2
#define GATE_HALF_WIDTH_M      15.0
#define MIN_LAP_S              20
#define MAX_LAP_S              1800
#define VENUE_RADIUS_DEFAULT_M 2000
#define VENUE_LEAVE_FACTOR     1.5
#define VENUE_LEAVE_S          60
#define VENUE_SCAN_S           5
#define LAYOUT_LEN_TOL         0.15
#define PIT_SPEED_KMH          5
#define PIT_TIME_S             10
#define PRED_TABLE_MAX         600
#define DRAG_ARM_SPEED_KMH     0.5f
#define DRAG_ARM_STILL_S       2
#define DRAG_LAUNCH_G          0.15f
#define DRAG_LAUNCH_HOLD_MS    100
#define DRAG_LAUNCH_SCAN_G     0.05f
#define DRAG_ROLLOUT_M         0.3048
#define DRAG_TIMEOUT_S         60
#define DRAG_FALSE_START_S     2
#define TRAP_DIST_M            20.117
#define FIX_KEYFRAME_S         5
#define SES_SYNC               0xA5
#define SES_MAX_PAYLOAD        247
#define TRK_MAX_LAYOUTS        8
#define TRK_MAX_USER           4
#define MOVING_SPEED_KMH       3
#endif
