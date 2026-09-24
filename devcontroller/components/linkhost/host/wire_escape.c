/* wire_escape.c -- see include/wire_escape.h. Pure, IDF-free (Plan 5.6 Task 5 fix 1; moved from
 * webapi/host/config_diff.c's config_diff_escape): no heap, no shared state. Built both into the
 * linkhost IDF component and linked directly by the host test
 * (devcontroller/test/test_wire_escape.c).
 */
#include "wire_escape.h"

#include <stdbool.h>
#include <string.h>

/* Defensive bound on the input length: wire_escape only ever escapes one console command-line
 * argument (a `config set` JSON payload today, well under 1 KB even for a large config), so this
 * just guards strnlen against scanning an unexpectedly huge/unterminated buffer -- never expected
 * to bind in practice. */
#define WIRE_ESCAPE_IN_MAX 2048u

/* True for the bytes wire_escape expands (each costs one extra byte on the console line): '"' and
 * '\\' would otherwise be consumed by the lap-timer's esp_console_split_argv; ' ' would end the
 * argument early. */
static bool wire_esc_char(char c)
{
    return c == '"' || c == '\\' || c == ' ';
}

size_t wire_escape(const char *in, char *out, size_t cap)
{
    if (!in || !out || cap == 0) return 0;
    size_t n = strnlen(in, WIRE_ESCAPE_IN_MAX + 1u);
    if (n > WIRE_ESCAPE_IN_MAX) return 0;

    size_t o = 0;
    for (size_t i = 0; i < n; i++) {                          /* bounded by n */
        char c = in[i];
        if (wire_esc_char(c)) {
            if (o + 2u >= cap) return 0;   /* would overflow -- reject rather than truncate */
            out[o++] = '\\';
            out[o++] = c;
        } else {
            if (o + 1u >= cap) return 0;
            out[o++] = c;
        }
    }
    out[o] = '\0';
    return o;
}
