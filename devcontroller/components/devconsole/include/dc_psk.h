/* devcontroller/components/devconsole/include/dc_psk.h -- dc_psk: the dev-kit's SoftAP WPA2
 * passphrase, a 12-character value drawn from esp_random() and generated ONCE into NVS (F3 Task 5,
 * dk:H1 / R16).
 *
 * Replaces a PSK that was "ltdev-" + the eFuse MAC: the SoftAP BSSID broadcast in every beacon is
 * a published ESP-IDF function of that same base MAC, so anyone in radio range could invert the
 * beacon back to the password. A random value generated once and stored is not a function of
 * anything an attacker can read off the air.
 *
 * PINNED interface (host-tested: devcontroller/test/test_dc_psk.c, the pure half only --
 * dc_psk_get/dc_psk_reset need NVS/RNG and so are on-target only, exercised by dc status / dc psk
 * reset on hardware). IDF-free: only stdbool.h/stddef.h/stdint.h, so dc_psk_format/dc_psk_valid
 * compile into the host test harness's dchost library unmodified (devcontroller/test/CMakeLists.txt
 * globs each component's host directory into it). The NVS/RNG half lives in dc_psk.c, which is NOT
 * under host/ and so is target-only.
 */
#ifndef DC_PSK_H
#define DC_PSK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 12 characters * 5 bits/character (DC_PSK_ALPHABET has exactly 32 entries, so one random byte's
 * low 5 bits select a character with no bias and no rejection sampling needed) = 60 bits of
 * entropy, comfortably above WPA2's 8-character minimum and the point where range-limited radio
 * brute force stops being a practical threat. */
#define DC_PSK_LEN 12u

/* 32 unambiguous characters for a password an operator may have to read off a boot log and type
 * into a WiFi client: no '0'/'O', '1'/'I'/'l', and no lowercase (upper/lower look identical on
 * some fonts/labels). Exactly 32 entries -- dc_psk_format relies on that to map a byte's low 5
 * bits directly, with every character equally likely. */
#define DC_PSK_ALPHABET "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

/* Pure: maps up to DC_PSK_LEN bytes of rnd to DC_PSK_ALPHABET characters (one byte's low 5 bits
 * per output character) and NUL-terminates. Writes at most out_cap - 1 characters plus the NUL,
 * so a short out buffer truncates rather than overflows. n and out_cap may be anything; rnd may be
 * NULL only if n == 0. */
void dc_psk_format(const uint8_t *rnd, size_t n, char *out, size_t out_cap);

/* Pure: true iff s is exactly DC_PSK_LEN characters, every one of them in DC_PSK_ALPHABET, with a
 * NUL immediately after. Rejects NULL, the empty string, wrong length, and any value (such as the
 * old "ltdev-" + hex form) containing a character outside the alphabet. */
bool dc_psk_valid(const char *s);

/* Loads the dev-kit's AP passphrase from NVS ("dc" namespace, "psk" key), generating and storing
 * one on first boot (or if the stored value fails dc_psk_valid). out_cap must be >=
 * DC_PSK_LEN + 1; out is NUL-terminated on every return >= 0.
 *   0  - loaded (or newly generated) and persisted in NVS.
 *   1  - generated but NOT persisted (an NVS error); out is still a usable DC_PSK_LEN-character
 *        value for this boot only -- a reboot before the next successful write regenerates it.
 *  <0  - could not produce a value at all (out_cap too small); out is untouched. */
int dc_psk_get(char *out, size_t out_cap);

/* Regenerates the passphrase (independent of whatever is currently stored) and persists it to
 * NVS, for `dc psk reset`. Same out/out_cap contract and return codes as dc_psk_get. Safe to call
 * with the AP (and so RF) already running -- unlike dc_psk_get's first-boot path, this draws
 * entropy with esp_fill_random() directly rather than bootloader_random_enable()/_disable(),
 * which bootloader_random.h forbids once RF/ADC is up. */
int dc_psk_reset(char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* DC_PSK_H */
