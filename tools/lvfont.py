#!/usr/bin/env python3
"""Generator czcionek LVGL 9.3 (lv_font_fmt_txt, 4 bpp, plain) z plikow TTF (takze zmiennych).
Uzycie: lvfont.py <ttf> <waga_wght|-> <rozmiar_px> <nazwa_c> <plik_wyj.h> [--extra-ranges ...]
Nie wymaga lv_font_conv (node). Rasteryzacja: Pillow/FreeType."""
import sys, math
from PIL import ImageFont, Image

PL = "ĄĆĘŁŃÓŚŹŻąćęłńóśźż"
EXTRA = "°•–—…„”“’·×÷←→↑↓€"
ASCII = [chr(c) for c in range(0x20, 0x7F)]

def build(ttf, wght, size, name, out, only=None, bpp=4):
    font = ImageFont.truetype(ttf, size)
    if wght:
        vals = []
        for ax in font.get_variation_axes():
            n = ax['name'] if isinstance(ax['name'], str) else ax['name'].decode()
            vals.append(wght if n.lower().startswith('weight') else ax['default'])
        font.set_variation_by_axes(vals)
    chars = sorted(set(only if only else (''.join(ASCII) + PL + EXTRA)), key=ord)
    ascent, descent = font.getmetrics()
    line_h = ascent + descent
    base_line = descent
    maxv = (1 << bpp) - 1
    from PIL import ImageDraw
    bitmap = bytearray()
    glyphs = [(0, 0, 0, 0, 0, 0)]
    for ch in chars:
        adv = font.getlength(ch)
        bbox = font.getbbox(ch, anchor='ls')
        if ch == ' ' or bbox[2] <= bbox[0]:
            glyphs.append((len(bitmap), round(adv * 16), 0, 0, 0, 0)); continue
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).text((-x0, -y0), ch, font=font, fill=255, anchor='ls')
        px = im.load()
        bits = 0; nb = 0; start = len(bitmap)
        for yy in range(h):
            for xx in range(w):
                v = round(px[xx, yy] * maxv / 255)
                bits = (bits << bpp) | v; nb += bpp
                while nb >= 8:
                    bitmap.append((bits >> (nb - 8)) & 0xFF); nb -= 8
                    bits &= (1 << nb) - 1
        if nb:
            bitmap.append((bits << (8 - nb)) & 0xFF)
        glyphs.append((start, round(adv * 16), w, h, x0, -y1))
    codes = [ord(c) for c in chars]
    base = codes[0]
    L = []
    L.append(f"/* Czcionka {name}: {ttf.split('/')[-1]} wght={wght} {size}px {bpp}bpp (tools/lvfont.py) */")
    L.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t %s_bmp[] = {" % name)
    for i in range(0, len(bitmap), 16):
        L.append("  " + ",".join("0x%02x" % b for b in bitmap[i:i + 16]) + ",")
    L.append("};")
    L.append("static const lv_font_fmt_txt_glyph_dsc_t %s_gd[] = {" % name)
    for g in glyphs:
        L.append("  {.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d,.ofs_x=%d,.ofs_y=%d}," % g)
    L.append("};")
    L.append("static const uint16_t %s_ul[] = {%s};" % (name, ",".join(str(c - base) for c in codes)))
    L.append("static const lv_font_fmt_txt_cmap_t %s_cm[] = {{.range_start=%d,.range_length=%d,.glyph_id_start=1,.unicode_list=%s_ul,.glyph_id_ofs_list=NULL,.list_length=%d,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};" % (name, base, codes[-1] - base + 1, name, len(codes)))
    L.append("static const lv_font_fmt_txt_dsc_t %s_dsc = {.glyph_bitmap=%s_bmp,.glyph_dsc=%s_gd,.cmaps=%s_cm,.kern_dsc=NULL,.kern_scale=0,.cmap_num=1,.bpp=%d,.kern_classes=0,.bitmap_format=0};" % (name, name, name, name, bpp))
    L.append("const lv_font_t %s = {.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=%d,.base_line=%d,.subpx=LV_FONT_SUBPX_NONE,.underline_position=-1,.underline_thickness=1,.dsc=&%s_dsc};" % (name, line_h, base_line, name))
    return "\n".join(L) + "\n", len(bitmap)

FONTS = [  # (nazwa, plik, waga, px, tylko_znaki)
    ('ui_f13',  'ofl/nunito/Nunito[wght].ttf',  700, 13, None),
    ('ui_f15',  'ofl/nunito/Nunito[wght].ttf',  700, 15, None),
    ('ui_f18',  'ofl/nunito/Nunito[wght].ttf',  800, 18, None),
    ('ui_f24',  'ofl/fredoka/Fredoka[wdth,wght].ttf', 600, 24, None),
    ('ui_f32',  'ofl/fredoka/Fredoka[wdth,wght].ttf', 600, 32, None),
    ('ui_f48',  'ofl/fredoka/Fredoka[wdth,wght].ttf', 600, 48, None),
    ('ui_f80',  'ofl/fredoka/Fredoka[wdth,wght].ttf', 600, 80, ' 0123456789:-'),
]

if __name__ == '__main__':
    # tryb zbiorczy: lvfont.py --all <katalog_z_ofl> <wyjscie.h>
    if sys.argv[1] == '--all':
        root, outh = sys.argv[2], sys.argv[3]
        parts = ['/* WYGENEROWANE przez tools/lvfont.py (Nunito + Fredoka, licencja OFL) — nie edytuj recznie. */', '#pragma once', '#include "lvgl.h"', '']
        tot = 0
        for name, f, w, px, only in FONTS:
            txt, n = build(root + '/' + f, w, px, name, None, only)
            parts.append(txt); tot += n
            print(name, px, 'px', n, 'B')
        open(outh, 'w').write("\n".join(parts))
        print('razem', tot // 1024, 'KB bitmap')
    else:
        ttf, wght, size, name, out = sys.argv[1:6]
        t, n = build(ttf, None if wght == '-' else int(wght), int(size), name, out)
        open(out, 'w').write('#include "lvgl.h"\n' + t)
