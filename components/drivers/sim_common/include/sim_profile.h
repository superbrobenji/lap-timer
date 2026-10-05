#ifndef SIM_PROFILE_H
#define SIM_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
/* Standing-start drag profile for the sim build (spec §4.6, bench checklist item 1). Pure: no state,
 * no allocation, built for the host harness and the ESP32 alike. Times are microseconds after the
 * scenario anchor; distance is along a straight line from the start point. */
#define SIM_DRAG_HOLD_US     3000000LL   /* standstill first: the drag engine ARMs on 2 s of stillness */
#define SIM_DRAG_ACCEL_MPS2  6.0         /* 0-100 km/h in 4.6 s, 0-200 km/h in 9.3 s (257 m) */
#define SIM_DRAG_VMAX_MPS    62.0        /* 223 km/h: past every default SPEED_FROM0 gate but 0-300 */
#define SIM_DRAG_RUN_M       450.0       /* keep going past the 1/4-mile trap (402.336 m) before braking */
#define SIM_DRAG_BRAKE_MPS2  8.0
typedef struct { double dist_m; double speed_mps; double accel_mps2; bool parked; } sim_drag_state_t;
void sim_drag_at(int64_t t_us, sim_drag_state_t *out);
#endif
