/* dbg_console.c -- minimal diagnostics console for 3.2 (spec §18.4, exit-criterion command).
 *
 * IDF esp_console REPL on UART0 with one registered command, `dbg status`. The full §18.4
 * command set (status/list/open/... and the other dbg verbs) extends this same REPL in 3.5.
 */
#include "app/dbg_console.h"
#include "app/lt_nvs.h"
#include "app/lt_sup.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"

static int s_reset_reason;

static int cmd_dbg(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        const lt_counters_t *c = lt_counters();
        long long up = esp_timer_get_time() / 1000000;
        printf("boot count : %u\n", (unsigned)lt_nvs_boot_get());
        printf("reset      : %s (%d)\n", lt_reset_reason_str(s_reset_reason), s_reset_reason);
        printf("uptime     : %lld s\n", up);
        printf("counters   : boots=%u crashes=%u wdt=%u brownout=%u\n",
               (unsigned)c->boots, (unsigned)c->crashes, (unsigned)c->wdt, (unsigned)c->brownout);
        printf("sys_flags  : 0x%08x%s\n", (unsigned)sys_flags_get(),
               (sys_flags_get() & (1u << SYS_SAFE_MODE)) ? " [SAFE_MODE]" : "");
        for (int i = 0; i < HB_COUNT; i++) {
            printf("hb[%d]      : %u\n", i, (unsigned)g_hb[i]);
        }
        return 0;
    }
    printf("usage: dbg status\n");
    return 1;
}

void dbg_console_start(int reset_reason)
{
    s_reset_reason = reset_reason;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "laptimer>";
    repl_cfg.task_priority = 2;
    repl_cfg.task_stack_size = 4096;
    repl_cfg.max_cmdline_length = 128;
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    if (esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl) != ESP_OK) return;

    linenoiseSetDumbMode(1);   /* §18.4: line editing disabled (plain serial terminal) */

    const esp_console_cmd_t cmd = {
        .command = "dbg",
        .help = "diagnostics; 'dbg status' prints boot/reset/uptime/counters/flags/heartbeats",
        .hint = NULL,
        .func = cmd_dbg,
    };
    esp_console_cmd_register(&cmd);
    esp_console_start_repl(repl);
}
