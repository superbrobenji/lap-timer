/* config_diff.h -- pure, IDF-free config-diff/minify logic for POST /api/config
 * (Plan 5.5 Task 4 Step 1/3). Lives in components/webapi/host/config_diff.c so it links both
 * into the webapi IDF component (webapi.c) and the host test (devcontroller/test/test_config_diff.c)
 * -- it depends only on the vendored jsmn (webapi/host/jsmn.{c,h}) plus string.h, never esp_ or httpd.
 *
 * The lap-timer console's `config set` line is capped at max_cmdline_length (256 B) while
 * `config get` returns ~939 B, so POST /api/config diffs the desired document against the live
 * one and sends only the changed top-level keys, minified, over one or more `config set` lines.
 *
 * The actual esp-console escaping of one `config set` line (Plan 5.5 Task 4 Step 1 B1) moved to
 * devcontroller/components/linkhost/include/wire_escape.h's wire_escape() (Plan 5.6 Task 5 fix 1)
 * -- it is a wire-protocol concern shared with cmd_lt.c's `lt config set`, not a config-diff one.
 */
#ifndef CONFIG_DIFF_H
#define CONFIG_DIFF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Max bytes of one whole `config set <obj>` command line (console max_cmdline_length is 256 B;
 * Plan 5.5 Task 4 keeps every line <=250 B). */
#define CFG_SET_LINE_MAX   250
/* "config set " prefix length. */
#define CFG_SET_PREFIX_LEN 11
/* Max bytes of the JSON object argument that fits on one line after the prefix ("{...}"). */
#define CFG_SET_OBJ_MAX    (CFG_SET_LINE_MAX - CFG_SET_PREFIX_LEN)   /* 239 */
/* Max bytes of a single "\"key\":value" fragment (the object is "{" + fragment + "}"). */
#define CFG_SET_FRAG_MAX   (CFG_SET_OBJ_MAX - 2)                     /* 237 */

/* config_diff_minify's two negative return codes (dk:L1): a malformed POST body and an oversize
 * changed value used to both come back as a bare -1, so webapi.c could not tell "reject the
 * request" (400) apart from "reject this one value" (413). */
/* A buffer was too small to hold the result: insufficient out_cap, a single changed key's
 * "\"key\":value" fragment exceeds CFG_SET_FRAG_MAX (cannot fit one `config set` line), or either
 * document is SYNTACTICALLY VALID JSON that ran out of the parser's fixed internal token budget
 * (jsmn's JSMN_ERROR_NOMEM) -- that last case is a capacity limit, not a bad request -> 413. */
#define CFG_DIFF_E_SIZE  (-1)
/* current_json or desired_json was genuinely malformed JSON (jsmn JSMN_ERROR_INVAL/_PART, or an
 * empty parse), or was not a top-level JSON object -> 400. */
#define CFG_DIFF_E_PARSE (-2)

/* Diff two top-level JSON objects. Writes to out[out_cap] the minimal minified object holding
 * only the keys present in desired_json whose (whitespace-insensitive) value differs from
 * current_json, or that are absent from current_json. Keys only in current_json are ignored
 * (`config set` cannot delete keys). Returns the written length (NUL-terminated; ">=2", "{}"
 * when nothing changed), CFG_DIFF_E_PARSE if either document is genuinely malformed JSON or not a
 * top-level object, or CFG_DIFF_E_SIZE if out_cap is insufficient, a single changed key's
 * "\"key\":value" fragment exceeds CFG_SET_FRAG_MAX (cannot fit one `config set` line), or either
 * document is valid JSON that exceeds the parser's internal token budget (too complex, not
 * malformed) -- caller answers 413 for CFG_DIFF_E_SIZE either way. Not reentrant (uses static
 * scratch; callers are single-flight). */
int config_diff_minify(const char *current_json, const char *desired_json,
                       char *out, size_t out_cap);

/* Splits a minified object (as produced by config_diff_minify) into successive `config set`
 * object arguments, each "{...}" whose ESP-console-ESCAPED form (see linkhost/wire_escape.h's
 * wire_escape()) is <= CFG_SET_OBJ_MAX bytes, greedily packing whole key fragments. *cursor must
 * be 0 on the first call; each call writes the next (raw, un-escaped) object to out[out_cap] and
 * advances *cursor. Returns the raw length written (NUL-terminated), 0 when the whole object has
 * been consumed (out untouched -- also the first-call result for "{}"), or <0 if a single
 * fragment's escaped form cannot fit CFG_SET_OBJ_MAX / out_cap or the input is not a minified
 * object. Budgeting on the ESCAPED length keeps the final `config set <escaped-obj>` line within
 * the console's 256 B max_cmdline_length after wire_escape() expands quotes/backslashes/spaces. */
int config_diff_next_line(const char *obj, size_t *cursor, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_DIFF_H */
