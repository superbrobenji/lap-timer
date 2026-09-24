/* devcontroller/components/devconsole/console.c -- see include/console.h. The REPL scaffold: the ONE
 * esp_console registration door (console_register), the shared --json arg-stripping helper
 * (console_wants_json), and console_start (creates+starts the REPL on the console UART, then wires
 * up each command module). Copies the lap-timer's export_serial.c REPL bring-up pattern
 * (components/drivers/export_serial/export_serial.c:1044-1062) -- same esp_console_repl_config_t
 * shape, same "leave the RX ring at esp_console_new_repl_uart's default" call.
 */
#include "console.h"

#include <assert.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"

#include "cmd_dc.h"
#include "cmd_lt.h"
#include "cmd_stream.h"

static const char *TAG = "console";

static esp_console_repl_t *s_repl;
static int s_ncmd;   /* pragmatic-P10: bounded command table, asserted in console_register */

void console_register(const char *name, const char *help, int (*fn)(int, char **))
{
    assert(name != NULL);
    assert(fn != NULL);
    assert(s_ncmd < 32);
    const esp_console_cmd_t c = { .command = name, .help = help, .func = fn };
    ESP_ERROR_CHECK(esp_console_cmd_register(&c));
    s_ncmd++;
}

bool console_wants_json(int *argc, char **argv)
{
    assert(argc != NULL);
    assert(argv != NULL);
    if (*argc > 0 && strcmp(argv[*argc - 1], "--json") == 0) {
        (*argc)--;
        return true;
    }
    return false;
}

esp_err_t console_start(void)
{
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "devkit> ";
    rc.max_cmdline_length = 256;
    rc.task_stack_size = 6144;

    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t e = esp_console_new_repl_uart(&uc, &rc, &s_repl);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_new_repl_uart: %s", esp_err_to_name(e));
        return e;
    }

    cmd_dc_register();       /* T4 */
    cmd_lt_register();       /* T5 */
    cmd_stream_register();   /* T6 */
    /* later tasks append: cmd_selftest_register(); cmd_flash_register(); ... */

    ESP_LOGI(TAG, "REPL up on UART%d (%d cmd registered)", CONFIG_ESP_CONSOLE_UART_NUM, s_ncmd);
    return esp_console_start_repl(s_repl);
}
