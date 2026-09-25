/* stage_bounds.c -- see include/stage_bounds.h. Pure arithmetic, no I/O, no shared state. */
#include "stage_bounds.h"

bool stage_bounds_write_overflows(uint32_t written, size_t pending, size_t n, uint32_t bound)
{
    return (uint64_t)written + (uint64_t)pending + (uint64_t)n > (uint64_t)bound;
}

bool stage_bounds_finish_ok(stage_bounds_mode_t mode, uint32_t written, uint32_t bound)
{
    if (mode == STAGE_BOUNDS_EXACT) return written == bound;
    return written <= bound;
}
