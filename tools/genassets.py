#!/usr/bin/env python3
"""Generuje ui_assets.h (grafiki dla LVGL 9.3) z definicji SVG ponizej.
 - ikony duotone: dwie maski A8 (wypelnienie + kontur) -> kolorowane w LVGL (themable)
 - sprite'y kolorowe: ARGB8888
Renderowanie: Chromium (Playwright). Uzycie: genassets.py <wyjscie.h> [podglad.png]"""
import sys, io, re
import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright

# ---------------------------------------------------------------- IKONY duotone (siatka 24)
ICONS = {
 'home':   ('<path class="f" d="M4.5 11 12 4.5l7.5 6.5v8a1 1 0 0 1-1 1h-13a1 1 0 0 1-1-1Z"/><path d="M9.5 20.5v-5a1 1 0 0 1 1-1h3a1 1 0 0 1 1 1v5M3 11.5l9-8 9 8"/>'),
 'bottle': ('<path class="f" d="M8 9h8v10a2.5 2.5 0 0 1-2.5 2.5h-3A2.5 2.5 0 0 1 8 19Z"/><path d="M10 3.5h4M10.5 3.5v2.2M13.5 3.5v2.2M8.5 8.5c0-1.7 1.4-2.8 3.5-2.8s3.5 1.100 3.500 2.800M8 9h8v10a2.5 2.5 0 0 1-2.5 2.5h-3A2.5 2.5 0 0 1 8 19ZM8 13h2.500M8 16.500h2.500"/>'),
 'clock':  ('<circle class="f" cx="12" cy="12" r="8.500"/><path d="M12 7v5.200l3.300 2M12 3.500v1M12 19.500v1M3.500 12h1M19.500 12h1"/><circle cx="12" cy="12" r="8.500"/>'),
 'chart':  ('<rect class="f" x="4" y="12" width="4" height="8" rx="1.5"/><rect class="f" x="10" y="5" width="4" height="15" rx="1.5"/><rect class="f" x="16" y="9" width="4" height="11" rx="1.5"/>'),
 'weight': ('<rect class="f" x="3.5" y="4" width="17" height="16" rx="5"/><path d="M8 10.5a5 5 0 0 1 8 0M12 11l1.6-1.8"/><rect x="3.5" y="4" width="17" height="16" rx="5"/>'),
 'moon':   ('<path class="f" d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.500Z"/><path d="M16 3.500v3M14.500 5h3M19.500 9v2M18.500 10h2"/><path d="M20 14.500A8 8 0 0 1 9.500 4a8 8 0 1 0 10.500 10.500Z"/>'),
 'bath':   ('<path class="f" d="M3 12h18v2a5 5 0 0 1-5 5H8a5 5 0 0 1-5-5Z"/><path d="m7 19-1 2m11-2 1 2M6 12V6.5A2.5 2.5 0 0 1 8.5 4"/><circle cx="14" cy="7" r="1.3"/><circle cx="17.5" cy="9" r=".9"/><path d="M3 12h18v2a5 5 0 0 1-5 5H8a5 5 0 0 1-5-5Z"/>'),
 'feed':   ('<path class="f" d="M12 20.500C5 15.500 3.500 12 3.500 9a4.500 4.500 0 0 1 8.500-2 4.500 4.500 0 0 1 8.500 2c0 3-1.500 6.500-8.500 11.500Z"/><path d="M12 20.500C5 15.500 3.500 12 3.500 9a4.500 4.500 0 0 1 8.500-2 4.500 4.500 0 0 1 8.500 2c0 3-1.500 6.500-8.500 11.500Z"/>'),
 'diaper': ('<path class="f" d="M3.500 6.500h17v4.500c0 4.500-3.500 7-8.500 7s-8.500-2.500-8.500-7Z"/><path d="M3.500 6.500h17v4.500c0 4.500-3.500 7-8.500 7s-8.500-2.500-8.500-7ZM8.500 9.500v1M15.500 9.500v1M3.500 6.500c1 1.500 2.500 2 4.500 2M20.500 6.500c-1 1.500-2.500 2-4.500 2"/>'),
 'calendar':('<rect class="f" x="3.5" y="5" width="17" height="15" rx="3.5"/><path d="M8 3v4M16 3v4M3.5 10h17M8 14h.01M12 14h.01M16 14h.01M8 17h.01M12 17h.01"/>'),
 'wifi':   ('<path d="M3 9.500a13 13 0 0 1 18 0M6 13a8.500 8.500 0 0 1 12 0M9 16.500a4 4 0 0 1 6 0"/><circle class="f" cx="12" cy="19.500" r="1.500"/>'),
 'disk':   ('<rect class="f" x="4" y="3.500" width="16" height="17" rx="3"/><path d="M8 3.500v5h7v-5M8 20.500v-6h8v6"/><rect x="4" y="3.500" width="16" height="17" rx="3"/>'),
 'bulb':   ('<path class="f" d="M12 3.500a6 6 0 0 0-3.500 10.900c.7.600 1 1.300 1 2.100h5c0-.8.300-1.500 1-2.100A6 6 0 0 0 12 3.500Z"/><path d="M12 3.500a6 6 0 0 0-3.500 10.900c.7.600 1 1.300 1 2.100h5c0-.8.300-1.500 1-2.100A6 6 0 0 0 12 3.500ZM9.700 19h4.600M10.500 21h3"/>'),
 'shirt':  ('<path class="f" d="M8.500 4 3.500 7l2 4 2.500-1.200V20h8V9.800L18.500 11l2-4-5-3c-.5 1.500-1.700 2.300-3.500 2.300S9 5.500 8.500 4Z"/><path d="M8.500 4 3.500 7l2 4 2.500-1.200V20h8V9.800L18.500 11l2-4-5-3c-.5 1.500-1.700 2.300-3.500 2.300S9 5.500 8.500 4Z"/>'),
 'back':   ('<path d="M14.500 5.500 8 12l6.500 6.500"/>'),
 'drop':   ('<path class="f" d="M12 3.500C8 8 6 11 6 14.200a6 6 0 0 0 12 0C18 11 16 8 12 3.500Z"/><path d="M12 3.500C8 8 6 11 6 14.200a6 6 0 0 0 12 0C18 11 16 8 12 3.500Z"/>'),
 'zzz':    ('<path d="M5 5h6l-6 7h6M13 12h4.500l-4.500 5.500h4.500"/>'),
 'sun_s':  ('<circle class="f" cx="12" cy="12" r="4.500"/><circle cx="12" cy="12" r="4.500"/><path d="M12 2.500v2.500M12 19v2.500M2.500 12H5M19 12h2.500M5.300 5.300l1.800 1.800M16.900 16.900l1.800 1.800M5.300 18.700l1.800-1.800M16.900 7.100l1.800-1.800"/>'),
}

def icon_svg(body, px, layer):
    if layer == 'fill':
        # tylko elementy .f jako pelne
        els = re.findall(r'<[^>]*class="f"[^>]*/>', body)
        inner = ''.join(re.sub(r'\s*class="f"', ' fill="#000" stroke="none"', e) for e in els)
    else:
        inner = ''.join(re.sub(r'\s*class="f"', '', e) for e in re.findall(r'<[^>]*/>', body) if 'class="f"' not in e)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%d" height="%d" fill="none" stroke="#000" '
            'stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">%s</svg>' % (px, px, inner))

# ---------------------------------------------------------------- SPRITE'Y kolorowe (siatka podana w viewBox)
G = lambda i, a, b: '<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>' % (i, a, b)
R = lambda i, a, b, cx=.4, cy=.35, r=.75: '<radialGradient id="%s" cx="%s" cy="%s" r="%s"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></radialGradient>' % (i, cx, cy, r, a, b)

def rays():
    out = []
    import math
    for i in range(12):
        a = i * 30
        ln = 13 if i % 2 == 0 else 8
        out.append('<rect x="47" y="%d" width="6" height="%d" rx="3" fill="#F7B733" transform="rotate(%d 50 50)"/>' % (4 if i % 2 == 0 else 9, ln, a))
    return ''.join(out)

SPRITES = {
 # name: (w, h, svg_inner(viewBox 0 0 w h))
 'sun_disk': (100, 100, '<defs>%s</defs><circle cx="50" cy="50" r="40" fill="#FFE9A8" opacity=".35"/><circle cx="50" cy="50" r="27" fill="url(#s)"/><ellipse cx="42" cy="40" rx="9" ry="6" fill="#fff" opacity=".45" transform="rotate(-30 42 40)"/>' % R('s', '#FFE27A', '#F5A623')),
 'sun_rays': (100, 100, rays()),
 'moon':     (100, 100, '<defs>%s</defs><path d="M62 18A34 34 0 1 0 82 66 28 28 0 0 1 62 18Z" fill="url(#m)"/><circle cx="40" cy="58" r="5" fill="#E8C25A" opacity=".55"/><circle cx="52" cy="72" r="3.2" fill="#E8C25A" opacity=".55"/><circle cx="35" cy="42" r="3" fill="#E8C25A" opacity=".5"/>' % G('m', '#FFF3C4', '#F2CE6E')),
 'cloud':    (120, 76, '<defs>%s</defs><g fill="#B8CCE3" opacity=".9"><circle cx="38" cy="50" r="20"/><circle cx="62" cy="38" r="26"/><circle cx="88" cy="50" r="19"/><rect x="18" y="48" width="90" height="24" rx="12"/></g><g fill="url(#c)"><circle cx="38" cy="47" r="19"/><circle cx="62" cy="35" r="25"/><circle cx="88" cy="47" r="18"/><rect x="19" y="45" width="88" height="23" rx="11.5"/></g><ellipse cx="54" cy="26" rx="12" ry="6" fill="#fff" opacity=".7"/>' % G('c', '#FFFFFF', '#E3EDF8')),
 'cloud_d':  (120, 76, '<defs>%s</defs><g fill="#7E8FA6" opacity=".95"><circle cx="38" cy="50" r="20"/><circle cx="62" cy="38" r="26"/><circle cx="88" cy="50" r="19"/><rect x="18" y="48" width="90" height="24" rx="12"/></g><g fill="url(#c)"><circle cx="38" cy="47" r="19"/><circle cx="62" cy="35" r="25"/><circle cx="88" cy="47" r="18"/><rect x="19" y="45" width="88" height="23" rx="11.5"/></g><ellipse cx="54" cy="26" rx="12" ry="6" fill="#fff" opacity=".35"/>' % G('c', '#D4DEEA', '#9FB0C6')),
 'drop':     (16, 26, '<defs>%s</defs><path d="M8 1C4 8 2 12 2 17a6 6 0 0 0 12 0C14 12 12 8 8 1Z" fill="url(#d)"/><ellipse cx="5.500" cy="17" rx="1.600" ry="3" fill="#fff" opacity=".55"/>' % G('d', '#8DBCF7', '#4F86D6')),
 'flake':    (24, 24, '<g stroke="#FFFFFF" stroke-width="2.4" stroke-linecap="round"><path d="M12 2v20M3.300 7l17.400 10M3.300 17 20.700 7"/></g><g stroke="#AFC8E8" stroke-width="1" opacity=".8"><path d="M12 2v20M3.300 7l17.400 10M3.300 17 20.700 7"/></g><circle cx="12" cy="12" r="2.200" fill="#fff"/>'),
 'bolt':     (30, 48, '<defs>%s</defs><path d="M19 2 4 27h9l-4 19L26 20h-9Z" fill="url(#b)" stroke="#E08A1A" stroke-width="1.800" stroke-linejoin="round"/>' % G('b', '#FFE56A', '#FFB62E')),
 'star4':    (20, 20, '<path d="M10 0c.8 5.500 3.500 8.200 10 10-6.500 1.800-9.200 4.500-10 10-.8-5.500-3.500-8.200-10-10C6.500 8.200 9.200 5.500 10 0Z" fill="#FFF1C2"/>'),
 'glow':     (64, 64, '<defs><radialGradient id="g"><stop offset="0" stop-color="#fff" stop-opacity="1"/><stop offset=".35" stop-color="#fff" stop-opacity=".45"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient></defs><circle cx="32" cy="32" r="32" fill="url(#g)"/>'),
 'leaf':     (40, 40, '<defs>%s</defs><path d="M6 34C6 17 16 7 35 5c0 19-9 29-25 29Z" fill="url(#l)"/><path d="M6 34C14 22 22 15 31 10" stroke="#2E6B45" stroke-width="2" fill="none" stroke-linecap="round" opacity=".7"/>' % G('l', '#7DC58F', '#3F8C5A')),
 'bear':     (132, 132, '<defs>%s%s%s</defs><circle cx="30" cy="40" r="18" fill="url(#h)"/><circle cx="102" cy="40" r="18" fill="url(#h)"/><circle cx="30" cy="40" r="10" fill="#F5C7B0"/><circle cx="102" cy="40" r="10" fill="#F5C7B0"/><circle cx="66" cy="72" r="46" fill="url(#h)"/><ellipse cx="66" cy="88" rx="24" ry="19" fill="#F8DFC6"/><ellipse cx="66" cy="79" rx="8.500" ry="6" fill="#4A3328"/><ellipse cx="63.500" cy="77" rx="2.500" ry="1.500" fill="#fff" opacity=".6"/><path d="M58 94c3 4 13 4 16 0" stroke="#4A3328" stroke-width="2.800" fill="none" stroke-linecap="round" opacity="0"/><path d="M26 54C28 28 48 20 66 20s38 8 40 34C92 40 40 40 26 54Z" fill="url(#cap)"/><path d="M24 56c14-12 70-12 84 0" stroke="#2F6A45" stroke-width="9" fill="none" stroke-linecap="round"/><path d="M30 55c12-8 60-8 72 0" stroke="#4E9468" stroke-width="3" fill="none" stroke-linecap="round" opacity=".7"/><circle cx="66" cy="16" r="9" fill="#F4E2B8"/><circle cx="63" cy="13" r="3" fill="#fff" opacity=".6"/>' % (R('h', '#D49B6B', '#A8683F', .4, .3, .85), G('cap', '#5BA377', '#35794F'), '')),
 'rattle':   (112, 52, '<g stroke-linecap="round" stroke-linejoin="round"><path d="M33 31 47 47" stroke="#E8B6A0" stroke-width="6" fill="none"/><circle cx="52" cy="47" r="6" fill="none" stroke="#F08AA0" stroke-width="3.2"/><circle cx="25" cy="22" r="17" fill="#FFD36B" stroke="#E5A52F" stroke-width="2.4"/><path d="M13 15c7 3 17 3 24-1M12 28c8 4 18 4 26-1" fill="none" stroke="#FFF6DC" stroke-width="3"/><circle cx="22" cy="17" r="2.200" fill="#F08AA0"/><circle cx="31" cy="26" r="2.200" fill="#7CC7B8"/><circle cx="25" cy="5.500" r="2.600" fill="#F08AA0"/><circle cx="92" cy="16" r="10" fill="none" stroke="#F08AA0" stroke-width="4"/><ellipse cx="92" cy="34" rx="19" ry="10" fill="#9FD8CF" stroke="#58A89A" stroke-width="2.400"/><ellipse cx="92" cy="34" rx="8" ry="4.500" fill="#E6F6F2" stroke="#58A89A" stroke-width="1.600"/><circle cx="92" cy="47" r="6" fill="#FFD9C9" stroke="#E29B82" stroke-width="2.200"/></g>'),
}

MASKS = {  # maski A8 (kolorowane w LVGL)
 'pine': (40, 64, '<path d="M20 2 7 24h8L4 42h10L2 58h14v4h8v-4h14L26 42h10L25 24h8Z" fill="#000"/>'),
 'hill': (240, 70, '<path d="M0 70V34C40 10 90 2 140 14c40 10 70 26 100 4v52Z" fill="#000"/>'),
}

def render_all(jobs, scale=1):
    """jobs: lista (key, svg_str, w, h) -> dict key -> RGBA ndarray"""
    out = {}
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={'width': 400, 'height': 400}, device_scale_factor=1)
        for key, svg, w, h in jobs:
            pg.set_viewport_size({'width': max(w, 8), 'height': max(h, 8)})
            pg.set_content('<html><body style="margin:0;background:transparent">%s</body></html>' % svg)
            png = pg.screenshot(omit_background=True, clip={'x': 0, 'y': 0, 'width': w, 'height': h})
            out[key] = np.array(Image.open(io.BytesIO(png)).convert('RGBA'))
        b.close()
    return out

def c_array(name, data):
    flat = data.tobytes()
    lines = ["static const LV_ATTRIBUTE_LARGE_CONST uint8_t %s_map[] = {" % name]
    for i in range(0, len(flat), 24):
        lines.append("  " + ",".join(str(x) for x in flat[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)

INIT_LINES = []

def dsc(name, w, h, cf, bpp):
    # Bez designated initializers (kolejnosc pol zalezy od wersji LVGL) — pola ustawiane w ui_assets_init().
    INIT_LINES.append("  %s.header.magic = LV_IMAGE_HEADER_MAGIC; %s.header.cf = %s; %s.header.flags = 0; %s.header.w = %d; %s.header.h = %d; %s.header.stride = %d; %s.data_size = %d; %s.data = %s_map;"
                      % (name, name, cf, name, name, w, name, h, name, w * bpp, name, w * h * bpp, name, name))
    return "static lv_image_dsc_t %s;" % name

def main(out_h, preview=None):
    icon_sizes = {'s': 24, 'm': 32, 'l': 44}
    jobs = []
    for nm, body in ICONS.items():
        for sk, px in icon_sizes.items():
            if sk == 'l' and nm not in ('feed', 'bottle', 'moon', 'diaper', 'bath', 'weight', 'sun_s', 'drop', 'clock', 'calendar', 'bulb', 'shirt'): continue
            for layer in ('fill', 'line'):
                jobs.append(('ic_%s_%s_%s' % (nm, sk, layer), icon_svg(body, px, layer), px, px))
    for nm, (w, h, inner) in SPRITES.items():
        jobs.append(('sp_' + nm, '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">%s</svg>' % (w, h, w, h, inner), w, h))
    for nm, (w, h, inner) in MASKS.items():
        jobs.append(('mk_' + nm, '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">%s</svg>' % (w, h, w, h, inner), w, h))
    res = render_all(jobs)
    parts = ['/* WYGENEROWANE przez tools/genassets.py — nie edytuj recznie. */', '#pragma once', '#include "lvgl.h"', '']
    total = 0
    sheet_items = []
    for key, arr in res.items():
        h_, w_ = arr.shape[:2]
        if key.startswith(('ic_', 'mk_')):
            a = arr[:, :, 3].copy()
            parts.append(c_array(key, a)); parts.append(dsc(key, w_, h_, 'LV_COLOR_FORMAT_A8', 1)); total += a.size
        else:
            # ARGB8888 w pamieci LVGL (little endian) = B,G,R,A
            bgra = arr[:, :, [2, 1, 0, 3]].copy()
            parts.append(c_array(key, bgra)); parts.append(dsc(key, w_, h_, 'LV_COLOR_FORMAT_ARGB8888', 4)); total += bgra.size
        sheet_items.append((key, arr))
    parts.append("static void ui_assets_init() {\n  static bool done = false;\n  if (done) return;\n  done = true;\n" + "\n".join(INIT_LINES) + "\n}\n")
    open(out_h, 'w').write("\n".join(parts) + "\n")
    print('assets:', len(res), 'obrazow,', total // 1024, 'KB')
    if preview:
        W = 960; x = y = 6; rowh = 0
        sheet = Image.new('RGBA', (W, 900), (245, 237, 220, 255))
        for key, arr in sheet_items:
            if key.endswith('_line') and key.startswith('ic_') and not key.endswith('_l_line'): continue
            if key.startswith('ic_') and key.endswith('_fill') and not key.endswith('_l_fill'): continue
            im = Image.fromarray(arr)
            if key.startswith('ic_'):
                base = Image.new('RGBA', im.size, (0, 0, 0, 0))
                solid = Image.new('RGBA', im.size, (63, 122, 82, 255)); alpha = Image.fromarray(arr[:, :, 3])
                im = Image.new('RGBA', im.size, (0, 0, 0, 0)); im.paste(solid, mask=alpha)
            if x + im.width + 6 > W: x = 6; y += rowh + 6; rowh = 0
            sheet.alpha_composite(im, (x, y)); x += im.width + 6; rowh = max(rowh, im.height)
        sheet.crop((0, 0, W, y + rowh + 8)).convert('RGB').save(preview)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
