#ifndef CORE_LAP_CFG_H
#define CORE_LAP_CFG_H
#include "core/lap.h"
#include "core/cfg.h"
/* §15.1/§15.2 (audit fix wave R24): project the user cfg's lap.* block onto the engine's
 * lap_cfg_t. Each field is accepted only inside its §15.1 range and otherwise falls back to the
 * consts.h default, so the engine is safe even on a cfg blob that never saw cfg_validate(). The
 * default_layout map is copied verbatim, truncated to LAP_MAX_DEFAULT_LAYOUTS. cfg NULL -> pure
 * defaults. This is the ONLY place core/lap.h and core/cfg.h meet. */
void lap_cfg_from_user(const cfg_t *cfg, lap_cfg_t *out);
#endif
