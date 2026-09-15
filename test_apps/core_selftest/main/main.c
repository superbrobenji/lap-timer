#include <stdio.h>
#include <stdint.h>
#include "unity.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Each host test file is compiled with main/setUp/tearDown renamed per suite (see CMakeLists.txt).
 * Unity calls the global setUp()/tearDown(); these dispatch to the suite currently running. */
typedef void (*hook_t)(void);
static hook_t cur_setup, cur_teardown;
void setUp(void) { if (cur_setup) cur_setup(); }
void tearDown(void) { if (cur_teardown) cur_teardown(); }

#define SUITE_DECL(name) int run_test_##name(void); void setUp_test_##name(void); void tearDown_test_##name(void);
SUITE_DECL(smoke) SUITE_DECL(bw) SUITE_DECL(ring) SUITE_DECL(geo) SUITE_DECL(tb) SUITE_DECL(ses_frame)
SUITE_DECL(ses_records) SUITE_DECL(jw) SUITE_DECL(cfg) SUITE_DECL(trk) SUITE_DECL(exp_vbo) SUITE_DECL(exp_nmea_json)

typedef struct { const char *name; int (*run)(void); hook_t setup, teardown; } suite_t;
#define SUITE(name) { #name, run_test_##name, setUp_test_##name, tearDown_test_##name }
static const suite_t suites[] = {
    SUITE(smoke), SUITE(bw), SUITE(ring), SUITE(geo), SUITE(tb), SUITE(ses_frame),
    SUITE(ses_records), SUITE(jw), SUITE(cfg), SUITE(trk), SUITE(exp_vbo), SUITE(exp_nmea_json),
};

void app_main(void)
{
    esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
    pcfg.stack_size = 6144; pcfg.prio = 5;
    esp_pthread_set_cfg(&pcfg);

    const size_t n = sizeof suites / sizeof suites[0];
    int failed = 0;
    printf("\n=== core_selftest: %u suites, free heap %u ===\n", (unsigned)n, (unsigned)esp_get_free_heap_size());
    for (size_t i = 0; i < n; i++) {
        cur_setup = suites[i].setup; cur_teardown = suites[i].teardown;
        printf("--- %s ---\n", suites[i].name);
        int64_t t0 = esp_timer_get_time();
        int r = suites[i].run();
        printf("--- %s: %s (%lld ms) ---\n", suites[i].name, r == 0 ? "OK" : "FAIL", (long long)((esp_timer_get_time() - t0) / 1000));
        if (r != 0) failed++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("=== core_selftest RESULT: %s, %d failing suites, free heap %u, min free %u ===\n",
           failed ? "FAIL" : "PASS", failed, (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
}
