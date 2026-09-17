/* See pbm.h for the PBM (P4) format and the fb "0 = black" -> PBM "1 = black" inversion this file
 * applies. Host-test-only helper: free to use stdio/malloc, unlike components/core (pure C11, no
 * malloc after init). */
#include "pbm.h"

#include <stdio.h>

static int write_pbm_to(FILE *fp, const fb_t *fb)
{
    if (fprintf(fp, "P4\n%u %u\n", (unsigned)fb->w, (unsigned)fb->h) < 0) {
        return -1;
    }
    size_t n = (size_t)fb->stride * (size_t)fb->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t inverted = (uint8_t)~fb->bits[i]; /* fb: 0=black; PBM: 1=black */
        if (fputc(inverted, fp) == EOF) {
            return -1;
        }
    }
    return 0;
}

bool pbm_write(const char *path, const fb_t *fb)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        return false;
    }
    int wrote_ok = write_pbm_to(fp, fb) == 0;
    int closed_ok = fclose(fp) == 0;
    return wrote_ok && closed_ok;
}

bool pbm_eq_file(const char *path, const fb_t *fb)
{
    char header[32];
    int  header_len = snprintf(header, sizeof header, "P4\n%u %u\n", (unsigned)fb->w, (unsigned)fb->h);
    bool eq = header_len > 0 && (size_t)header_len < sizeof header;

    FILE *fp = eq ? fopen(path, "rb") : NULL;
    eq = eq && fp != NULL;

    for (int i = 0; eq && i < header_len; i++) {
        int c = fgetc(fp);
        eq = c != EOF && (char)c == header[i];
    }

    size_t n = (size_t)fb->stride * (size_t)fb->h;
    for (size_t i = 0; eq && i < n; i++) {
        int     c = fgetc(fp);
        uint8_t expected = (uint8_t)~fb->bits[i];
        eq = c != EOF && (uint8_t)c == expected;
    }

    if (eq) {
        eq = fgetc(fp) == EOF; /* no trailing bytes beyond the expected content */
    }
    if (fp != NULL) {
        fclose(fp);
    }

    if (!eq) {
        char actual_path[512];
        snprintf(actual_path, sizeof actual_path, "%s.actual.pbm", path);
        pbm_write(actual_path, fb);
    }
    return eq;
}
