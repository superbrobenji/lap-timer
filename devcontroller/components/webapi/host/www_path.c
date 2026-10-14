/* devcontroller/components/webapi/host/www_path.c -- see www_path.h. */
#include "www_path.h"

#include <stddef.h>

bool www_path_ok(const char *uri)
{
    if (uri == NULL || uri[0] != '/') return false;
    /* Whitelist, not a blacklist: the `www` partition holds SPA assets whose names this device
     * generates, so anything outside [A-Za-z0-9._/-] (and any empty or dot-only segment) is not a
     * real asset path and never reaches the filesystem (dk:L4). A query string is stripped by the
     * caller before this runs. */
    size_t seg = 0, dots = 0;
    for (size_t i = 0; uri[i] != '\0'; i++) {          /* bounded by the NUL (caller's URI) */
        char c = uri[i];
        if (i >= WWW_URI_MAX) return false;
        if (c == '/') {
            if (i > 0 && (seg == 0 || seg == dots)) return false;   /* "//", "/.", "/.." */
            seg = 0; dots = 0;
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok) return false;
        if (c == '.') dots++;
        seg++;
    }
    if (seg == 0 || seg == dots) return false;          /* trailing "/", "/." or "/.." */
    return true;
}
