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
    size_t i;
    /* fix round 1 finding 2: the scan is bounded by the loop's OWN condition against the constant
     * WWW_URI_MAX (i <= WWW_URI_MAX, so at most WWW_URI_MAX+1 bytes are ever examined) -- not by
     * reactively discovering, one index late, that a NUL-sentinel scan has run too far. A NUL found
     * at i == WWW_URI_MAX means the content is exactly WWW_URI_MAX bytes long (still in-contract);
     * reaching i == WWW_URI_MAX without ever seeing '\0' means no terminator was found within
     * bound and the URI is rejected as too long before any further byte is read. */
    for (i = 0; i <= WWW_URI_MAX; i++) {
        char c = uri[i];
        if (c == '\0') break;
        if (i == WWW_URI_MAX) return false;              /* no terminator within WWW_URI_MAX bytes */
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
