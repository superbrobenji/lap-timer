#!/usr/bin/env python3
"""Generate components/core/ui/fonts.c from the bundled DejaVu Sans Mono Bold TTF (spec §20.2).

Rasterises each font's glyph set with Pillow, at a fixed pixel size per font, into a 1bpp cell of
the font's uniform monospace advance width. Each pixel is thresholded at 50% (>=128 of 0..255 is
ink). Output is deterministic: fixed TTF, fixed sizes, fixed character sets, fixed threshold, and
glyphs are emitted in ascending code-point order.

This is a manual maintenance tool, never run in CI or the build — the generated fonts.c is the
committed build input. Regenerate with:

    python3 tools/fonts/gen_fonts.py > components/core/ui/fonts.c

Requires Pillow (`pip install pillow`).
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from PIL import __version__ as PILLOW_VERSION

TTF_PATH = Path(__file__).resolve().parent / "DejaVuSansMono-Bold.ttf"
THRESHOLD = 128  # 50% of the 0..255 coverage Pillow renders into the 'L' canvas
REGEN_CMD = "python3 tools/fonts/gen_fonts.py > components/core/ui/fonts.c"

# (C name, px size passed to ImageFont.truetype, character set in spec order). Bitmaps are stored
# sorted by code point regardless of this order; the char->index map recovers the mapping.
FONT_SPECS = [
    ("FONT_BIG", 40, "0123456789:.-+S"),
    ("FONT_MED", 24, "0123456789:.-+" + "".join(chr(c) for c in range(ord("A"), ord("Z") + 1))),
    ("FONT_SMALL", 12, "".join(chr(c) for c in range(32, 127))),
]

NO_GLYPH = 0xFF


def rasterize_glyph(font, ch, w, h):
    """Render `ch` into a w x h cell (top-left anchored on the font's ascender line, Pillow's
    default 'la' anchor) and threshold to 1bpp, MSB-first, row-major bytes."""
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), ch, font=font, fill=255)
    stride = (w + 7) // 8
    px = img.load()
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if px[x, y] >= THRESHOLD:
                out[y * stride + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def build_font(c_name, px_size, chars):
    font = ImageFont.truetype(str(TTF_PATH), px_size)

    widths = {round(font.getlength(c)) for c in chars}
    if len(widths) != 1:
        sys.exit(f"{c_name}: character set has non-uniform advance width {widths} — not monospace at size {px_size}")
    w = widths.pop()
    h = px_size
    stride = (w + 7) // 8

    codepoints = sorted(ord(c) for c in chars)
    if len(set(codepoints)) != len(codepoints):
        sys.exit(f"{c_name}: duplicate character in set")
    first = codepoints[0]
    count = len(codepoints)
    if count > 255 or first > 255:
        sys.exit(f"{c_name}: first/count must fit uint8_t")

    index_map = [NO_GLYPH] * 128
    for idx, cp in enumerate(codepoints):
        if cp >= 128:
            sys.exit(f"{c_name}: non-ASCII code point U+{cp:04X} not supported")
        index_map[cp] = idx

    glyphs = [rasterize_glyph(font, chr(cp), w, h) for cp in codepoints]
    return {
        "name": c_name,
        "w": w,
        "h": h,
        "first": first,
        "count": count,
        "stride": stride,
        "codepoints": codepoints,
        "glyphs": glyphs,
        "index_map": index_map,
    }


def emit_bytes_rows(values, per_row=16):
    lines = []
    for i in range(0, len(values), per_row):
        row = ", ".join(f"0x{v:02x}" for v in values[i : i + per_row])
        lines.append(f"    {row},")
    return lines


def emit_font(lines, spec):
    name = spec["name"]
    bitmap_name = f"{name}_BITMAP"
    map_name = f"{name}_MAP"

    all_bytes = bytearray()
    for glyph in spec["glyphs"]:
        all_bytes.extend(glyph)

    lines.append(f"static const uint8_t {bitmap_name}[{len(all_bytes)}] = {{")
    for gi, cp in enumerate(spec["codepoints"]):
        glyph = spec["glyphs"][gi]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in glyph)
        lines.append(f"    /* U+{cp:04X} */ {hex_bytes},")
    lines.append("};")
    lines.append("")

    lines.append(f"static const uint8_t {map_name}[128] = {{")
    lines.extend(emit_bytes_rows(spec["index_map"]))
    lines.append("};")
    lines.append("")

    lines.append(f"const font_t {name} = {{")
    lines.append(
        f"    .w = {spec['w']}, .h = {spec['h']}, .first = {spec['first']}, "
        f".count = {spec['count']}, .stride = {spec['stride']},"
    )
    lines.append(f"    .bitmaps = {bitmap_name},")
    lines.append("};")
    lines.append("")
    return map_name


def main():
    if not TTF_PATH.exists():
        sys.exit(f"missing TTF: {TTF_PATH}")

    specs = [build_font(name, px, chars) for name, px, chars in FONT_SPECS]

    lines = []
    lines.append("/* GENERATED FILE — do not edit by hand.")
    lines.append(f" * Regenerate with: {REGEN_CMD}")
    lines.append(f" * Generator: tools/fonts/gen_fonts.py, Pillow {PILLOW_VERSION}, source tools/fonts/DejaVuSansMono-Bold.ttf")
    lines.append(" * 1bpp, MSB-first, row-major glyph cells; bit=1 is glyph ink; threshold 50% (>=128/255).")
    lines.append(" * See core/ui/fonts.h for the font_t layout and font_glyph_index() contract.")
    lines.append(" */")
    lines.append('#include "core/ui/fonts.h"')
    lines.append("")

    map_names = []
    for spec in specs:
        map_names.append((spec["name"], emit_font(lines, spec)))

    lines.append("int font_glyph_index(const font_t *font, char c)")
    lines.append("{")
    lines.append("    const uint8_t *map;")
    lines.append(f"    if (font == &{map_names[0][0]}) {{")
    lines.append(f"        map = {map_names[0][1]};")
    for fname, mname in map_names[1:]:
        lines.append(f"    }} else if (font == &{fname}) {{")
        lines.append(f"        map = {mname};")
    lines.append("    } else {")
    lines.append("        return -1;")
    lines.append("    }")
    lines.append("")
    lines.append("    uint8_t code = (uint8_t)c;")
    lines.append("    if (code >= 128U) {")
    lines.append("        return -1;")
    lines.append("    }")
    lines.append(f"    uint8_t idx = map[code];")
    lines.append(f"    return idx == 0xFFU ? -1 : (int)idx;")
    lines.append("}")
    lines.append("")

    sys.stdout.write("\n".join(lines))


if __name__ == "__main__":
    main()
