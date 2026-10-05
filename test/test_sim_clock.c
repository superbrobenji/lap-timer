#include "unity.h"
#include "sim_clock.h"
#include <stdint.h>

/* test_sim_clock.c -- host coverage for sim_clock.c's pure gps_us bookkeeping (final review C1,
 * ruling F-1: "every dbg sim <scenario> rewinds the delivered GPS time base"). No sim_capture.h,
 * no gps_sim.c: a tiny synthetic 5-fix/1 Hz "capture" stands in for SIM_FIXES[] so this stays a
 * pure unit test of the arithmetic gps_sim.c's deliver_*() functions now delegate to.
 */

void setUp(void) {}
void tearDown(void) {}

#define CAP_N 5
static const int64_t CAP_ELAPSED[CAP_N] = { 0, 1000000, 2000000, 3000000, 4000000 }; /* 1 Hz, 4 s span */
#define CAP_SPAN_US      (CAP_ELAPSED[CAP_N - 1])
#define WRAP_PAUSE_US    6000000LL   /* mirrors gps_sim.c's SIM_LAPS_WRAP_PAUSE_US */
#define REPEAT_GAP_US    200000LL    /* mirrors gps_sim.c's inter-repeat 200 ms gap */
#define PARK_PERIOD_US   1000000LL   /* mirrors gps_sim.c's SIM_PARK_PERIOD_US */
#define DRAG_PERIOD_US   200000LL    /* mirrors gps_sim.c's SIM_DRAG_PERIOD_US -- the rearm step */

/* ---- direct formula checks (pin the exact arithmetic) ---- */

static void test_init_seeds_base_unchanged(void)
{
    sim_clock_t sc;
    sim_clock_init(&sc, 42000000);
    TEST_ASSERT_EQUAL_INT64(42000000, sim_clock_laps_us(&sc, 0, 0, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US));
    TEST_ASSERT_EQUAL_INT64(42000000, sim_clock_drag_us(&sc, 0));
    TEST_ASSERT_EQUAL_INT64(42000000, sim_clock_park_us(&sc, 0, PARK_PERIOD_US));
}

static void test_rearm_is_last_delivered_plus_step(void)
{
    sim_clock_t sc;
    sim_clock_init(&sc, 1000000);
    sim_clock_rearm(&sc, 9000000, DRAG_PERIOD_US);
    /* ruling F-1's letter: "the next scenario's first fix carries gps_us = last delivered gps_us
     * + 200000 (drag, laps and park alike)" -- checked here for all three. */
    TEST_ASSERT_EQUAL_INT64(9200000, sim_clock_drag_us(&sc, 0));
    TEST_ASSERT_EQUAL_INT64(9200000, sim_clock_park_us(&sc, 0, PARK_PERIOD_US));
    TEST_ASSERT_EQUAL_INT64(9200000, sim_clock_laps_us(&sc, 0, 0, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US));
    /* a second rearm off a LATER last-delivered value must not go backwards even if the step is
     * the same -- the anchor tracks whatever was actually last sent, not a running total. */
    sim_clock_rearm(&sc, 9200000 + CAP_SPAN_US, DRAG_PERIOD_US);
    TEST_ASSERT_TRUE(sim_clock_drag_us(&sc, 0) > 9200000);
}

static void test_laps_and_wrap_formula_matches_hand_computation(void)
{
    sim_clock_t sc;
    sim_clock_init(&sc, 5000000);
    /* repeat 1 (0-based), fix 2: base + cap_elapsed[2] + 1*(span+pause+gap) */
    int64_t off1 = CAP_SPAN_US + WRAP_PAUSE_US + REPEAT_GAP_US;
    int64_t want = 5000000 + CAP_ELAPSED[2] + off1;
    TEST_ASSERT_EQUAL_INT64(want, sim_clock_laps_us(&sc, CAP_ELAPSED[2], 1, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US));

    /* the repeat-0 last fix's own elapsed-from-base, fed back through sim_clock_elapsed_us(), must
     * round-trip exactly -- this is what deliver_wrap_pause() relies on for elapsed0. */
    int64_t last_fix_gps_us = sim_clock_laps_us(&sc, CAP_ELAPSED[CAP_N - 1], 0, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US);
    int64_t elapsed0 = sim_clock_elapsed_us(&sc, last_fix_gps_us);
    TEST_ASSERT_EQUAL_INT64(CAP_ELAPSED[CAP_N - 1], elapsed0);
    /* wrap tick 1 (1-based): base + elapsed0 + 1*period -- strictly past last_fix_gps_us, never a
     * duplicate of it (M3, folded into C1: the pre-increment fix). */
    int64_t wrap1 = sim_clock_wrap_us(&sc, elapsed0, 1, PARK_PERIOD_US);
    TEST_ASSERT_EQUAL_INT64(last_fix_gps_us + PARK_PERIOD_US, wrap1);
    TEST_ASSERT_TRUE(wrap1 > last_fix_gps_us);
}

/* ---- end-to-end sequence: the scenario under review ---- */

/* Walks `repeats` full LAPS replays of the synthetic capture, each followed (except the last) by
 * `wrap_ticks` wrap-pause fixes -- mirrors deliver_laps()/deliver_wrap_pause() exactly, minus the
 * real-time due-time gating (pacing; irrelevant to the gps_us values under test here). Asserts
 * every delivered gps_us is strictly greater than the one before, updating *last as it goes. */
static void walk_laps(sim_clock_t *sc, int64_t *last, uint16_t repeats, int wrap_ticks)
{
    for (uint16_t r = 0; r < repeats; r++) {
        for (int i = 0; i < CAP_N; i++) {
            int64_t g = sim_clock_laps_us(sc, CAP_ELAPSED[i], r, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US);
            TEST_ASSERT_TRUE_MESSAGE(g > *last, "laps fix must be strictly newer than the last delivered fix");
            *last = g;
        }
        if (r + 1 < repeats) {
            int64_t elapsed0 = sim_clock_elapsed_us(sc, *last);
            for (int t = 1; t <= wrap_ticks; t++) {
                int64_t g = sim_clock_wrap_us(sc, elapsed0, (uint32_t)t, PARK_PERIOD_US);
                TEST_ASSERT_TRUE_MESSAGE(g > *last, "wrap-pause tick must be strictly newer than the last delivered fix");
                *last = g;
            }
        }
    }
}

/* Proves strict gps_us monotonicity across LAPS(2 repeats, incl. the 6 s wrap pause) -> DRAG ->
 * PARK -> (re-issued) PARK -> LAPS, and that every gps_sim_rearm()-style re-anchor lands strictly
 * past the previous scenario's last delivered fix -- the exact regression final review C1 found
 * (every `dbg sim <scenario>` used to rewind gps_us, invalidating fixes for seconds to minutes). */
static void test_scenario_sequence_stays_monotonic_across_every_switch(void)
{
    sim_clock_t sc;
    const int64_t first_gps_us = 1700000000000LL;   /* arbitrary cold-boot capture origin */
    sim_clock_init(&sc, first_gps_us);
    int64_t last = first_gps_us - 1;   /* sentinel: the very first delivered fix must exceed this */

    /* LAPS: 2 repeats, a full 6 s wrap pause (6 x 1 Hz ticks) between them -- `dbg sim laps 2`. */
    walk_laps(&sc, &last, 2, 6);
    TEST_ASSERT_EQUAL_INT64(first_gps_us, sim_clock_laps_us(&sc, 0, 0, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US));

    /* -> DRAG (`dbg sim drag`): gps_sim_rearm()'s rule under test. */
    int64_t pre_rearm = last;
    sim_clock_rearm(&sc, last, DRAG_PERIOD_US);
    TEST_ASSERT_EQUAL_INT64(pre_rearm + DRAG_PERIOD_US, sim_clock_drag_us(&sc, 0));
    for (int64_t t = 0; t <= 2000000; t += DRAG_PERIOD_US) {
        int64_t g = sim_clock_drag_us(&sc, t);
        TEST_ASSERT_TRUE(g > last);
        last = g;
    }
    TEST_ASSERT_TRUE(last > pre_rearm);

    /* -> PARK (`dbg sim park`): same rule, then several 1 Hz parked ticks. */
    pre_rearm = last;
    sim_clock_rearm(&sc, last, DRAG_PERIOD_US);
    int64_t first_park = sim_clock_park_us(&sc, 0, PARK_PERIOD_US);
    TEST_ASSERT_EQUAL_INT64(pre_rearm + DRAG_PERIOD_US, first_park);
    TEST_ASSERT_TRUE(first_park > last);
    last = first_park;
    for (uint32_t tick = 1; tick <= 5; tick++) {
        int64_t g = sim_clock_park_us(&sc, tick, PARK_PERIOD_US);
        TEST_ASSERT_TRUE(g > last);
        last = g;
    }

    /* -> PARK again, re-issued while ALREADY parked -- the exact C1 regression ("re-issuing `dbg
     * sim park` ... rewinds gps_us by however long the previous park lasted"): must still land
     * strictly past the many parked ticks already delivered, not back near the park's own start. */
    pre_rearm = last;
    sim_clock_rearm(&sc, last, DRAG_PERIOD_US);
    int64_t reparked = sim_clock_park_us(&sc, 0, PARK_PERIOD_US);
    TEST_ASSERT_EQUAL_INT64(pre_rearm + DRAG_PERIOD_US, reparked);
    TEST_ASSERT_TRUE(reparked > last);
    last = reparked;

    /* -> LAPS again (`dbg sim laps`): a fresh 2-repeat replay from the capture's own fix 0 (cap
     * elapsed restarts at 0) must still be strictly newer than everything delivered so far. */
    pre_rearm = last;
    sim_clock_rearm(&sc, last, DRAG_PERIOD_US);
    TEST_ASSERT_EQUAL_INT64(pre_rearm + DRAG_PERIOD_US,
                             sim_clock_laps_us(&sc, 0, 0, CAP_SPAN_US, WRAP_PAUSE_US, REPEAT_GAP_US));
    walk_laps(&sc, &last, 2, 6);
    TEST_ASSERT_TRUE(last > pre_rearm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_seeds_base_unchanged);
    RUN_TEST(test_rearm_is_last_delivered_plus_step);
    RUN_TEST(test_laps_and_wrap_formula_matches_hand_computation);
    RUN_TEST(test_scenario_sequence_stays_monotonic_across_every_switch);
    return UNITY_END();
}
