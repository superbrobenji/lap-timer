/* devcontroller/components/linkhost/include/wire_escape.h -- wire_escape: pure, IDF-free escaping
 * for one console command-line argument sent over UART1 to the lap-timer's esp_console REPL
 * (Plan 5.6 Task 5 fix 1). Moved here from webapi/host/config_diff.c's config_diff_escape so that
 * linkhost's own `lt config set` relay (devconsole/cmd_lt.c) does not need to depend on the webapi
 * component just to escape a JSON payload -- the escaping itself has nothing to do with the web
 * API or config diffing; it belongs with linkhost, which owns the wire protocol both callers speak.
 *
 * The lap-timer's esp_console_split_argv STRIPS bare double quotes and treats '\\' as an escape
 * character, and an unescaped space ends an argument -- so a raw `{"units":"mph"}` argument would
 * arrive there as `{units:mph}` (malformed), and a string value containing a space would be split
 * into multiple argv tokens. wire_escape rewrites '"' -> '\\"', '\\' -> '\\\\' and ' ' -> '\\ ' so
 * the far end's tokenizer reconstructs the original bytes exactly. Used for `config set <json>`
 * (webapi.c's POST /api/config, via config_diff_next_line's budgeting, and cmd_lt.c's
 * `lt config set`) -- any future console command whose argument may itself contain '"'/'\\'/' '
 * can reuse it.
 */
#ifndef WIRE_ESCAPE_H
#define WIRE_ESCAPE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Escapes in[0..strlen(in)) into out[0..cap): '"', '\\' and ' ' each become two bytes ('\\' plus
 * the original char); every other byte passes through unchanged. On success writes a
 * NUL-terminated string to out and returns the escaped length (excluding the NUL).
 *
 * Reports overflow rather than truncating (pragmatic-P10: a half-escaped config payload sent over
 * the wire would corrupt the lap-timer's config, not just cosmetically truncate a debug string) --
 * returns 0 if in/out is NULL, cap == 0, or the escaped form (plus the NUL) would not fit cap; on
 * that path out's contents are unspecified (whatever was written before the overflow was
 * detected, not NUL-terminated -- never read out after a 0 return). A genuinely empty `in` also
 * returns 0 on success (out[0] = '\0'), which is why 0 doubles as the failure signal: this is only
 * unambiguous because this file's two real callers (webapi.c's POST /api/config and cmd_lt.c's
 * `lt config set`) both guard against ever passing an empty `in` before calling wire_escape. */
size_t wire_escape(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* WIRE_ESCAPE_H */
