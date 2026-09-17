/* Hand-authored 12x12 1bpp status-strip icons (spec §20.2). See core/ui/icons.h for the
 * icon_id_t enum and the bit convention (MSB-first, row-major, bit=1 is icon ink). Each icon's
 * pixel art is reproduced as a comment above its byte rows ('#' = ink, '.' = background) so the
 * table can be maintained by eye; the two bytes per row cover columns 0-7 then 8-11 (top 4 bits
 * of the low byte's padding half unused). */
#include "core/ui/icons.h"

const uint8_t icon_bitmaps[ICON_COUNT][ICON_H][ICON_STRIDE] = {
    /* ICON_GPS — filled location pin (fix acquired)
     * ....####....   ...######...   ..########..   ..########..
     * ..########..   ..########..   ...######...   ....####....
     * ....####....   .....##.....   ............   ............
     */
    [ICON_GPS] = {
        { 0x0f, 0x00 }, { 0x1f, 0x80 }, { 0x3f, 0xc0 }, { 0x3f, 0xc0 },
        { 0x3f, 0xc0 }, { 0x3f, 0xc0 }, { 0x1f, 0x80 }, { 0x0f, 0x00 },
        { 0x0f, 0x00 }, { 0x06, 0x00 }, { 0x00, 0x00 }, { 0x00, 0x00 },
    },

    /* ICON_GPS_STRIKE — hollow GPS ring with a diagonal no-fix strike (SYS_GPS_NOFIX)
     * ##..####....   .###....#...   ..##.....#..   ..###....#..
     * ..#.##...#..   ..#..##..#..   ...#..###...   ....#####...
     * ........##..   .........##.   ..........##   ...........#
     */
    [ICON_GPS_STRIKE] = {
        { 0xcf, 0x00 }, { 0x70, 0x80 }, { 0x30, 0x40 }, { 0x38, 0x40 },
        { 0x2c, 0x40 }, { 0x26, 0x40 }, { 0x13, 0x80 }, { 0x0f, 0x80 },
        { 0x00, 0xc0 }, { 0x00, 0x60 }, { 0x00, 0x30 }, { 0x00, 0x10 },
    },

    /* ICON_IMU_Q — question mark (orientation/fusion quality disagreement)
     * ..######....   .##....##...   .#......#...   ........#...
     * ......##....   .....#......   ....#.......   ....#.......
     * ............   ....##......   ....##......   ............
     */
    [ICON_IMU_Q] = {
        { 0x3f, 0x00 }, { 0x61, 0x80 }, { 0x40, 0x80 }, { 0x00, 0x80 },
        { 0x03, 0x00 }, { 0x04, 0x00 }, { 0x08, 0x00 }, { 0x08, 0x00 },
        { 0x00, 0x00 }, { 0x0c, 0x00 }, { 0x0c, 0x00 }, { 0x00, 0x00 },
    },

    /* ICON_DISK — floppy disk, hollow shutter + label panels (storage OK)
     * ............   .##########.   .#........#.   .#.######.#.
     * .#.#....#.#.   .#.######.#.   .#........#.   .#.######.#.
     * .#.#....#.#.   .#.#....#.#.   .##########.   ............
     */
    [ICON_DISK] = {
        { 0x00, 0x00 }, { 0x7f, 0xe0 }, { 0x40, 0x20 }, { 0x5f, 0xa0 },
        { 0x50, 0xa0 }, { 0x5f, 0xa0 }, { 0x40, 0x20 }, { 0x5f, 0xa0 },
        { 0x50, 0xa0 }, { 0x50, 0xa0 }, { 0x7f, 0xe0 }, { 0x00, 0x00 },
    },

    /* ICON_DISK_FULL — same outline, panels filled solid (storage full)
     * ............   .##########.   .#........#.   .#.######.#.
     * .#.######.#.   .#.######.#.   .#........#.   .#.######.#.
     * .#.######.#.   .#.######.#.   .##########.   ............
     */
    [ICON_DISK_FULL] = {
        { 0x00, 0x00 }, { 0x7f, 0xe0 }, { 0x40, 0x20 }, { 0x5f, 0xa0 },
        { 0x5f, 0xa0 }, { 0x5f, 0xa0 }, { 0x40, 0x20 }, { 0x5f, 0xa0 },
        { 0x5f, 0xa0 }, { 0x5f, 0xa0 }, { 0x7f, 0xe0 }, { 0x00, 0x00 },
    },

    /* ICON_DISK_WARN — disk outline with an exclamation mark (storage warning)
     * ............   .##########.   .#........#.   .#...##...#.
     * .#...##...#.   .#...##...#.   .#...##...#.   .#........#.
     * .#...##...#.   .#........#.   .##########.   ............
     */
    [ICON_DISK_WARN] = {
        { 0x00, 0x00 }, { 0x7f, 0xe0 }, { 0x40, 0x20 }, { 0x46, 0x20 },
        { 0x46, 0x20 }, { 0x46, 0x20 }, { 0x46, 0x20 }, { 0x40, 0x20 },
        { 0x46, 0x20 }, { 0x40, 0x20 }, { 0x7f, 0xe0 }, { 0x00, 0x00 },
    },

    /* ICON_BATT_LOW — battery outline, single low-charge segment (SYS_BATT_LOW)
     * ....####....   ....####....   .##########.   .#........#.
     * .#........#.   .#........#.   .#........#.   .#........#.
     * .#..####..#.   .#..####..#.   .##########.   ............
     */
    [ICON_BATT_LOW] = {
        { 0x0f, 0x00 }, { 0x0f, 0x00 }, { 0x7f, 0xe0 }, { 0x40, 0x20 },
        { 0x40, 0x20 }, { 0x40, 0x20 }, { 0x40, 0x20 }, { 0x40, 0x20 },
        { 0x4f, 0x20 }, { 0x4f, 0x20 }, { 0x7f, 0xe0 }, { 0x00, 0x00 },
    },

    /* ICON_THERMOMETER — stem + bulb (SYS_DISP_TEMP_THROTTLE)
     * .....##.....   .....##.....   .....##.....   .....##.....
     * .....##.....   .....##.....   ....####....   ...######...
     * ..########..   ..########..   ..########..   ...######...
     */
    [ICON_THERMOMETER] = {
        { 0x06, 0x00 }, { 0x06, 0x00 }, { 0x06, 0x00 }, { 0x06, 0x00 },
        { 0x06, 0x00 }, { 0x06, 0x00 }, { 0x0f, 0x00 }, { 0x1f, 0x80 },
        { 0x3f, 0xc0 }, { 0x3f, 0xc0 }, { 0x3f, 0xc0 }, { 0x1f, 0x80 },
    },

    /* ICON_BLE — Bluetooth rune (connected/advertising)
     * ......#.....   ......##....   ......#.#...   ..#...#..#..
     * ...#..#.#...   ....#.#.....   ....#.#.....   ...#..#.#...
     * ..#...#..#..   ......#.#...   ......##....   ......#.....
     */
    [ICON_BLE] = {
        { 0x02, 0x00 }, { 0x03, 0x00 }, { 0x02, 0x80 }, { 0x22, 0x40 },
        { 0x12, 0x80 }, { 0x0a, 0x00 }, { 0x0a, 0x00 }, { 0x12, 0x80 },
        { 0x22, 0x40 }, { 0x02, 0x80 }, { 0x03, 0x00 }, { 0x02, 0x00 },
    },

    /* ICON_SAFE — padlock (safe mode)
     * ....####....   ...#....#...   ...#....#...   ...#....#...
     * .##########.   .#........#.   .#..####..#.   .#...##...#.
     * .#...##...#.   .#........#.   .##########.   ............
     */
    [ICON_SAFE] = {
        { 0x0f, 0x00 }, { 0x10, 0x80 }, { 0x10, 0x80 }, { 0x10, 0x80 },
        { 0x7f, 0xe0 }, { 0x40, 0x20 }, { 0x4f, 0x20 }, { 0x46, 0x20 },
        { 0x46, 0x20 }, { 0x40, 0x20 }, { 0x7f, 0xe0 }, { 0x00, 0x00 },
    },
};
