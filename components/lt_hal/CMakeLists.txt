# lt_hal -- HAL interface headers (spec §4.1/§5.1). Header-only INTERFACE component:
# drivers implement these contracts, app/ links against them, coupling lives only here.
#
# NOTE: the spec §4.2 names this directory components/hal, but ESP-IDF ships a built-in
# component named `hal`; a project component of the same name silently OVERRIDES it and
# breaks the build (every driver depends on IDF's hal). Renamed to lt_hal; the include
# prefix stays `hal/` (drivers do #include "hal/board.h"). See draft32 task 1 ruling.
idf_component_register(INCLUDE_DIRS include)
