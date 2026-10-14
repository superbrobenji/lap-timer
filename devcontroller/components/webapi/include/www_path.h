/* devcontroller/components/webapi/include/www_path.h -- the PURE static-asset path whitelist for
 * the catch-all GET handler (static_file, webapi.c) that serves the SPA out of the `www` LittleFS
 * partition (F3 Task 4 Step 3, dk:L4).
 *
 * static_file used to build "/www" + req->uri with no normalization at all and hand the result
 * straight to fopen(): a `..` segment reached LittleFS unexamined, and containment was incidental
 * (lfs clamps path resolution at the mount root) rather than something this code asserted. This is
 * a whitelist, not a blacklist, because the `www` partition only ever holds SPA asset names this
 * device's own build generates (www_path_ok's doc comment, www_path.c). No IDF dependency, so
 * devcontroller/test/test_www_path.c links it directly.
 */
#ifndef WWW_PATH_H
#define WWW_PATH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest URI (caller's local copy, query string already stripped, NUL excluded) www_path_ok
 * accepts. static_file's path[160] buffer holds WWW_BASE ("/www", 4 B) + the URI + NUL, so 128
 * leaves comfortable margin for both. */
#define WWW_URI_MAX 128u

/* True if `uri` is safe to append to the `www` mount root and open: starts with '/', every byte
 * is in [A-Za-z0-9._/-], no empty segment ("//"), no "." or ".." segment, and the whole string
 * (excluding the NUL) is no longer than WWW_URI_MAX. NULL and "" are rejected. The caller strips
 * any query string before calling this (a '?' is not in the whitelist, so an unstripped one would
 * always be rejected). */
bool www_path_ok(const char *uri);

#ifdef __cplusplus
}
#endif

#endif /* WWW_PATH_H */
