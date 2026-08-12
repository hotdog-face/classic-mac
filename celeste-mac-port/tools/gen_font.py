#!/usr/bin/env python3
"""
gen_font.py -- extract the REAL PICO-8 system font from ccleste's font.bmp
(128x85 1bpp, 8x8 cells indexed by character code) and emit src/p8_font.h in
the format p8.c already consumes: 5 rows per glyph, low 3 bits = pixels,
bit2 = left column, 4px advance.

Faithful PICO-8 quirk preserved: the sheet's a..z cells hold the standard
"big" glyphs and A..Z hold the puny small-caps -- so lowercase strings (which
is what celeste prints) render exactly like PICO-8.

Usage: python3 gen_font.py <ccleste_dir> <out_dir>
"""
import struct
import sys

FIRST, LAST, ROWS = 32, 122, 5


def load_bmp_1bpp(path):
    d = open(path, 'rb').read()
    ofs = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bpp = struct.unpack('<H', d[28:30])[0]
    assert bpp == 1, f'expected 1bpp, got {bpp}'
    row_bytes = ((w + 31) // 32) * 4
    top_down = h < 0
    h = abs(h)
    px = [[0] * w for _ in range(h)]
    for y in range(h):
        sy = y if top_down else h - 1 - y
        row = d[ofs + sy * row_bytes: ofs + (sy + 1) * row_bytes]
        for x in range(w):
            px[y][x] = (row[x >> 3] >> (7 - (x & 7))) & 1
    return w, h, px


def main():
    src, out = sys.argv[1], sys.argv[2]
    w, h, px = load_bmp_1bpp(f'{src}/data/font.bmp')

    # decide glyph polarity: the 'a' cell (big A glyph) must have set pixels
    def cell_rows(code, invert):
        cx, cy = (code % 16) * 8, (code // 16) * 8
        rows = []
        for r in range(ROWS):
            bits = 0
            for c in range(3):
                v = px[cy + r][cx + c] if cy + r < h and cx + c < w else 0
                if invert:
                    v = 1 - v
                if v:
                    bits |= 1 << (2 - c)
            rows.append(bits)
        return rows

    invert = sum(cell_rows(ord('a'), False)) == 0
    glyphs = {code: cell_rows(code, invert) for code in range(FIRST, LAST + 1)}
    assert sum(glyphs[ord('a')]) > 0 and sum(glyphs[ord('!')]) > 0, 'bad polarity'

    with open(f'{out}/p8_font.h', 'w') as f:
        f.write(
            "/*\n"
            " * p8_font.h -- the REAL PICO-8 system font, extracted from the\n"
            " * cart's font sheet by tools/gen_font.py (do not edit by hand).\n"
            " * Each glyph is 5 rows; the low 3 bits of each row are the pixels\n"
            " * (bit2 = left column).  print() advances 4px per char.\n"
            " *\n"
            " * PICO-8 quirk kept: a..z cells hold the standard glyphs, A..Z the\n"
            " * puny small-caps -- lowercase text renders exactly like PICO-8.\n"
            " */\n"
            "#ifndef P8_FONT_H\n#define P8_FONT_H\n\n"
            f"#define FONT_FIRST {FIRST}\n#define FONT_LAST  {LAST}\n"
            f"#define FONT_ROWS  {ROWS}\n#define FONT_ADVANCE 4\n\n"
            "/* index by (ascii - FONT_FIRST) */\n"
            "static const unsigned char p8_font"
            "[FONT_LAST - FONT_FIRST + 1][FONT_ROWS] = {\n")
        for code in range(FIRST, LAST + 1):
            rows = ','.join(f'0b{b:03b}' for b in glyphs[code])
            name = chr(code) if code != 92 else 'backslash'
            f.write(f'    {{{rows}}},  /* {code} {name} */\n')
        f.write('};\n\n#endif /* P8_FONT_H */\n')
    print(f'extracted {LAST - FIRST + 1} glyphs (invert={invert})')


if __name__ == '__main__':
    main()
