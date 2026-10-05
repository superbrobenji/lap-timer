# sim_common -- shared bench scenario profile for gps_sim/imu_sim (spec §4.6, bench checklist item
# 1). host/sim_profile.c is the pure drag profile (no IDF, host-tested by test/test_sim_profile.c);
# sim_scenario.c is a tiny runtime switch (current scenario + anchor time + lap replay count) both
# sims poll every gps_poll()/imu_read_fifo() call. Required unconditionally by app (pipeline.c
# calls sim_scenario_set() under #if CFG_GPS_SIM) and export_serial (dbg_sim reads SIM_SC_*), so it
# builds (and costs nothing) on a non-sim env too.
idf_component_register(
    SRCS "host/sim_profile.c" "sim_scenario.c"
    INCLUDE_DIRS "include"
    REQUIRES core)

# Same strictness as components/core (§17.9); conversion warnings non-fatal for IDF macros.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wconversion -Wmissing-prototypes -Wmissing-declarations)
