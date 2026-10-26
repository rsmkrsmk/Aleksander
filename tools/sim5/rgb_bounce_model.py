# Model maszyny stanow esp_lcd_panel_rgb.c (IDF 5.5.5) w trybie bounce, 6 porcji/klatke.
# restart_in_vsync=True odpowiada CONFIG_LCD_RGB_RESTART_IN_VSYNC=1 (rdzen Arduino 3.3.12).
# Uruchom: python3 rgb_bounce_model.py — pokazuje trwale przesuniecie [1,2,3,4,5,0] po jednym
# zgubionym przerwaniu EOF i to, ze esp_lcd_panel_init go nie naprawia. Poprawka v5.1: zerowanie
# bb_eof_count w callbacku VSYNC (patrz v5/hw_display.cpp).
N = 6
class Drv:
    def __init__(s, riv, count0=0):
        s.riv = riv; s.pos = 0; s.count = count0; s.b = [None, None]; s.wraps = []; s.t = 0
    def fill(s, i):
        s.b[i] = s.pos; s.pos += 1
        if s.pos >= N: s.pos = 0; s.wraps.append(s.t)
    def start_transmission(s):       # esp_lcd_panel_init (twarda resynchronizacja) — count NIE zerowany
        s.pos = 0; s.fill(0); s.fill(1)
    def vsync(s):
        if not s.riv:
            do = s.count < N; s.count = 0
            if not do: return
        if s.pos > 2: s.pos = 0
        if s.pos < 1: s.fill(0)
        if s.pos < 2: s.fill(1)
    def eof(s):
        bb = s.count % 2; s.count += 1; s.fill(bb)
def frame(d, lose_eof_at=None):
    shown = []
    for k in range(N):              # DMA czyta b0,b1,b0,... (restart zawsze od b0)
        d.t += 1; shown.append(d.b[k % 2])
        if k == lose_eof_at:        # dwa EOF zlane w jedno przerwanie (ISR spozniony > 1 porcja)
            continue
        d.eof()
    d.t += 1; d.vsync()
    return shown
for riv in (True, False):
    print("RESTART_IN_VSYNC =", riv)
    d = Drv(riv); d.start_transmission()
    for f in range(3): sh = frame(d)
    print("  normalnie:          ", sh)
    frame(d, lose_eof_at=2)
    for f in range(4): sh = frame(d)
    print("  po zgubionym EOF:   ", sh, " count%2 =", d.count % 2)
    d.start_transmission()          # nasza 'twarda resynchronizacja' po VSYNC
    for f in range(4): sh = frame(d)
    print("  po resync (init):   ", sh)
    d.count += 1                    # resync wykonany 'za pozno' (po 1. EOF klatki)
    d.start_transmission()
    for f in range(4): sh = frame(d)
    print("  resync przy nieparz:", sh)

# ---- Porownanie wariantow poprawki przy losowych zakloceniach (2% klatek) ----
import random
N = 6
class Drv:
    def __init__(s, mode):
        s.mode = mode; s.pos = 0; s.count = 0; s.b = [None, None]
    def fill(s, i):
        s.b[i] = s.pos; s.pos += 1
        if s.pos >= N:
            s.pos = 0
            if s.mode == 'wrap' and s.count % 2 == 1: s.count += 1   # poprawka w on_frame_buf_complete
    def start(s):                      # esp_lcd_panel_init (count nie zerowany)
        s.pos = 0; s.fill(0); s.fill(1)
    def vsync(s):
        if s.mode == 'vsync': s.count = 0
        if s.pos > 2: s.pos = 0
        if s.pos < 1: s.fill(0)
        if s.pos < 2: s.fill(1)
    def eof(s):
        bb = s.count % 2; s.count += 1; s.fill(bb)
def frame(d, ev):
    shown = []
    for k in range(N):
        shown.append(d.b[k % 2])
        if ev == ('lose', k): continue
        if ev == ('late',) and k == N - 1: continue
        if ev == ('init', k): d.start(); continue
        d.eof()
    d.vsync()
    if ev == ('late',): d.eof()
    return shown
ok = list(range(N))
for mode in ('none', 'vsync', 'wrap'):
    for kind in ('lose', 'late', 'init', 'mix'):
        random.seed(7); d = Drv(mode); d.start(); bad = 0; dist = 0
        for f in range(30000):
            ev = None
            if random.random() < 0.02:
                dist += 1
                k = kind if kind != 'mix' else random.choice(['lose', 'late', 'init'])
                ev = ('late',) if k == 'late' else (k, random.randrange(N))
            if frame(d, ev) != ok: bad += 1
        end = [frame(d, None) for _ in range(5)][-1]
        print(f"{mode:6s} {kind:5s} zaklocen {dist:4d} zlych klatek {bad:5d} koniec {'OK' if end == ok else end}")
