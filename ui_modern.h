// =============================================================================
//  ui_modern.h — nowoczesny interfejs urzadzenia "Lesna bajka" (LVGL 9.3)
//  Zawiera: motyw dzien/noc, ekran startowy (animowany), wygaszacz-tablice
//  informacyjna oraz widok wykresow (przelaczane karty). Plik nie zalezy od
//  reszty szkicu: dane dostaje przez struct UiModel, dzieki czemu da sie go
//  uruchomic i obejrzec na komputerze (symulator).
// =============================================================================
#pragma once
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_fonts.h"
#include "ui_assets.h"

// ------------------------------- Paleta / motyw ------------------------------
struct UiPal {
  bool night;
  lv_color_t bg, card, card2, brd, shade, ink, ink2, muted, faint;
  lv_color_t acc, acc2, feed, milk, sleep, bath, sun, danger, warn, good;
  lv_color_t skyA, skyB;  // gradient "nieba" w naglowku wygaszacza
};
static UiPal P;

static void uiApplyTheme(bool night) {
  ui_fonts_init(); ui_assets_init();
  P.night = night;
  if (!night) {
    P.bg = lv_color_hex(0xF5EDDC);  P.card = lv_color_hex(0xFFFAF0);  P.card2 = lv_color_hex(0xF3E8D0);
    P.brd = lv_color_hex(0xD9C7A3); P.shade = lv_color_hex(0xE6D8BA);
    P.ink = lv_color_hex(0x33281C); P.ink2 = lv_color_hex(0x4A3B29);
    P.muted = lv_color_hex(0x8A7658); P.faint = lv_color_hex(0xB0A083);
    P.acc = lv_color_hex(0x3F7A52);  P.acc2 = lv_color_hex(0x5A9A6D);
    P.feed = lv_color_hex(0xE98A4A); P.milk = lv_color_hex(0x5F8FD8);
    P.sleep = lv_color_hex(0x8F75D4); P.bath = lv_color_hex(0x4FB3A2);
    P.sun = lv_color_hex(0xD9A233);  P.danger = lv_color_hex(0xD4553F);
    P.warn = lv_color_hex(0xE2A93B); P.good = lv_color_hex(0x3F9A62);
    P.skyA = lv_color_hex(0xFFE7BF); P.skyB = lv_color_hex(0xFFF7E6);
  } else {
    P.bg = lv_color_hex(0x1B160F);  P.card = lv_color_hex(0x2A2218);  P.card2 = lv_color_hex(0x352B1E);
    P.brd = lv_color_hex(0x4D3F2A); P.shade = lv_color_hex(0x130F09);
    P.ink = lv_color_hex(0xF3E9D4); P.ink2 = lv_color_hex(0xD9CAA9);
    P.muted = lv_color_hex(0xA8987A); P.faint = lv_color_hex(0x7D6F56);
    P.acc = lv_color_hex(0x7CC08F);  P.acc2 = lv_color_hex(0x5A9A6D);
    P.feed = lv_color_hex(0xF2A56A); P.milk = lv_color_hex(0x8FB4EE);
    P.sleep = lv_color_hex(0xB9A3F0); P.bath = lv_color_hex(0x6FD0BF);
    P.sun = lv_color_hex(0xE8C46A);  P.danger = lv_color_hex(0xF0786A);
    P.warn = lv_color_hex(0xE8C46A); P.good = lv_color_hex(0x7CC08F);
    P.skyA = lv_color_hex(0x1C2A4A); P.skyB = lv_color_hex(0x33466E);
  }
}

// -------------------------------- Dane do wyswietlenia ------------------------
struct UiHour { bool has; uint8_t h, m; int temp; };
struct UiDay {              // jedna doba (do wykresow)
  char label[8];            // "18.10"
  int feed, milkMother, milkModified, milkMixed;
  int sleepDayMin, sleepNightMin;
  int wet, dirty; bool bath;
};
constexpr int UI_MAX_GAPS = 16;
constexpr int UI_RHYTHM_DAYS = 3;
constexpr int UI_RHYTHM_MAX = 16;
constexpr int UI_WEEKS = 10;
constexpr int UI_WPTS = 48;   // maks. punktow pomiarow wagi na wykresie
constexpr int UI_NIGHTS = 14;
struct UiModel {
  bool night = false;
  bool wifiOk = false, storageOk = false, timeOk = false;
  char clockHH[4] = "--", clockMM[4] = "--";
  char dateLine[40] = "";
  char ageText[48] = "";
  // karmienie
  bool hasLastFeed = false; int sinceFeedMin = 0; char nextFeedClock[8] = "";
  char lastFeedTxt[40] = ""; char lastBottleTxt[40] = "";
  int warnMin = 180, alarmMin = 240;
  // drzemka / sen
  bool sleeping = false; int sleepingMin = 0;
  char napLine[48] = ""; uint8_t napState = 0;  // 0 brak, 1 info, 2 zolty, 3 czerwony
  // statystyki dnia
  int feedCount = 0, longestGapMin = 0, avgGapMin = 0;
  char statVal[3][14] = {"-", "-", "-"};
  // pogoda
  bool weatherValid = false; int wmo = 0, temp = 0, feels = 0, tmin = 0, tmax = 0;
  char weatherDesc[40] = ""; char dress[120] = "";
  UiHour hours[3] = {};
  char tip[240] = "";
  char lastFeedEntry[48] = ""; char lastBottleEntry[48] = "";
  // 5 ostatnich dob (days[4] = dzis)
  UiDay days[5] = {};
  // przerwy dzis: poczatek (min od polnocy) + dlugosc (min) + otwarta przerwa
  int gapN = 0; int gapStart[UI_MAX_GAPS] = {}; int gapLen[UI_MAX_GAPS] = {}; bool gapOpen = false;
  // rytm 3 dni: pora karmien (min od polnocy); [0]=dzis, [1]=wczoraj, [2]=przedwczoraj
  int rhN[UI_RHYTHM_DAYS] = {}; int rhT[UI_RHYTHM_DAYS][UI_RHYTHM_MAX] = {};
  char rhLabel[UI_RHYTHM_DAYS][8] = {};
  // waga
  int weightN = 0; int weekVal[UI_WEEKS] = {}; bool weekHas[UI_WEEKS] = {}; bool weekPartial[UI_WEEKS] = {};
  int weekNo[UI_WEEKS] = {}; int lastWeightG = 0; char lastWeightDate[12] = "";
  // wykres wagi (dzien zycia + gramy) i statystyki przyrostu (kafelki na stronie "Waga")
  int wpN = 0; int wpDay[UI_WPTS] = {}; int wpG[UI_WPTS] = {};
  int wBirthG = 3080, wTodayDay = 0, wLastDay = 0;
  bool wHasSince = false; int wSince = 0, wGainTotal = 0;
  bool wHas14 = false; int w14 = 0;
  // sen nocny: minuty (-1 brak), poczatek i koniec (min od polnocy)
  int nightMin[UI_NIGHTS] = {}; int nightFrom[UI_NIGHTS] = {}; int nightTo[UI_NIGHTS] = {}; char nightLabel[UI_NIGHTS][8] = {};
  int sleepTodayDay = 0, sleepTodayNight = 0, sleepNeedDay = 0, sleepNeedNight = 0;
  // kapiele: miesiac biezacy
  char bathMonth[24] = ""; int bathFirstDow = 0, bathDays = 30, bathToday = 1; uint32_t bathMask = 0;
  int bathInterval = 3, bathSinceDays = -1, bathNextDay = 0; char bathLastTxt[32] = "", bathNextTxt[40] = "";
  // kafelki "dzis"
  int todayFeed = 0, todayMl = 0, todayDiapers = 0; bool todayBath = false;
};


// Typy pomocnicze dla firmware (wczytywanie CSV) — tutaj, aby prototypy generowane
// przez Arduino IDE widzialy je przed pierwsza funkcja szkicu.
// Duze, rzadko uzywane bufory trzymamy w PSRAM, nie w RAM wewnetrznym (.bss): wewnetrzny RAM
// jest deficytowy (TLS/Wi-Fi potrzebuja ciaglych blokow). Poza ESP (symulator) — zwykly calloc.
static inline void *ui_alloc_big(size_t bytes) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
  if (p) return p;
#endif
  return calloc(1, bytes);
}
struct UiRow { char date[11]; int hh; int mm; char type[24]; int ml; };
struct UiWeightPt { int day; int g; };

// ------------------------------- Pomocnicze rysowanie ------------------------
static inline lv_obj_t *ui_box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, lv_opa_t o = LV_OPA_COVER) {
  lv_obj_t *b = lv_obj_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, r, 0);
  lv_obj_set_style_bg_color(b, c, 0);
  lv_obj_set_style_bg_opa(b, o, 0);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  return b;
}
static inline void ui_grad(lv_obj_t *o, lv_color_t a, lv_color_t b) {
  lv_obj_set_style_bg_color(o, a, 0);
  lv_obj_set_style_bg_grad_color(o, b, 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}
static inline lv_obj_t *ui_label(lv_obj_t *p, const char *t, const lv_font_t *f, lv_color_t c, int x, int y,
                                 int w = 0, lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, al, 0);
  if (w > 0) { lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP); lv_obj_set_width(l, w); }
  lv_label_set_text(l, t);
  lv_obj_set_pos(l, x, y);
  return l;
}
static inline lv_obj_t *ui_img(lv_obj_t *p, const lv_image_dsc_t *d, int x, int y) {
  lv_obj_t *i = lv_image_create(p);
  lv_image_set_src(i, d);
  lv_obj_set_pos(i, x, y);
  lv_obj_remove_flag(i, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(i, LV_OBJ_FLAG_SCROLLABLE);
  return i;
}
static inline void ui_tint(lv_obj_t *img, lv_color_t c, lv_opa_t o = LV_OPA_COVER) {
  lv_obj_set_style_image_recolor(img, c, 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_set_style_image_opa(img, o, 0);
}
// Ikona duotone: wypelnienie (przezroczyste) + kontur, wspolne kolory motywu.
struct UiIcon { lv_obj_t *fill; lv_obj_t *line; };
static inline UiIcon ui_icon2(lv_obj_t *p, const lv_image_dsc_t *fillD, const lv_image_dsc_t *lineD, int x, int y,
                              lv_color_t c, lv_opa_t fillOpa = 70) {
  UiIcon ic;
  ic.fill = ui_img(p, fillD, x, y); ui_tint(ic.fill, c, fillOpa);
  ic.line = ui_img(p, lineD, x, y); ui_tint(ic.line, c);
  return ic;
}
#define UI_ICON(P_, NAME, SZ, X, Y, COL) ui_icon2(P_, &ic_##NAME##_##SZ##_fill, &ic_##NAME##_##SZ##_line, X, Y, COL)

// Karta: kremowy "papier" z twardym cieniem pod spodem (jak na stronie WWW).
static inline lv_obj_t *ui_card(lv_obj_t *p, int x, int y, int w, int h, int r = 22, bool shadow = true) {
  if (shadow) ui_box(p, x, y + 4, w, h, r, P.shade);
  lv_obj_t *c = ui_box(p, x, y, w, h, r, P.card);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_set_style_border_color(c, P.brd, 0);
  return c;
}

// ------------------------------- Animacje --------------------------------------
static void ui_cb_opa(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void ui_cb_iopa(void *o, int32_t v) { lv_obj_set_style_image_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void ui_cb_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void ui_cb_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }
static void ui_cb_w(void *o, int32_t v) { lv_obj_set_width((lv_obj_t *)o, v); }
static void ui_cb_h(void *o, int32_t v) { lv_obj_set_height((lv_obj_t *)o, v); }
static void ui_cb_rot(void *o, int32_t v) { lv_image_set_rotation((lv_obj_t *)o, v); }
static void ui_cb_scale(void *o, int32_t v) { lv_image_set_scale((lv_obj_t *)o, v); }
static void ui_cb_arcrot(void *o, int32_t v) { lv_arc_set_rotation((lv_obj_t *)o, v); }
static void ui_cb_arcval(void *o, int32_t v) { lv_arc_set_value((lv_obj_t *)o, v); }
static lv_anim_t *ui_anim_start(void *var, lv_anim_exec_xcb_t cb, int32_t a, int32_t b, uint32_t dur, uint32_t delay = 0,
                                lv_anim_path_cb_t path = lv_anim_path_ease_out, uint32_t playback = 0, bool loop = false) {
  lv_anim_t an;
  lv_anim_init(&an);
  lv_anim_set_var(&an, var);
  lv_anim_set_exec_cb(&an, cb);
  lv_anim_set_values(&an, a, b);
  lv_anim_set_duration(&an, dur);
  lv_anim_set_delay(&an, delay);
  lv_anim_set_path_cb(&an, path);
  if (playback) lv_anim_set_reverse_duration(&an, playback);
  if (loop) lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
  return lv_anim_start(&an);
}

// =============================================================================
//  EKRAN STARTOWY (animowany): nocny las, ksiezyc, swietliki, misiek w czapeczce,
//  lista krokow z animowanymi wskaznikami i pasek postepu synchronizacji.
// =============================================================================
constexpr uint8_t UI_BOOT_STEPS = 5;
static lv_obj_t *uiBootScr = nullptr;
static lv_obj_t *uiBootInd[UI_BOOT_STEPS];     // kontener wskaznika kroku
static lv_obj_t *uiBootTxt[UI_BOOT_STEPS];
static lv_obj_t *uiBootBarFill = nullptr, *uiBootPct = nullptr, *uiBootStatus = nullptr;
static lv_obj_t *uiBootEyeL = nullptr, *uiBootEyeR = nullptr, *uiBootHedge = nullptr, *uiBootSmile = nullptr;
static lv_timer_t *uiBootTimer = nullptr;
static int uiBootBarTarget = 0;
struct UiFly { lv_obj_t *img; float cx, cy, rx, ry, sp, ph; };
static UiFly uiFlies[8];
static uint32_t uiBootT0 = 0;
static const lv_point_precise_t UI_CHECK_PTS[3] = {{5, 11}, {9, 15}, {16, 7}};

static void ui_boot_tick(lv_timer_t *t) {
  (void)t;
  const float tm = lv_tick_get() * 0.001f;
  for (auto &f : uiFlies) {
    if (!f.img) continue;
    const int half = lv_obj_get_width(f.img) / 2;
    lv_obj_set_pos(f.img, (int)(f.cx + f.rx * sinf(tm * f.sp + f.ph)) - half, (int)(f.cy + f.ry * cosf(tm * f.sp * 0.7f + f.ph)) - half);
    const int o = 120 + (int)(110.f * (0.5f + 0.5f * sinf(tm * f.sp * 2.1f + f.ph * 2.f)));
    lv_obj_set_style_image_opa(f.img, (lv_opa_t)o, 0);
  }
}
static void ui_boot_blink_cb(lv_timer_t *t) {
  static uint8_t n = 0;
  (void)t;
  // mrugniecie: oczy splaszczaja sie (i obnizaja srodek) na ~140 ms
  for (lv_obj_t *e : {uiBootEyeL, uiBootEyeR}) {
    if (!e) continue;
    ui_anim_start(e, ui_cb_h, 15, 2, 70, 0, lv_anim_path_ease_in, 90);
    ui_anim_start(e, ui_cb_y, 78, 84, 70, 0, lv_anim_path_ease_in, 90);
  }
  // co drugi raz jez podskakuje z radosci
  if (uiBootHedge && (n++ & 1)) {
    ui_anim_start(uiBootHedge, ui_cb_y, 44, 30, 170, 150, lv_anim_path_ease_out, 230);
  }
}
static lv_timer_t *uiBootBlinkTimer = nullptr;

static void uiBootCreate() {
  ui_fonts_init(); ui_assets_init();
  uiBootScr = lv_obj_create(nullptr);
  lv_obj_t *s = uiBootScr;
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x0B1E1A), 0);
  lv_obj_set_style_bg_grad_color(s, lv_color_hex(0x225239), 0);
  lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_main_stop(s, 40, 0);
  lv_obj_set_style_bg_grad_stop(s, 255, 0);

  // gwiazdy (migocza z roznym rytmem)
  static const int16_t sx[] = {30, 78, 120, 176, 236, 290, 330, 392, 442, 60, 214, 420};
  static const int16_t sy[] = {40, 110, 26, 76, 18, 92, 150, 120, 52, 180, 140, 190};
  for (int i = 0; i < 12; ++i) {
    lv_obj_t *st = ui_img(s, &sp_star4, sx[i] * 1 + (i % 3) * 3 + (i > 7 ? 0 : 0), sy[i]);
    if (i % 2) lv_image_set_scale(st, 150);
    lv_obj_set_style_image_opa(st, 170, 0);
  }

  // ksiezyc z poswiata, delikatnie unosi sie
  lv_obj_t *glow = ui_img(s, &mk_glow_m, 410 - 70, 70 - 70);
  ui_tint(glow, lv_color_hex(0xFFE9A8), 60);
  lv_obj_t *moon = ui_img(s, &sp_moon, 410 - 50, 24);
  lv_image_set_scale(moon, 190);

  // wzgorza i sosny (sylwety)
  lv_obj_t *h1 = ui_img(s, &mk_hill_a, -160, 197); ui_tint(h1, lv_color_hex(0x1B4733));
  lv_obj_t *h2 = ui_img(s, &mk_hill_b, 39, 215); ui_tint(h2, lv_color_hex(0x163B2B));
  static const int16_t px_[] = {12, 52, 396, 436, 360, 24};
  static const int16_t py_[] = {188, 214, 196, 176, 226, 238};
  static const int16_t ps_[] = {420, 330, 380, 470, 300, 250};
  for (int i = 0; i < 6; ++i) {
    lv_obj_t *p = ui_img(s, &mk_pine, px_[i], py_[i]);
    lv_image_set_scale(p, ps_[i]);
    lv_image_set_pivot(p, 0, 0);
    ui_tint(p, lv_color_hex(i % 2 ? 0x12301F : 0x174030));
  }
  ui_box(s, 0, 296, 480, 184, 0, lv_color_hex(0x0F2A1F));

  // JEZ: wyskakuje z dolu, kolce "oddychaja", mruga, usmiecha sie i co chwile podskakuje
  lv_obj_t *halo = ui_img(s, &mk_glow_xl, 128, -2);
  ui_tint(halo, lv_color_hex(0xBFE8C8), 60);
  uiBootHedge = ui_box(s, 174, 44, 132, 132, 0, lv_color_black(), LV_OPA_TRANSP);
  lv_obj_t *quills = ui_img(uiBootHedge, &sp_hedge_q, 0, 0);
  lv_image_set_pivot(quills, 66, 100);
  ui_img(uiBootHedge, &sp_hedge_f, 0, 0);
  // twarz: oczy (z odblaskiem), rumience, usmiech
  uiBootEyeL = ui_box(uiBootHedge, 44, 78, 11, 15, 6, lv_color_hex(0x2B1E16));
  uiBootEyeR = ui_box(uiBootHedge, 77, 78, 11, 15, 6, lv_color_hex(0x2B1E16));
  ui_box(uiBootEyeL, 2, 2, 4, 5, 2, lv_color_white(), 220);
  ui_box(uiBootEyeR, 2, 2, 4, 5, 2, lv_color_white(), 220);
  lv_obj_t *chL = ui_box(uiBootHedge, 29, 93, 15, 10, 6, lv_color_hex(0xF08F8F), 150);
  lv_obj_t *chR = ui_box(uiBootHedge, 88, 93, 15, 10, 6, lv_color_hex(0xF08F8F), 150);
  ui_anim_start(chL, ui_cb_opa, 90, 190, 1100, 0, lv_anim_path_ease_in_out, 1100, true);
  ui_anim_start(chR, ui_cb_opa, 90, 190, 1100, 0, lv_anim_path_ease_in_out, 1100, true);
  uiBootSmile = lv_arc_create(uiBootHedge);
  lv_obj_set_size(uiBootSmile, 28, 28);
  lv_obj_set_pos(uiBootSmile, 52, 98);
  lv_arc_set_bg_angles(uiBootSmile, 25, 155);
  lv_arc_set_range(uiBootSmile, 0, 100); lv_arc_set_value(uiBootSmile, 0);
  lv_obj_remove_style(uiBootSmile, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(uiBootSmile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(uiBootSmile, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_color(uiBootSmile, lv_color_hex(0x5A3D2B), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(uiBootSmile, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(uiBootSmile, 0, LV_PART_INDICATOR);
  if (!uiBootBlinkTimer) uiBootBlinkTimer = lv_timer_create(ui_boot_blink_cb, 2600, nullptr);

  // tytul
  lv_obj_t *t1 = ui_label(s, "Leśny Dziennik", &ui_f24, lv_color_hex(0xBFE0C4), 0, 190, 480, LV_TEXT_ALIGN_CENTER);
  lv_obj_t *t2 = ui_label(s, "Aleksander", &ui_f48, lv_color_hex(0xFFF3D6), 0, 212, 480, LV_TEXT_ALIGN_CENTER);
  (void)t1; (void)t2;

  // lista krokow na ciemnej, polprzezroczystej karcie
  ui_box(s, 70, 282, 340, 134, 22, lv_color_hex(0x07150F), 150);
  static const char *names[UI_BOOT_STEPS] = {"Wi-Fi", "Zegar (NTP)", "Pamięć danych", "Pogoda", "Dane z panelu WWW"};
  for (int i = 0; i < UI_BOOT_STEPS; ++i) {
    const int y = 290 + i * 24;
    uiBootInd[i] = ui_box(s, 92, y + 1, 20, 20, 10, lv_color_hex(0x3C5A47));
    uiBootTxt[i] = ui_label(s, names[i], &ui_f15, lv_color_hex(0x9DB8A5), 124, y, 0);
    lv_obj_set_style_opa(uiBootTxt[i], 190, 0);
  }

  // pasek postepu: tor + wypelnienie z polyskiem
  ui_box(s, 80, 432, 320, 10, 5, lv_color_hex(0x07150F), 200);
  uiBootBarFill = ui_box(s, 80, 432, 0, 10, 5, lv_color_hex(0x7FDF8A));
  ui_grad(uiBootBarFill, lv_color_hex(0xA7F0B0), lv_color_hex(0x55C26A));
  uiBootPct = ui_label(s, "", &ui_f13, lv_color_hex(0xBFE0C4), 408, 428, 60);
  uiBootStatus = ui_label(s, "Uruchamianie…", &ui_f15, lv_color_hex(0xBFE0C4), 0, 450, 480, LV_TEXT_ALIGN_CENTER);

  // (swietliki usuniete na ekranie startowym: stabilnosc obrazu)
  lv_screen_load(s);
}

// stan: 0 = w toku (obracajacy sie luk), 1 = OK (zielone kolko z haczykiem), 2 = pominiete
static void uiBootStep(uint8_t idx, uint8_t state) {
  if (idx >= UI_BOOT_STEPS || !uiBootInd[idx]) return;
  lv_obj_t *ind = uiBootInd[idx];
  lv_obj_clean(ind);
  lv_obj_set_style_opa(uiBootTxt[idx], state == 2 ? 120 : 255, 0);
  if (state == 0) {
    lv_obj_set_style_bg_opa(ind, LV_OPA_TRANSP, 0);
    lv_obj_t *a = lv_arc_create(ind);
    lv_obj_set_size(a, 20, 20);
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_angles(a, 0, 100);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN); lv_obj_set_style_arc_color(a, lv_color_hex(0x3C5A47), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 3, LV_PART_INDICATOR); lv_obj_set_style_arc_color(a, lv_color_hex(0xF2D76B), LV_PART_INDICATOR);
    ui_anim_start(a, ui_cb_arcrot, 0, 360, 900, 0, lv_anim_path_linear, 0, true);
    lv_obj_set_style_text_color(uiBootTxt[idx], lv_color_hex(0xF2E6A8), 0);
  } else if (state == 1) {
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ind, lv_color_hex(0x59C27A), 0);
    lv_obj_t *ln = lv_line_create(ind);
    lv_line_set_points(ln, UI_CHECK_PTS, 3);
    lv_obj_set_style_line_width(ln, 3, 0);
    lv_obj_set_style_line_color(ln, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_obj_set_pos(ln, 0, 0);
    lv_obj_set_style_text_color(uiBootTxt[idx], lv_color_hex(0xC9F0D2), 0);
    // "pop" — wskaznik chwile sie powieksza
    ui_anim_start(ind, ui_cb_w, 8, 20, 260, 0, lv_anim_path_overshoot);
    ui_anim_start(ind, ui_cb_h, 8, 20, 260, 0, lv_anim_path_overshoot);
  } else {
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ind, lv_color_hex(0x4A5C50), 0);
    ui_box(ind, 5, 9, 10, 3, 2, lv_color_hex(0xA9B8AE));
    lv_obj_set_style_text_color(uiBootTxt[idx], lv_color_hex(0x8FA397), 0);
  }
}
static void uiBootProgress(int pct) {
  if (!uiBootBarFill) return;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  const int w = pct * 320 / 100;
  ui_anim_start(uiBootBarFill, ui_cb_w, lv_obj_get_width(uiBootBarFill), w, 350, 0, lv_anim_path_ease_out);
  uiBootBarTarget = pct;
  if (uiBootPct) lv_label_set_text_fmt(uiBootPct, "%d%%", pct);
}
static void uiBootSetStatus(const char *t) { if (uiBootStatus) lv_label_set_text(uiBootStatus, t); }
static void uiBootDestroy() {
  if (uiBootTimer) { lv_timer_delete(uiBootTimer); uiBootTimer = nullptr; }
  if (uiBootBlinkTimer) { lv_timer_delete(uiBootBlinkTimer); uiBootBlinkTimer = nullptr; }
  for (auto &f : uiFlies) f.img = nullptr;
  uiBootEyeL = uiBootEyeR = uiBootHedge = uiBootSmile = nullptr;
  if (uiBootScr) { lv_obj_delete(uiBootScr); uiBootScr = nullptr; }
  for (uint8_t i = 0; i < UI_BOOT_STEPS; ++i) { uiBootInd[i] = nullptr; uiBootTxt[i] = nullptr; }
  uiBootBarFill = uiBootPct = uiBootStatus = nullptr;
}

// =============================================================================
//  Wspolne: ikona pogody (animowana), pomocnicze ustawianie tekstu
// =============================================================================
static inline void ui_set(lv_obj_t *l, const char *t) {
  if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
static inline lv_obj_t *ui_img_c(lv_obj_t *p, const lv_image_dsc_t *d, int cx, int cy, int scale = 256) {
  lv_obj_t *i = ui_img(p, d, cx - (int)d->header.w / 2, cy - (int)d->header.h / 2);
  if (scale != 256) lv_image_set_scale(i, scale);
  return i;
}
// rodzaje pogody wg kodow WMO (jak w firmware)
enum UiWx { UW_SUN, UW_PARTLY, UW_CLOUD, UW_FOG, UW_RAIN, UW_SNOW, UW_STORM };
static UiWx ui_wx_kind(int c) {
  if (c == 0) return UW_SUN;
  if (c == 1 || c == 2) return UW_PARTLY;
  if (c == 3) return UW_CLOUD;
  if (c == 45 || c == 48) return UW_FOG;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return UW_RAIN;
  if (c >= 71 && c <= 77) return UW_SNOW;
  if (c >= 95) return UW_STORM;
  return UW_CLOUD;
}
static void ui_cb_bolt(void *o, int32_t v) {  // blysk: szybkie rozbłyski po dluzszej przerwie
  const int k = v % 100;
  lv_obj_set_style_image_opa((lv_obj_t *)o, k < 12 ? 255 : (k < 20 ? 40 : (k < 30 ? 255 : 0)), 0);
}
// Buduje animowana ikone pogody w pudelku ~104x92 (czyści poprzednia zawartosc).
static void uiWeatherIcon(lv_obj_t *box, int wmo, bool night, bool valid) {
  lv_obj_clean(box);
  const UiWx k = valid ? ui_wx_kind(wmo) : UW_CLOUD;
  auto sunAt = [&](int cx, int cy, int sc) {
    lv_obj_t *rays = ui_img_c(box, &sp_sun_rays, cx, cy, sc);
    (void)rays;   // bez obrotu: transformacja calego sprite co klatke kosztuje CPU/PSRAM na tablicy dzialajacej caly dzien
    lv_obj_t *disk = ui_img_c(box, &sp_sun_disk, cx, cy, sc);
    (void)disk;
  };
  auto moonAt = [&](int cx, int cy, int sc) {
    lv_obj_t *g = ui_img_c(box, &mk_glow_w, cx, cy);
    ui_tint(g, lv_color_hex(0xFFE9A8), 70);
    lv_obj_t *m = ui_img_c(box, &sp_moon, cx, cy, sc);
    (void)m;
  };
  auto stars = [&]() {
    static const int8_t xs[] = {8, 88, 92, 14}; static const int8_t ys[] = {14, 8, 62, 70};
    for (int i = 0; i < 4; ++i) {
      lv_obj_t *s = ui_img_c(box, &sp_star4, xs[i], ys[i], 160 + i * 20);
      if (i < 2) ui_anim_start(s, ui_cb_iopa, 40, 255, 1400 + i * 400, i * 300, lv_anim_path_ease_in_out, 1400 + i * 300, true);
    }
  };
  auto cloudAt = [&](const lv_image_dsc_t *d, int cx, int cy, int sc, int drift, uint32_t dur, uint32_t delay) {
    lv_obj_t *c = ui_img_c(box, d, cx, cy, sc);
    const int x0 = lv_obj_get_x(c);
    ui_anim_start(c, ui_cb_x, x0 - drift, x0 + drift, dur, delay, lv_anim_path_ease_in_out, dur, true);
    return c;
  };
  switch (k) {
    case UW_SUN:
      if (night) { moonAt(52, 46, 230); stars(); } else sunAt(52, 46, 220);
      break;
    case UW_PARTLY:
      if (night) moonAt(34, 34, 170); else sunAt(34, 32, 160);
      cloudAt(&sp_cloud, 58, 58, 200, 4, 3200, 0);
      break;
    case UW_CLOUD:
      cloudAt(&sp_cloud_d, 70, 34, 150, 5, 4200, 600);
      cloudAt(&sp_cloud, 46, 56, 210, 5, 3600, 0);
      break;
    case UW_FOG: {
      cloudAt(&sp_cloud, 52, 34, 200, 4, 3600, 0);
      static const int w_[3] = {70, 54, 64}; static const int x_[3] = {18, 34, 22};
      for (int i = 0; i < 3; ++i) {
        lv_obj_t *b = ui_box(box, x_[i], 60 + i * 11, w_[i], 6, 3, night ? lv_color_hex(0x9FB0C4) : lv_color_hex(0xA9B8CC));
      }
      break;
    }
    case UW_RAIN: {
      cloudAt(&sp_cloud_d, 52, 32, 210, 3, 4000, 0);
      static const int8_t xs[5] = {26, 40, 54, 68, 82};
      for (int i = 0; i < 5; i += 2) {   // 3 krople zamiast 5
        lv_obj_t *d = ui_img_c(box, &sp_drop, xs[i], 58, 200);
        ui_anim_start(d, ui_cb_y, 52, 84, 800 + (i % 3) * 140, i * 170, lv_anim_path_ease_in, 0, true);
        ui_anim_start(d, ui_cb_iopa, 0, 255, 400 + (i % 3) * 70, i * 170, lv_anim_path_ease_out, 400 + (i % 3) * 70, true);
      }
      break;
    }
    case UW_SNOW: {
      cloudAt(&sp_cloud, 52, 32, 210, 3, 4000, 0);
      static const int8_t xs[5] = {26, 40, 54, 68, 82};
      for (int i = 0; i < 5; i += 2) {   // 3 platki, bez obrotu i bocznego dryfu
        lv_obj_t *f = ui_img_c(box, &sp_flake, xs[i], 58, 200);
        ui_anim_start(f, ui_cb_y, 52, 86, 1900 + i * 160, i * 330, lv_anim_path_linear, 0, true);
      }
      break;
    }
    case UW_STORM: {
      cloudAt(&sp_cloud_d, 52, 30, 220, 3, 4200, 0);
      lv_obj_t *b = ui_img_c(box, &sp_bolt, 52, 68, 200);
      lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, b); lv_anim_set_exec_cb(&a, ui_cb_bolt);
      lv_anim_set_values(&a, 0, 99); lv_anim_set_duration(&a, 600); lv_anim_set_repeat_delay(&a, 1800);
      lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_set_path_cb(&a, lv_anim_path_linear);
      lv_anim_start(&a);
      break;
    }
  }
  if (!valid) lv_obj_set_style_opa(box, 110, 0); else lv_obj_set_style_opa(box, LV_OPA_COVER, 0);
}

// =============================================================================
//  WYGASZACZ — tablica informacyjna (pierwszy ekran urzadzenia)
// =============================================================================
// Zamiana ekranu: nowy jest juz zbudowany; jesli stary byl aktywny, najpierw ladujemy nowy.
static void ui_retire_screen(lv_obj_t *old, lv_obj_t *neu) {
  if (!old) return;
  if (lv_screen_active() == old && neu) lv_screen_load(neu);
  lv_obj_delete(old);
}

static lv_obj_t *uiSaverScr = nullptr;
static lv_obj_t *uiSvClockH, *uiSvColon, *uiSvClockM, *uiSvDate, *uiSvNapIcon, *uiSvNap;
static lv_obj_t *uiSvWxBox, *uiSvTemp, *uiSvDesc, *uiSvMinMax;
static lv_obj_t *uiSvRing, *uiSvRingTxt, *uiSvFeedCap, *uiSvFeedMain, *uiSvFeedNext;
static lv_obj_t *uiSvStatVal[3];
static lv_obj_t *uiSvHourT[3], *uiSvHourV[3], *uiSvDress, *uiSvTip;
static int uiSvWmo = -999; static bool uiSvWxValid = false, uiSvWxNight = false;
static bool uiSvAlarmAnim = false;
static int uiSvRingPrev = -1;

static void ui_clock_place() {  // uklada HH : MM wg rzeczywistych szerokosci cyfr (cyfry sa proporcjonalne)
  lv_point_t a, c;
  lv_text_get_size(&a, lv_label_get_text(uiSvClockH), &ui_f80, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_text_get_size(&c, ":", &ui_f80, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_set_x(uiSvColon, 24 + a.x + 2);
  lv_obj_set_x(uiSvClockM, 24 + a.x + c.x + 4);
}

static void uiSaverCreate(bool night) {
  lv_obj_t *oldScr = uiSaverScr; uiSaverScr = nullptr;
  uiSvWmo = -999; uiSvAlarmAnim = false; uiSvRingPrev = -1;
  uiApplyTheme(night);
  uiSaverScr = lv_obj_create(nullptr);
  lv_obj_t *s = uiSaverScr;
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s, P.bg, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_border_width(s, 0, 0);

  // ---------------- HERO: zegar + pogoda na "niebie" ----------------
  ui_box(s, 14, 16, 452, 174, 30, P.shade);
  lv_obj_t *hero = ui_box(s, 14, 12, 452, 174, 30, P.skyA);
  ui_grad(hero, P.skyA, P.skyB);
  lv_obj_set_style_border_width(hero, 2, 0);
  lv_obj_set_style_border_color(hero, P.brd, 0);
  if (night) {
    static const int16_t sx[] = {30, 70, 120, 168, 214, 262, 300, 352, 14, 252};
    static const int16_t sy[] = {18, 62, 24, 90, 40, 70, 22, 110, 118, 130};
    for (int i = 0; i < 10; ++i) {
      lv_obj_t *st = ui_img(hero, &sp_star4, sx[i] + (i > 5 ? 20 : 0), sy[i]);
      lv_image_set_scale(st, 120 + (i % 3) * 40);
      if (i < 4) ui_anim_start(st, ui_cb_iopa, 30, 230, 1600 + i * 300, i * 400, lv_anim_path_ease_in_out, 1600 + i * 250, true);
      else lv_obj_set_style_image_opa(st, 140, 0);
    }
  } else {
    // dwa delikatne chmurki dryfujace w tle (rzadkie odswiezanie, male pole)
    lv_obj_t *c1 = ui_img_c(hero, &sp_cloud, 120, 130, 110); lv_obj_set_style_image_opa(c1, 110, 0);
    ui_anim_start(c1, ui_cb_x, lv_obj_get_x(c1) - 24, lv_obj_get_x(c1) + 60, 18000, 0, lv_anim_path_ease_in_out, 18000, true);
    lv_obj_t *c2 = ui_img_c(hero, &sp_cloud, 250, 40, 90); lv_obj_set_style_image_opa(c2, 90, 0);
    ui_anim_start(c2, ui_cb_x, lv_obj_get_x(c2) + 30, lv_obj_get_x(c2) - 50, 22000, 0, lv_anim_path_ease_in_out, 22000, true);
  }
  // zegar: HH : MM (dwukropek mruga)
  uiSvClockH = ui_label(hero, "--", &ui_f80, P.ink, 24, -4);
  uiSvColon = ui_label(hero, ":", &ui_f80, P.acc, 24, -6);
  uiSvClockM = ui_label(hero, "--", &ui_f80, P.ink, 24, -4);
  ui_clock_place();
  ui_anim_start(uiSvColon, ui_cb_opa, 255, 60, 700, 0, lv_anim_path_ease_in_out, 700, true);
  uiSvDate = ui_label(hero, "", &ui_f18, P.ink2, 26, 100, 270);
  // pigulka drzemki
  ui_box(hero, 22, 134, 262, 28, 14, P.card, 200);
  UiIcon mi = UI_ICON(hero, moon, s, 30, 136, P.sleep); uiSvNapIcon = mi.line;
  uiSvNap = ui_label(hero, "", &ui_f13, P.muted, 60, 140, 218);
  // pogoda (prawa kolumna)
  uiSvWxBox = ui_box(hero, 322, -10, 104, 92, 0, lv_color_black(), LV_OPA_TRANSP);
  uiSvTemp = ui_label(hero, "--°", &ui_f48, P.ink, 292, 70, 152, LV_TEXT_ALIGN_CENTER);
  uiSvDesc = ui_label(hero, "", &ui_f13, P.ink2, 290, 134, 156, LV_TEXT_ALIGN_CENTER);
  lv_label_set_long_mode(uiSvDesc, LV_LABEL_LONG_MODE_DOTS);
  uiSvMinMax = ui_label(hero, "", &ui_f13, P.muted, 290, 150, 156, LV_TEXT_ALIGN_CENTER);

  // ---------------- KARMIENIE: pierscien + opis ----------------
  ui_card(s, 14, 198, 452, 80, 24);
  uiSvRing = lv_arc_create(s);
  lv_obj_set_size(uiSvRing, 64, 64);
  lv_obj_set_pos(uiSvRing, 28, 206);
  lv_arc_set_rotation(uiSvRing, 270);
  lv_arc_set_bg_angles(uiSvRing, 0, 360);
  lv_arc_set_range(uiSvRing, 0, 100);
  lv_arc_set_value(uiSvRing, 0);
  lv_obj_remove_style(uiSvRing, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(uiSvRing, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(uiSvRing, 8, LV_PART_MAIN); lv_obj_set_style_arc_color(uiSvRing, P.card2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(uiSvRing, 8, LV_PART_INDICATOR); lv_obj_set_style_arc_color(uiSvRing, P.good, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(uiSvRing, true, LV_PART_INDICATOR);
  uiSvRingTxt = ui_label(s, "", &ui_f15, P.ink, 28, 228, 64, LV_TEXT_ALIGN_CENTER);
  uiSvFeedCap = ui_label(s, "Ostatnie karmienie", &ui_f13, P.muted, 108, 207, 340);
  uiSvFeedMain = ui_label(s, "", &ui_f24, P.ink, 108, 222, 350);
  uiSvFeedNext = ui_label(s, "", &ui_f13, P.muted, 108, 254, 350);

  // ---------------- STATYSTYKI DNIA (3 kafelki) ----------------
  static const char *caps[3] = {"karmień dziś", "mleka dziś", "średnia przerwa"};
  for (int i = 0; i < 3; ++i) {
    const int x = 14 + i * 153;
    ui_box(s, x, 290, 146, 50, 18, P.card2);
    lv_obj_t *ib = ui_box(s, x + 8, 297, 22, 22, 11, i == 0 ? P.feed : (i == 1 ? P.milk : P.sleep), 70);
    (void)ib;
    if (i == 0) UI_ICON(s, feed, s, x + 7, 296, P.feed);
    else if (i == 1) UI_ICON(s, bottle, s, x + 7, 296, P.milk);
    else UI_ICON(s, clock, s, x + 7, 296, P.sleep);
    uiSvStatVal[i] = ui_label(s, "-", &ui_f24, P.ink, x + 36, 289, 106);
    ui_label(s, caps[i], &ui_f13, P.muted, x + 10, 321, 134);
  }

  // ---------------- PROGNOZA GODZINOWA + UBIOR ----------------
  ui_card(s, 14, 348, 452, 52, 20);
  for (int i = 0; i < 3; ++i) {
    const int x = 18 + i * 56;
    uiSvHourT[i] = ui_label(s, "--:--", &ui_f13, P.muted, x, 354, 56, LV_TEXT_ALIGN_CENTER);
    uiSvHourV[i] = ui_label(s, "--°", &ui_f18, P.ink, x, 369, 56, LV_TEXT_ALIGN_CENTER);
  }
  ui_box(s, 190, 358, 2, 32, 1, P.brd);
  UI_ICON(s, shirt, m, 200, 358, P.acc);
  uiSvDress = ui_label(s, "", &ui_f13, P.ink2, 238, 355, 222);
  lv_label_set_long_mode(uiSvDress, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(uiSvDress, 42);

  // ---------------- WSKAZOWKA DNIA ----------------
  ui_card(s, 14, 408, 452, 62, 20);
  UI_ICON(s, bulb, m, 24, 420, P.sun);
  uiSvTip = ui_label(s, "", &ui_f13, P.ink2, 62, 413, 330);
  lv_label_set_long_mode(uiSvTip, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(uiSvTip, 54);
  lv_obj_t *rt = ui_img(s, &sp_rattle, 358, 418);
  lv_image_set_scale(rt, 230);
  lv_image_set_pivot(rt, 56, 26);
  lv_obj_set_pos(rt, 358 + 10, 418 + 2);
  lv_image_set_rotation(rt, -150);   // grzechotka statyczna, lekko przechylona
  ui_retire_screen(oldScr, uiSaverScr);
}
static void uiSaverDestroy() {
  if (uiSaverScr) { lv_obj_t *o = uiSaverScr; uiSaverScr = nullptr; if (lv_screen_active() != o) lv_obj_delete(o); }
}

static void ui_ring_color(lv_color_t c) { lv_obj_set_style_arc_color(uiSvRing, c, LV_PART_INDICATOR); }
static void ui_alarm_pulse(bool on) {
  if (on == uiSvAlarmAnim) return;
  uiSvAlarmAnim = on;
  lv_anim_delete(uiSvRing, ui_cb_opa);
  lv_obj_set_style_opa(uiSvRing, LV_OPA_COVER, 0);
  if (on) ui_anim_start(uiSvRing, ui_cb_opa, 255, 80, 600, 0, lv_anim_path_ease_in_out, 600, true);
}

static void uiSaverUpdate(const UiModel &m) {
  if (!uiSaverScr) return;
  if (strcmp(lv_label_get_text(uiSvClockH), m.clockHH) || strcmp(lv_label_get_text(uiSvClockM), m.clockMM)) {
    lv_label_set_text(uiSvClockH, m.clockHH); lv_label_set_text(uiSvClockM, m.clockMM); ui_clock_place();
  }
  ui_set(uiSvDate, m.dateLine);
  // drzemka
  ui_set(uiSvNap, m.napLine);
  lv_color_t nc = m.napState == 2 ? P.warn : (m.napState == 3 ? P.danger : P.muted);
  lv_obj_set_style_text_color(uiSvNap, nc, 0);
  // pogoda
  if (m.weatherValid != uiSvWxValid || m.wmo != uiSvWmo || m.night != uiSvWxNight) {
    uiSvWxValid = m.weatherValid; uiSvWmo = m.wmo; uiSvWxNight = m.night;
    uiWeatherIcon(uiSvWxBox, m.wmo, m.night, m.weatherValid);
  }
  char buf[64];
  if (m.weatherValid) {
    snprintf(buf, sizeof buf, "%s%d°", m.temp > 0 ? "+" : "", m.temp); ui_set(uiSvTemp, buf);
    ui_set(uiSvDesc, m.weatherDesc);
    snprintf(buf, sizeof buf, "odcz. %d°  ·  %d° / %d°", m.feels, m.tmin, m.tmax); ui_set(uiSvMinMax, buf);
  } else {
    ui_set(uiSvTemp, "--°"); ui_set(uiSvDesc, m.weatherDesc[0] ? m.weatherDesc : "Brak danych pogody"); ui_set(uiSvMinMax, "");
  }
  for (int i = 0; i < 3; ++i) {
    if (m.hours[i].has) {
      snprintf(buf, sizeof buf, "%02u:%02u", m.hours[i].h, m.hours[i].m); ui_set(uiSvHourT[i], buf);
      snprintf(buf, sizeof buf, "%s%d°", m.hours[i].temp > 0 ? "+" : "", m.hours[i].temp); ui_set(uiSvHourV[i], buf);
    } else { ui_set(uiSvHourT[i], "--:--"); ui_set(uiSvHourV[i], "--°"); }
  }
  ui_set(uiSvDress, m.dress);
  ui_set(uiSvTip, m.tip);
  // karmienie
  if (m.hasLastFeed) {
    const int pct = m.sinceFeedMin * 100 / (m.alarmMin ? m.alarmMin : 240);
    const int v = pct > 100 ? 100 : pct;
    if (v != uiSvRingPrev) { ui_anim_start(uiSvRing, ui_cb_arcval, uiSvRingPrev < 0 ? 0 : uiSvRingPrev, v, 900, 0, lv_anim_path_ease_out); uiSvRingPrev = v; }
    const bool alarm = m.sinceFeedMin >= m.alarmMin, warn = m.sinceFeedMin >= m.warnMin;
    ui_ring_color(alarm ? P.danger : (warn ? P.warn : P.good));
    ui_alarm_pulse(alarm);
    snprintf(buf, sizeof buf, "%d:%02d", m.sinceFeedMin / 60, m.sinceFeedMin % 60); ui_set(uiSvRingTxt, buf);
    ui_set(uiSvFeedMain, m.lastFeedTxt);
    lv_obj_set_style_text_color(uiSvFeedMain, alarm ? P.danger : (warn ? P.warn : P.ink), 0);
    if (m.nextFeedClock[0]) { snprintf(buf, sizeof buf, "następne około %s", m.nextFeedClock); ui_set(uiSvFeedNext, buf); }
    else ui_set(uiSvFeedNext, "");
  } else {
    ui_set(uiSvRingTxt, "–"); ui_set(uiSvFeedMain, "brak wpisu karmienia"); ui_set(uiSvFeedNext, "");
    lv_arc_set_value(uiSvRing, 0); ui_alarm_pulse(false);
  }
  for (int i = 0; i < 3; ++i) ui_set(uiSvStatVal[i], m.statVal[i]);
}

// =============================================================================
//  PANEL (po dotknieciu wygaszacza): przelaczane karty z wykresami jak na stronie WWW
//  Przelaczanie: dolny pasek ikon albo przeciagniecie w lewo/prawo.
// =============================================================================
constexpr int UI_PAGES = 6;
static lv_obj_t *uiDashScr = nullptr;
static lv_obj_t *uiDashStrip = nullptr;       // pasek ze stronami (przesuwany w poziomie)
static lv_obj_t *uiDashPage[UI_PAGES];
static lv_obj_t *uiDashPill = nullptr;
static lv_obj_t *uiDashTabIcon[UI_PAGES][2];
static lv_obj_t *uiDashTabLbl[UI_PAGES];
static lv_obj_t *uiDashClock = nullptr, *uiDashAge = nullptr;
static lv_obj_t *uiDashLedW = nullptr, *uiDashLedS = nullptr, *uiDashLedT = nullptr;
static int uiDashCur = 0;
static UiModel uiDashM;                        // ostatnie dane (do przebudowy stron)
static bool uiDashHasData = false;
static void (*uiOnBackToSaver)() = nullptr;    // ustawiane przez firmware
static void (*uiOnDiagnostics)() = nullptr;
static lv_timer_t *uiDashTimer = nullptr;
static bool uiDashAlarmPhase = false;
static lv_obj_t *uiDashCounterCard = nullptr, *uiDashCounterTxt = nullptr, *uiDashCounterCap = nullptr;
static lv_obj_t *uiDashCounterBar = nullptr, *uiDashCounterBarBg = nullptr;

// ---- animowane slupki (pula, bo lv_anim potrzebuje trwalego wskaznika) ----------
struct UiBarAnim { lv_obj_t *o; int base; int h; int x; int w; };
static UiBarAnim (*uiBars)[48] = nullptr;   // [UI_PAGES][48] w PSRAM (alokacja przy pierwszym uzyciu)
static int uiBarN[UI_PAGES];
static void ui_cb_bar(void *v, int32_t p) {   // p: 0..256
  UiBarAnim *b = (UiBarAnim *)v;
  const int hh = b->h * p / 256;
  lv_obj_set_height(b->o, hh < 1 ? 1 : hh);
  lv_obj_set_y(b->o, b->base - (hh < 1 ? 1 : hh));
}
static lv_obj_t *ui_bar(int page, lv_obj_t *parent, int x, int base, int w, int h, int r, lv_color_t c, bool animate, int idx) {
  lv_obj_t *o = ui_box(parent, x, base - h, w, h < 1 ? 1 : h, r, c);
  if (!uiBars) uiBars = static_cast<UiBarAnim (*)[48]>(ui_alloc_big(sizeof(UiBarAnim) * 48 * UI_PAGES));
  if (uiBars && uiBarN[page] < 48) {
    UiBarAnim &b = uiBars[page][uiBarN[page]++];
    b.o = o; b.base = base; b.h = h; b.x = x; b.w = w;
    if (animate && h > 0) {
      lv_obj_set_height(o, 1); lv_obj_set_y(o, base - 1);
      ui_anim_start(&b, ui_cb_bar, 0, 256, 520, 60 + idx * 55, lv_anim_path_overshoot);
    }
  }
  return o;
}
static void ui_bars_stop_all() {   // wywolac PRZED usunieciem ekranu: anim slupkow trzyma wskaznik do puli, nie do obiektu
  for (int p = 0; p < UI_PAGES; ++p) {
    for (int i = 0; i < uiBarN[p]; ++i) lv_anim_delete(&uiBars[p][i], ui_cb_bar);
    uiBarN[p] = 0;
  }
}
// Strona "Waga": dwa widoki (wykres wagi / wykres przyrostu) przelaczane same co ~3,5 s.
static lv_obj_t *uiWtViewA = nullptr, *uiWtViewB = nullptr, *uiWtSub = nullptr, *uiWtDot[2] = {nullptr, nullptr};
static int uiWtShown = 0, uiWtTicks = 0;
static lv_point_precise_t uiWtMedPts[41];
static lv_point_precise_t uiWtActPts[UI_WPTS];
static void ui_page_reset(int page) {
  if (page == 3) { uiWtViewA = uiWtViewB = uiWtSub = uiWtDot[0] = uiWtDot[1] = nullptr; uiWtShown = 0; uiWtTicks = 0; }
  for (int i = 0; i < uiBarN[page]; ++i) lv_anim_delete(&uiBars[page][i], ui_cb_bar);
  uiBarN[page] = 0;
  lv_obj_clean(uiDashPage[page]);
}
static void ui_hline(lv_obj_t *p, int x, int y, int w, lv_color_t c, lv_opa_t o = 110) { ui_box(p, x, y, w, 1, 0, c, o); }
static void ui_dot(lv_obj_t *p, int cx, int cy, int r, lv_color_t c) { ui_box(p, cx - r, cy - r, 2 * r, 2 * r, r, c); }
static void ui_fmt_hm(char *b, size_t n, int min) {
  if (min < 0) { snprintf(b, n, "–"); return; }
  if (min >= 60) snprintf(b, n, "%dh %02dm", min / 60, min % 60); else snprintf(b, n, "%dm", min);
}
static void ui_title(lv_obj_t *p, const char *t, const char *sub = nullptr) {
  ui_label(p, t, &ui_f18, P.ink, 22, 12);
  if (sub) ui_label(p, sub, &ui_f13, P.muted, 22, 36);
}

// ----------------------------- STRONA 0: DZIS ---------------------------------
static void ui_tile(lv_obj_t *p, int x, int y, int w, int h, const lv_image_dsc_t *fi, const lv_image_dsc_t *li, lv_color_t c,
                    const char *val, const char *cap) {
  ui_box(p, x, y + 4, w, h, 22, P.shade);
  ui_box(p, x, y, w, h, 22, P.card);
  lv_obj_t *t = ui_box(p, x, y, w, h, 22, c, 34);
  lv_obj_set_style_border_width(t, 2, 0); lv_obj_set_style_border_color(t, c, 0); lv_obj_set_style_border_opa(t, 90, 0);
  ui_box(p, x + 12, y + 12, 38, 38, 19, P.card, 230);
  ui_icon2(p, fi, li, x + 15, y + 15, c);
  ui_label(p, val, &ui_f32, P.ink, x + 60, y + 4, w - 64);
  ui_label(p, cap, &ui_f13, P.ink2, x + 62, y + h - 24, w - 66);
}
static void ui_build_today(const UiModel &m, bool anim) {
  const int pg = 0; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  // licznik od ostatniego karmienia
  ui_box(p, 14, 4 + 4, 452, 94, 26, P.shade);
  uiDashCounterCard = ui_box(p, 14, 4, 452, 94, 26, P.card);
  lv_obj_set_style_border_width(uiDashCounterCard, 2, 0); lv_obj_set_style_border_color(uiDashCounterCard, P.brd, 0);
  uiDashCounterCap = ui_label(p, "OSTATNIE KARMIENIE", &ui_f13, P.muted, 30, 12);
  uiDashCounterTxt = ui_label(p, m.hasLastFeed ? m.lastFeedTxt : "brak wpisu", &ui_f32, P.ink, 30, 28, 420);
  const bool alarm = m.hasLastFeed && m.sinceFeedMin >= m.alarmMin, warn = m.hasLastFeed && m.sinceFeedMin >= m.warnMin;
  const lv_color_t sc = alarm ? P.danger : (warn ? P.warn : P.good);
  uiDashCounterBarBg = ui_box(p, 30, 72, 420, 12, 6, P.card2);
  int w = m.hasLastFeed ? (m.sinceFeedMin * 420 / (m.alarmMin ? m.alarmMin : 240)) : 0; if (w > 420) w = 420; if (w < 12 && m.hasLastFeed) w = 12;
  uiDashCounterBar = ui_box(p, 30, 72, w, 12, 6, sc);
  if (anim && w > 0) ui_anim_start(uiDashCounterBar, ui_cb_w, 1, w, 700, 150, lv_anim_path_ease_out);
  ui_box(p, 30 + 420 * m.warnMin / (m.alarmMin ? m.alarmMin : 240) - 1, 69, 2, 18, 1, P.faint);
  // ostatnie wpisy
  ui_box(p, 14, 108 + 4, 220, 64, 20, P.shade);
  lv_obj_t *c1 = ui_box(p, 14, 108, 220, 64, 20, P.card);
  lv_obj_set_style_border_width(c1, 2, 0); lv_obj_set_style_border_color(c1, P.brd, 0);
  ui_dot(p, 34, 128, 5, P.feed);
  ui_label(p, "Ostatnie karmienie", &ui_f13, P.muted, 46, 119, 180);
  ui_label(p, m.lastFeedEntry[0] ? m.lastFeedEntry : "–", &ui_f15, P.ink, 28, 142, 200);
  ui_box(p, 246, 108 + 4, 220, 64, 20, P.shade);
  lv_obj_t *c2 = ui_box(p, 246, 108, 220, 64, 20, P.card);
  lv_obj_set_style_border_width(c2, 2, 0); lv_obj_set_style_border_color(c2, P.brd, 0);
  ui_dot(p, 266, 128, 5, P.milk);
  ui_label(p, "Ostatnia butelka", &ui_f13, P.muted, 278, 119, 180);
  ui_label(p, m.lastBottleEntry[0] ? m.lastBottleEntry : "–", &ui_f15, P.ink, 260, 142, 200);
  // kafelki 2x2
  char b[32];
  snprintf(b, sizeof b, "%d", m.todayFeed);
  ui_tile(p, 14, 184, 220, 72, &ic_feed_m_fill, &ic_feed_m_line, P.feed, b, "karmień dziś");
  snprintf(b, sizeof b, "%d ml", m.todayMl);
  ui_tile(p, 246, 184, 220, 72, &ic_bottle_m_fill, &ic_bottle_m_line, P.milk, b, "mleka dziś");
  char hb[16]; ui_fmt_hm(hb, sizeof hb, m.sleepTodayDay + m.sleepTodayNight);
  ui_tile(p, 14, 268, 220, 72, &ic_moon_m_fill, &ic_moon_m_line, P.sleep, hb, "snu dziś");
  snprintf(b, sizeof b, "%d", m.todayDiapers);
  ui_tile(p, 246, 268, 220, 72, &ic_diaper_m_fill, &ic_diaper_m_line, P.bath, b, "pieluch dziś");
}

// ----------------------------- STRONA 1: MLEKO --------------------------------
static void ui_build_milk(const UiModel &m, bool anim) {
  const int pg = 1; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  ui_card(p, 14, 2, 452, 340, 26);
  ui_title(p, "Mleko — ostatnie 5 dni", "dzienne sumy w ml, pod słupkami liczba karmień");
  // legenda
  ui_dot(p, 262, 22, 5, P.acc); ui_label(p, "matki", &ui_f13, P.muted, 271, 14);
  ui_dot(p, 326, 22, 5, P.milk); ui_label(p, "modyf.", &ui_f13, P.muted, 335, 14);
  ui_dot(p, 392, 22, 5, P.bath); ui_label(p, "mieszane", &ui_f13, P.muted, 401, 14);
  int mx = 300;
  for (int i = 0; i < 5; ++i) { const int t = m.days[i].milkMother + m.days[i].milkModified + m.days[i].milkMixed; if (t > mx) mx = t; }
  mx = ((mx + 99) / 100) * 100;
  const int px0 = 52, pw = 396, base = 262, ph = 188;
  for (int g = 0; g <= mx; g += (mx > 600 ? 200 : 100)) {
    const int y = base - g * ph / mx;
    ui_hline(p, px0, y, pw, P.brd, g == 0 ? 200 : 120);
    char t[8]; snprintf(t, sizeof t, "%d", g); ui_label(p, t, &ui_f13, P.faint, 14, y - 9, 34, LV_TEXT_ALIGN_RIGHT);
  }
  const int slot = pw / 5, bw = 44;
  for (int i = 0; i < 5; ++i) {
    const UiDay &d = m.days[i];
    const int x = px0 + slot * i + (slot - bw) / 2;
    int hm = d.milkMother * ph / mx, hd = d.milkModified * ph / mx, hx = d.milkMixed * ph / mx;
    int y = base;
    const int segs[3] = {hm, hd, hx}; const lv_color_t cols[3] = {P.acc, P.milk, P.bath};
    int bi = 0;
    for (int s = 0; s < 3; ++s) {
      if (segs[s] <= 0) continue;
      const int hh = segs[s] > 3 ? segs[s] - 2 : segs[s];
      ui_bar(pg, p, x, y, bw, hh, bw / 5, cols[s], anim, i * 3 + bi++);
      y -= segs[s];
    }
    const int total = d.milkMother + d.milkModified + d.milkMixed;
    char t[16]; snprintf(t, sizeof t, "%d", total);
    ui_label(p, t, &ui_f15, i == 4 ? P.ink : P.ink2, x - 14, y - 22, bw + 28, LV_TEXT_ALIGN_CENTER);
    ui_label(p, d.label, &ui_f13, i == 4 ? P.ink : P.muted, x - 14, base + 6, bw + 28, LV_TEXT_ALIGN_CENTER);
    snprintf(t, sizeof t, "%d", d.feed);
    ui_dot(p, x + bw / 2 - 10, base + 33, 4, P.feed);
    ui_label(p, t, &ui_f13, P.feed, x + bw / 2 - 4, base + 24, 24);
  }
}

// ----------------------------- STRONA 2: ODSTEPY -----------------------------
static void ui_build_gaps(const UiModel &m, bool anim) {
  const int pg = 2; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  ui_card(p, 14, 2, 452, 190, 26);
  ui_title(p, "Odstępy między karmieniami", "dzisiaj");
  const int n = m.gapN;
  int mxg = 120;
  for (int i = 0; i < n; ++i) if (m.gapLen[i] > mxg) mxg = m.gapLen[i];
  const int rows = n > 5 ? 5 : n, first = n - rows;
  if (n == 0) ui_label(p, "Za mało karmień dzisiaj, aby policzyć odstępy.", &ui_f15, P.muted, 22, 80, 420);
  for (int r = 0; r < rows; ++r) {
    const int i = first + r, y = 62 + r * 24;
    char b[24];
    snprintf(b, sizeof b, "%02d:%02d – %02d:%02d", m.gapStart[i] / 60, m.gapStart[i] % 60, (m.gapStart[i] + m.gapLen[i]) / 60 % 24, (m.gapStart[i] + m.gapLen[i]) % 60);
    ui_label(p, b, &ui_f13, P.muted, 22, y + 1, 118);
    ui_box(p, 146, y + 3, 230, 12, 6, P.card2);
    const bool open = (i == n - 1) && m.gapOpen;
    const lv_color_t col = open ? P.warn : (m.gapLen[i] >= m.alarmMin ? P.danger : P.milk);
    const int w = m.gapLen[i] * 230 / mxg;
    lv_obj_t *bar = ui_box(p, 146, y + 3, w < 12 ? 12 : w, 12, 6, col);
    if (anim) ui_anim_start(bar, ui_cb_w, 1, w < 12 ? 12 : w, 600, 80 + r * 80, lv_anim_path_ease_out);
    ui_fmt_hm(b, sizeof b, m.gapLen[i]);
    ui_label(p, b, &ui_f15, open ? P.warn : P.ink, 384, y - 1, 70, LV_TEXT_ALIGN_RIGHT);
  }
  // rytm 3 dni
  ui_card(p, 14, 200, 452, 142, 26);
  ui_label(p, "Rytm karmień — 3 dni", &ui_f18, P.ink, 22, 210);
  for (int d = 0; d < UI_RHYTHM_DAYS; ++d) {
    const int y = 246 + d * 28;
    ui_label(p, m.rhLabel[d], &ui_f13, d == 0 ? P.ink : P.muted, 22, y - 1, 48);
    ui_box(p, 74, y, 372, 16, 8, P.card2);
    for (int h = 6; h < 24; h += 6) ui_box(p, 74 + 372 * h / 24, y + 2, 1, 12, 0, P.brd);
    for (int k = 0; k < m.rhN[d]; ++k) {
      const int cx = 74 + 7 + (372 - 14) * m.rhT[d][k] / 1440;
      ui_box(p, cx - 6, y + 2, 12, 12, 6, P.feed);
    }
  }
  for (int h = 0; h <= 24; h += 6) { char t[4]; snprintf(t, sizeof t, "%d", h); ui_label(p, t, &ui_f13, P.faint, 74 + 372 * h / 24 - 10, 322, 20, LV_TEXT_ALIGN_CENTER); }
}

// ----------------------------- STRONA 3: WAGA ---------------------------------
// Tablice WHO (chlopcy/dziewczeta usrednione jak na stronie WWW): percentyl 3, mediana, percentyl 97 [kg], co miesiac.
static const float UI_WHO_P3[22]  = {2.5f,3.4f,4.4f,5.1f,5.6f,6.1f,6.4f,6.7f,6.9f,7.1f,7.4f,7.6f,7.7f,7.9f,8.1f,8.3f,8.4f,8.6f,8.8f,8.9f,9.1f,9.2f};
static const float UI_WHO_MED[22] = {3.3f,4.5f,5.6f,6.4f,7.0f,7.5f,7.9f,8.3f,8.6f,8.9f,9.2f,9.4f,9.6f,9.9f,10.1f,10.3f,10.5f,10.7f,10.9f,11.1f,11.3f,11.5f};
static const float UI_WHO_P97[22] = {4.3f,5.7f,7.0f,7.9f,8.6f,9.2f,9.7f,10.2f,10.5f,10.9f,11.2f,11.5f,11.8f,12.1f,12.4f,12.7f,12.9f,13.2f,13.5f,13.7f,14.0f,14.3f};
static float ui_who_interp(const float *tab, int day) {
  const float mo = day / 30.4375f;
  if (mo <= 0) return tab[0];
  if (mo >= 21) return tab[21];
  const int i = (int)mo; const float f = mo - i;
  return tab[i] + (tab[i + 1] - tab[i]) * f;
}
// Pasmo oczekiwanej wagi [g] skalowane do wagi urodzeniowej (jak expectedWeightBand na WWW).
static void ui_who_band(int day, int birthG, int *mn, int *md, int *mx) {
  const float sc = birthG / (UI_WHO_MED[0] * 1000.0f);
  *mn = (int)lroundf(ui_who_interp(UI_WHO_P3, day) * 1000.0f * sc);
  *md = (int)lroundf(ui_who_interp(UI_WHO_MED, day) * 1000.0f * sc);
  *mx = (int)lroundf(ui_who_interp(UI_WHO_P97, day) * 1000.0f * sc);
}
static void ui_gain_per_day(int day, int *lo, int *hi) {   // 0/0 gdy brak normy
  *lo = *hi = 0;
  if (day < 2) return;
  if (day < 91) { *lo = 25; *hi = 30; } else if (day < 183) { *lo = 15; *hi = 20; } else { *lo = 10; *hi = 15; }
}
static void ui_weight_toggle() {
  if (!uiWtViewA || !uiWtViewB) return;
  uiWtShown ^= 1;
  if (uiWtShown) { lv_obj_add_flag(uiWtViewA, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(uiWtViewB, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(uiWtViewB, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(uiWtViewA, LV_OBJ_FLAG_HIDDEN); }
  if (uiWtSub) lv_label_set_text(uiWtSub, uiWtShown ? "przyrost g/dzień co tydzień" : "waga dziecka na tle mediany WHO");
  for (int i = 0; i < 2; ++i) if (uiWtDot[i]) {
    lv_obj_set_style_bg_color(uiWtDot[i], i == uiWtShown ? P.acc : P.brd, 0);
    lv_obj_set_width(uiWtDot[i], i == uiWtShown ? 20 : 7);
  }
  if (uiWtDot[1]) lv_obj_set_x(uiWtDot[1], 22 + (uiWtShown == 0 ? 20 : 7) + 6);
}
static void ui_weight_click_cb(lv_event_t *e) {
  (void)e;
  lv_indev_t *in = lv_indev_active();
  if (in && lv_indev_get_gesture_dir(in) != LV_DIR_NONE) return;   // przeciagniecie = zmiana strony, nie wykresu
  ui_weight_toggle();
}
static void ui_wt_tile(lv_obj_t *p, int x, int y, int w, int h, const char *key, const char *val, const char *unit,
                       const char *sub, lv_color_t c) {
  ui_box(p, x, y + 4, w, h, 18, P.shade);
  ui_box(p, x, y, w, h, 18, P.card);
  lv_obj_t *t = ui_box(p, x, y, w, h, 18, c, 30);
  lv_obj_set_style_border_width(t, 2, 0); lv_obj_set_style_border_color(t, c, 0); lv_obj_set_style_border_opa(t, 90, 0);
  ui_label(p, key, &ui_f13, P.muted, x + 14, y + 6);
  lv_obj_t *v = ui_label(p, val, &ui_f32, P.ink, x + 14, y + 22);
  lv_obj_update_layout(v);
  ui_label(p, unit, &ui_f15, P.ink2, x + 14 + lv_obj_get_width(v) + 5, y + 38);
  ui_label(p, sub, &ui_f13, P.ink2, x + 14, y + h - 25, w - 20);
}
static void ui_weight_view_weight(lv_obj_t *v, const UiModel &m) {
  // Prosty wykres: waga dziecka (gruba linia + punkty) i cienka przerywana mediana WHO jako odniesienie.
  const int px0 = 62, pw = 384, top = 82, base = 204, ph = base - top;
  int maxDay = m.wTodayDay > m.wLastDay ? m.wTodayDay : m.wLastDay; if (maxDay < 28) maxDay = 28;
  int minG = 1 << 30, maxG = 0;
  for (int i = 0; i <= 8; ++i) {
    int a, md, b; ui_who_band(maxDay * i / 8, m.wBirthG, &a, &md, &b);
    if (md > maxG) maxG = md; if (md < minG) minG = md;
  }
  for (int i = 0; i < m.wpN; ++i) { if (m.wpG[i] > maxG) maxG = m.wpG[i]; if (m.wpG[i] < minG) minG = m.wpG[i]; }
  int mxG = ((maxG + 200 + 499) / 500) * 500;
  int mnG = ((minG - 200) / 500) * 500; if (mnG < 0) mnG = 0;
  if (mxG - mnG < 1500) mxG = mnG + 1500;
  const int span = mxG - mnG;
  auto yv = [&](int g) { return base - (g - mnG) * ph / span; };
  auto xv = [&](int d) { return px0 + d * pw / maxDay; };
  for (int g = mnG; g <= mxG; g += 500) {
    const int y = yv(g);
    ui_hline(v, px0, y, pw, P.brd, g == mnG ? 170 : 70);
    char t[10]; snprintf(t, sizeof t, "%d,%d", g / 1000, (g % 1000) / 100);
    ui_label(v, t, &ui_f13, P.faint, 14, y - 9, 42, LV_TEXT_ALIGN_RIGHT);
  }
  for (int i = 0; i <= 4; ++i) {
    char t[12];
    const int wk = (maxDay * i / 4 + 3) / 7;
    if (i == 0) snprintf(t, sizeof t, "ur."); else snprintf(t, sizeof t, "%d tyg", wk);
    if (i == 4) ui_label(v, t, &ui_f13, P.faint, px0 + pw - 48, base + 6, 48, LV_TEXT_ALIGN_RIGHT);
    else ui_label(v, t, &ui_f13, P.faint, px0 + pw * i / 4 - 24, base + 6, 48, LV_TEXT_ALIGN_CENTER);
  }
  // mediana WHO: cienka przerywana linia
  for (int k = 0; k <= 40; ++k) {
    const int d = maxDay * k / 40; int a, md, b; ui_who_band(d, m.wBirthG, &a, &md, &b);
    uiWtMedPts[k].x = xv(d); uiWtMedPts[k].y = yv(md);
  }
  lv_obj_t *ml = lv_line_create(v);
  lv_line_set_points(ml, uiWtMedPts, 41);
  lv_obj_set_style_line_width(ml, 2, 0); lv_obj_set_style_line_color(ml, P.sun, 0);
  lv_obj_set_pos(ml, 0, 0);
  // waga dziecka
  const int n = m.wpN > UI_WPTS ? UI_WPTS : m.wpN;
  for (int i = 0; i < n; ++i) { uiWtActPts[i].x = xv(m.wpDay[i]); uiWtActPts[i].y = yv(m.wpG[i]); }
  lv_obj_t *al = lv_line_create(v);
  lv_line_set_points(al, uiWtActPts, n);
  lv_obj_set_style_line_width(al, 4, 0); lv_obj_set_style_line_color(al, P.acc, 0);
  lv_obj_set_style_line_rounded(al, true, 0); lv_obj_set_pos(al, 0, 0);
  for (int i = 0; i < n - 1; ++i) if (n <= 16) {   // punkty pomiarow: bialy srodek + zielony pierscien
    lv_obj_t *d = ui_box(v, uiWtActPts[i].x - 5, uiWtActPts[i].y - 5, 10, 10, 5, P.card);
    lv_obj_set_style_border_width(d, 3, 0); lv_obj_set_style_border_color(d, P.acc, 0);
  }
  { // ostatni pomiar: miekka poswiata + pelna kropka z jasnym obrysem
    const int lx = uiWtActPts[n - 1].x, ly = uiWtActPts[n - 1].y;
    ui_box(v, lx - 12, ly - 12, 24, 24, 12, P.acc, 55);
    lv_obj_t *d = ui_box(v, lx - 7, ly - 7, 14, 14, 7, P.acc);
    lv_obj_set_style_border_width(d, 3, 0); lv_obj_set_style_border_color(d, P.card, 0);
  }
  // opis ostatniego pomiaru
  char t[16]; snprintf(t, sizeof t, "%d,%02d kg", m.wpG[n - 1] / 1000, (m.wpG[n - 1] % 1000) / 10);
  int lx = uiWtActPts[n - 1].x - 70; if (lx < px0) lx = px0;
  int ly = uiWtActPts[n - 1].y - 28; if (ly < top - 8) ly = top - 8;
  ui_label(v, t, &ui_f15, P.ink, lx, ly, 74, LV_TEXT_ALIGN_RIGHT);
  // legenda
  ui_box(v, px0 + 4, base + 28, 18, 4, 2, P.acc);
  ui_label(v, "waga", &ui_f13, P.ink2, px0 + 28, base + 20);
  ui_box(v, px0 + 96, base + 28, 22, 3, 1, P.sun);
  ui_label(v, "mediana WHO", &ui_f13, P.ink2, px0 + 126, base + 20);
}
static void ui_weight_view_gain(lv_obj_t *v, const UiModel &m, bool any) {
  if (!any) { ui_label(v, "Za mało pomiarów w odstępie kilku dni,\naby policzyć trend. Zważ dziecko za kilka dni.", &ui_f15, P.muted, 30, 120, 400); return; }
  const int pg = 3, n = UI_WEEKS;
  int mx = 40, mn = 0;
  for (int i = 0; i < n; ++i) if (m.weekHas[i]) { if (m.weekVal[i] > mx) mx = m.weekVal[i]; if (m.weekVal[i] < mn) mn = m.weekVal[i]; }
  mx = ((mx + 9) / 10) * 10; if (mn < 0) mn = ((mn - 9) / 10) * 10;
  const int px0 = 56, pw = 392, top = 80, base = 206, ph = base - top;
  const int span = mx - mn;
  auto yv = [&](int val) { return base - (val - mn) * ph / span; };
  const int stepG = span > 120 ? 40 : (span > 60 ? 20 : 10);
  for (int g = (mn / stepG) * stepG; g <= mx; g += stepG) {
    if (g < mn) continue;
    const int y = yv(g);
    ui_hline(v, px0, y, pw, P.brd, g == 0 ? 220 : 110);
    char t[8]; snprintf(t, sizeof t, "%d", g); ui_label(v, t, &ui_f13, P.faint, 14, y - 9, 38, LV_TEXT_ALIGN_RIGHT);
  }
  const int slot = pw / n, bw = slot - 10 > 30 ? 30 : slot - 10;
  const int zero = yv(0);
  for (int i = 0; i < n; ++i) {
    const int cx = px0 + slot * i + slot / 2;
    char t[8];
    if (!m.weekHas[i]) { ui_label(v, "–", &ui_f15, P.faint, cx - 10, zero - 20, 20, LV_TEXT_ALIGN_CENTER); }
    else {
      const int val = m.weekVal[i], yy = yv(val);
      const int hh = val >= 0 ? zero - yy : yy - zero;
      lv_color_t col = val < 0 ? P.danger : P.acc;
      lv_obj_t *bar = ui_box(v, cx - bw / 2, val >= 0 ? zero - (hh < 3 ? 3 : hh) : zero, bw, hh < 3 ? 3 : hh, 8, col);
      if (m.weekPartial[i]) lv_obj_set_style_bg_opa(bar, 140, 0);
      snprintf(t, sizeof t, "%d", val);
      ui_label(v, t, &ui_f13, P.ink, cx - 20, val >= 0 ? yy - 19 : yy + 3, 40, LV_TEXT_ALIGN_CENTER);
    }
    snprintf(t, sizeof t, "t%d", m.weekNo[i]);
    ui_label(v, t, &ui_f13, P.muted, cx - 20, base + 6, 40, LV_TEXT_ALIGN_CENTER);
  }
  (void)pg;
}
static void ui_build_weight(const UiModel &m, bool anim) {
  (void)anim;
  const int pg = 3; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  ui_card(p, 14, 2, 452, 340, 26);
  ui_label(p, "Waga dziecka", &ui_f18, P.ink, 22, 12);
  if (m.lastWeightG > 0) {
    char b[24]; snprintf(b, sizeof b, "%d,%02d kg", m.lastWeightG / 1000, (m.lastWeightG % 1000) / 10);
    ui_label(p, b, &ui_f32, P.acc, 250, 4, 204, LV_TEXT_ALIGN_RIGHT);
    ui_label(p, m.lastWeightDate, &ui_f13, P.muted, 250, 46, 204, LV_TEXT_ALIGN_RIGHT);
  }
  bool any = false; for (int i = 0; i < UI_WEEKS; ++i) any |= m.weekHas[i];
  if (m.wpN < 2) { ui_label(p, "Dodaj co najmniej dwa pomiary wagi,\naby zobaczyć wykresy i trend przyrostu.", &ui_f15, P.muted, 30, 130, 400); return; }
  uiWtSub = ui_label(p, "waga dziecka na tle mediany WHO", &ui_f13, P.muted, 22, 36, 226);
  // dwa widoki w tym samym miejscu; przelacza je dotkniecie wykresu
  uiWtViewA = ui_box(p, 0, 0, 480, 242, 0, P.bg, LV_OPA_TRANSP);
  uiWtViewB = ui_box(p, 0, 0, 480, 242, 0, P.bg, LV_OPA_TRANSP);
  lv_obj_t *vs[2] = {uiWtViewA, uiWtViewB};
  for (int i = 0; i < 2; ++i) {
    lv_obj_add_flag(vs[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(vs[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(vs[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(vs[i], ui_weight_click_cb, LV_EVENT_CLICKED, nullptr);
  }
  ui_weight_view_weight(uiWtViewA, m);
  ui_weight_view_gain(uiWtViewB, m, any);
  lv_obj_add_flag(uiWtViewB, LV_OBJ_FLAG_HIDDEN);
  uiWtShown = 0; uiWtTicks = 0;
  uiWtDot[0] = ui_box(p, 22, 63, 20, 7, 3, P.acc);
  uiWtDot[1] = ui_box(p, 48, 63, 7, 7, 3, P.brd);
  ui_label(p, "dotknij wykres, by zmienić", &ui_f13, P.faint, 70, 56);

  // stale kafelki: przyrost od urodzenia i z ostatnich 2 tygodni
  char v1[12], s1[40], v2[12], s2[40];
  if (m.wHasSince) snprintf(v1, sizeof v1, "%d", m.wSince); else snprintf(v1, sizeof v1, "—");
  snprintf(s1, sizeof s1, "%s%d g w %d dni", m.wGainTotal >= 0 ? "+" : "", m.wGainTotal, m.wLastDay);
  int lo, hi; ui_gain_per_day(m.wLastDay, &lo, &hi);
  lv_color_t c2 = P.acc;
  if (m.wHas14) {
    snprintf(v2, sizeof v2, "%d", m.w14);
    c2 = m.w14 <= 0 ? P.danger : (!hi ? P.good : (m.w14 < lo || m.w14 > hi ? P.warn : P.good));
  } else snprintf(v2, sizeof v2, "—");
  if (hi) snprintf(s2, sizeof s2, "norma %d–%d g", lo, hi); else snprintf(s2, sizeof s2, "trend z ważeń");
  ui_wt_tile(p, 26, 246, 204, 90, "Od urodzenia", v1, m.wHasSince ? "g/dzień" : "", s1, P.acc);
  ui_wt_tile(p, 250, 246, 204, 90, "Ostatnie 2 tygodnie", v2, m.wHas14 ? "g/dzień" : "", s2, c2);
}

// ----------------------------- STRONA 4: SEN ----------------------------------
static void ui_build_sleep(const UiModel &m, bool anim) {
  const int pg = 4; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  ui_card(p, 14, 2, 452, 340, 26);
  ui_title(p, "Sen nocny", "między karmieniami, ostatnie 14 nocy");
  int cnt = 0, sum = 0, best = -1, bestI = -1, lastI = -1;
  for (int i = 0; i < UI_NIGHTS; ++i) if (m.nightMin[i] > 0) { ++cnt; sum += m.nightMin[i]; if (m.nightMin[i] > best) { best = m.nightMin[i]; bestI = i; } lastI = i; }
  if (!cnt) { ui_label(p, "Za mało danych: potrzebne karmienie wieczorem\n(17–20) i pierwsze karmienie po północy.", &ui_f15, P.muted, 30, 120, 400); return; }
  int mxh = 8; for (int i = 0; i < UI_NIGHTS; ++i) { const int h = (m.nightMin[i] + 59) / 60 + 1; if (m.nightMin[i] > 0 && h > mxh) mxh = h; }
  if (mxh % 2) ++mxh;
  const int px0 = 44, pw = 408, base = 238, ph = 150, top = base - ph;
  for (int g = 0; g <= mxh; g += 2) {
    const int y = base - g * ph / mxh;
    ui_hline(p, px0, y, pw, P.brd, g == 0 ? 220 : 110);
    char t[8]; snprintf(t, sizeof t, "%dh", g); ui_label(p, t, &ui_f13, P.faint, 10, y - 9, 30, LV_TEXT_ALIGN_RIGHT);
  }
  const int slot = pw / UI_NIGHTS, bw = slot - 6;
  for (int i = 0; i < UI_NIGHTS; ++i) {
    const int cx = px0 + slot * i + slot / 2;
    if (m.nightMin[i] > 0) {
      const int hh = m.nightMin[i] * ph / (mxh * 60);
      ui_bar(pg, p, cx - bw / 2, base, bw, hh, 6, i == bestI ? P.acc : P.sleep, anim, i);
      if (i == lastI || i == bestI) { char t[8]; snprintf(t, sizeof t, "%d,%d", m.nightMin[i] / 60, (m.nightMin[i] % 60) * 10 / 60); ui_label(p, t, &ui_f13, P.ink, cx - 20, base - hh - 19, 40, LV_TEXT_ALIGN_CENTER); }
    } else ui_label(p, "–", &ui_f13, P.faint, cx - 8, base - 20, 16, LV_TEXT_ALIGN_CENTER);
    if (i % 2 == 0 || UI_NIGHTS <= 8) ui_label(p, m.nightLabel[i], &ui_f13, P.muted, cx - 22, base + 6, 44, LV_TEXT_ALIGN_CENTER);
  }
  // srednia (linia przerywana)
  const int ay = base - (sum / cnt) * ph / (mxh * 60);
  for (int x = px0; x < px0 + pw; x += 10) ui_box(p, x, ay, 5, 2, 1, P.sleep);
  // kafelki
  char b1[16], b2[16], b3[16], c1[24], c3[24];
  ui_fmt_hm(b1, sizeof b1, sum / cnt); ui_fmt_hm(b2, sizeof b2, best); ui_fmt_hm(b3, sizeof b3, m.nightMin[lastI]);
  snprintf(c1, sizeof c1, "z %d nocy", cnt);
  snprintf(c3, sizeof c3, "%02d:%02d – %02d:%02d", m.nightFrom[bestI] / 60 % 24, m.nightFrom[bestI] % 60, m.nightTo[bestI] / 60 % 24, m.nightTo[bestI] % 60);
  const char *ks[3] = {"średnia", "najdłuższa", "ostatnia"};
  const char *vs[3] = {b1, b2, b3}; const char *ss[3] = {c1, c3, ""};
  char c4[24]; snprintf(c4, sizeof c4, "%02d:%02d – %02d:%02d", m.nightFrom[lastI] / 60 % 24, m.nightFrom[lastI] % 60, m.nightTo[lastI] / 60 % 24, m.nightTo[lastI] % 60);
  ss[2] = c4;
  for (int i = 0; i < 3; ++i) {
    const int x = 24 + i * 144;
    ui_box(p, x, 268, 134, 62, 18, P.card2);
    ui_label(p, ks[i], &ui_f13, P.muted, x + 10, 272, 120);
    ui_label(p, vs[i], &ui_f18, P.ink, x + 10, 288, 120);
    ui_label(p, ss[i], &ui_f13, P.muted, x + 10, 310, 120);
  }
}

// ----------------------------- STRONA 5: KAPIELE -----------------------------
static void ui_build_bath(const UiModel &m, bool anim) {
  const int pg = 5; ui_page_reset(pg);
  lv_obj_t *p = uiDashPage[pg];
  ui_card(p, 14, 2, 452, 340, 26);
  ui_title(p, m.bathMonth, "kąpiel co kilka dni — zaznaczone zielonym kółkiem");
  static const char *dow[7] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
  const int cw = 54, x0 = 22 + (452 - 16 - 7 * cw) / 2, y0 = 82;
  for (int i = 0; i < 7; ++i) ui_label(p, dow[i], &ui_f13, P.faint, x0 + i * cw, y0 - 20, cw, LV_TEXT_ALIGN_CENTER);
  for (int d = 1; d <= m.bathDays; ++d) {
    const int idx = m.bathFirstDow + d - 1, col = idx % 7, row = idx / 7;
    const int cx = x0 + col * cw + cw / 2, cy = y0 + row * 34 + 14;
    char t[4]; snprintf(t, sizeof t, "%d", d);
    const bool done = (m.bathMask >> (d - 1)) & 1, today = d == m.bathToday, next = d == m.bathNextDay;
    if (done) { ui_dot(p, cx, cy, 14, P.bath); ui_label(p, t, &ui_f15, lv_color_white(), cx - 14, cy - 11, 28, LV_TEXT_ALIGN_CENTER); }
    else {
      if (next) { lv_obj_t *r = ui_box(p, cx - 14, cy - 14, 28, 28, 14, P.bath, 40); lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, P.bath, 0); }
      lv_obj_t *l = ui_label(p, t, &ui_f15, today ? P.ink : P.muted, cx - 14, cy - 11, 28, LV_TEXT_ALIGN_CENTER);
      (void)l;
    }
    if (today) { lv_obj_t *r = ui_box(p, cx - 17, cy - 17, 34, 34, 17, P.ink, 0); lv_obj_set_style_border_width(r, 2, 0); lv_obj_set_style_border_color(r, P.ink, 0); }
  }
  // podsumowanie
  ui_box(p, 24, 290, 205, 42, 16, P.card2);
  ui_label(p, "Ostatnia", &ui_f13, P.muted, 36, 292, 190);
  ui_label(p, m.bathLastTxt[0] ? m.bathLastTxt : "brak", &ui_f15, P.ink, 36, 308, 190);
  ui_box(p, 237, 290, 217, 42, 16, P.card2);
  ui_label(p, m.bathNextTxt[0] ? "Następna" : "Następna", &ui_f13, P.muted, 249, 292, 190);
  ui_label(p, m.bathNextTxt[0] ? m.bathNextTxt : "—", &ui_f15, P.ink, 249, 308, 200);
}

// ----------------------------- RAMKA PANELU -----------------------------------
static void ui_dash_build(int pg, bool anim) {
  switch (pg) {
    case 0: ui_build_today(uiDashM, anim); break;
    case 1: ui_build_milk(uiDashM, anim); break;
    case 2: ui_build_gaps(uiDashM, anim); break;
    case 3: ui_build_weight(uiDashM, anim); break;
    case 4: ui_build_sleep(uiDashM, anim); break;
    default: ui_build_bath(uiDashM, anim); break;
  }
}
static void ui_tabs_paint() {
  for (int i = 0; i < UI_PAGES; ++i) {
    const bool sel = (i == uiDashCur);
    lv_color_t c = sel ? P.acc : P.muted;
    for (int k = 0; k < 2; ++k) ui_tint(uiDashTabIcon[i][k], c, k == 0 ? (sel ? 90 : 40) : LV_OPA_COVER);
    lv_obj_set_style_text_color(uiDashTabLbl[i], c, 0);
  }
}
static void ui_slide_done(lv_anim_t *a) {
  (void)a;
  for (int i = 0; i < UI_PAGES; ++i) if (i != uiDashCur) { ui_page_reset(i); }
}
static void ui_dash_goto(int to, bool animate = true) {
  if (to < 0 || to >= UI_PAGES || !uiDashStrip) return;
  const int from = uiDashCur;
  if (to == from && uiBarN[to] > 0) return;
  uiDashCur = to;
  ui_dash_build(to, animate);
  ui_tabs_paint();
  const int slotW = 452 / UI_PAGES;
  const int px = 14 + slotW * to + (slotW - 64) / 2;
  if (animate) {
    ui_anim_start(uiDashPill, ui_cb_x, lv_obj_get_x(uiDashPill), px, 280, 0, lv_anim_path_ease_out);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, uiDashStrip); lv_anim_set_exec_cb(&a, ui_cb_x);
    lv_anim_set_values(&a, lv_obj_get_x(uiDashStrip), -to * 480); lv_anim_set_duration(&a, 260);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_set_completed_cb(&a, ui_slide_done); lv_anim_start(&a);
  } else {
    lv_obj_set_x(uiDashPill, px); lv_obj_set_x(uiDashStrip, -to * 480); ui_slide_done(nullptr);
  }
}
static void ui_tab_cb(lv_event_t *e) { ui_dash_goto((int)(intptr_t)lv_event_get_user_data(e)); }
static void ui_gesture_cb(lv_event_t *e) {
  (void)e;
  lv_indev_t *ind = lv_indev_active();
  if (!ind) return;
  const lv_dir_t d = lv_indev_get_gesture_dir(ind);
  if (d == LV_DIR_LEFT) ui_dash_goto(uiDashCur + 1);
  else if (d == LV_DIR_RIGHT) ui_dash_goto(uiDashCur - 1);
}
static void ui_back_cb(lv_event_t *) { if (uiOnBackToSaver) uiOnBackToSaver(); }
static void ui_diag_cb(lv_event_t *) { if (uiOnDiagnostics) uiOnDiagnostics(); }

static void ui_dash_tick(lv_timer_t *t) {   // 500 ms: sygnalizator "czas na karmienie" (biel/czerwien jak dawniej)
  (void)t;
  if (!uiDashScr || uiDashCur != 0 || !uiDashCounterCard || !uiDashHasData) return;
  const bool alarm = uiDashM.hasLastFeed && uiDashM.sinceFeedMin >= uiDashM.alarmMin;
  if (!alarm) return;
  uiDashAlarmPhase = !uiDashAlarmPhase;
  lv_obj_set_style_bg_color(uiDashCounterCard, uiDashAlarmPhase ? P.danger : P.card, 0);
  lv_obj_set_style_text_color(uiDashCounterTxt, uiDashAlarmPhase ? lv_color_white() : P.danger, 0);
  lv_obj_set_style_text_color(uiDashCounterCap, uiDashAlarmPhase ? lv_color_white() : P.muted, 0);
}

static void uiDashCreate(bool night) {
  uiWtViewA = uiWtViewB = uiWtSub = uiWtDot[0] = uiWtDot[1] = nullptr;
  if (uiDashTimer) { lv_timer_delete(uiDashTimer); uiDashTimer = nullptr; }
  lv_obj_t *oldScr = uiDashScr; uiDashScr = nullptr;
  ui_bars_stop_all();
  uiApplyTheme(night);
  uiDashScr = lv_obj_create(nullptr);
  lv_obj_t *s = uiDashScr;
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s, P.bg, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_border_width(s, 0, 0);

  // ---- gorny pasek: logo (powrot na tablice), tytul/wiek, statusy, zegar ----
  lv_obj_t *logo = ui_box(s, 14, 8, 40, 40, 20, P.acc);
  ui_grad(logo, P.acc2, P.acc);
  lv_obj_add_flag(logo, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logo, ui_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lf = ui_img(logo, &sp_leaf, 4, 4); lv_obj_remove_flag(lf, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(logo, 10);
  ui_label(s, "Leśny Dziennik", &ui_f18, P.ink, 62, 6);
  uiDashAge = ui_label(s, "", &ui_f13, P.muted, 62, 29, 252);
  lv_label_set_long_mode(uiDashAge, LV_LABEL_LONG_MODE_DOTS);
  uiDashClock = ui_label(s, "--:--", &ui_f24, P.ink, 400, 9, 66, LV_TEXT_ALIGN_RIGHT);
  lv_obj_add_flag(uiDashClock, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(uiDashClock, ui_diag_cb, LV_EVENT_LONG_PRESSED, nullptr);
  UiIcon w = UI_ICON(s, wifi, s, 322, 14, P.good); uiDashLedW = w.line;
  UiIcon d = UI_ICON(s, disk, s, 349, 14, P.good); uiDashLedS = d.line;
  UiIcon c = UI_ICON(s, clock, s, 376, 14, P.good); uiDashLedT = c.line;
  // (ikony statusu: zielone = OK, czerwone = blad; w dolnej linii dlugie przytrzymanie zegara = diagnostyka)

  // ---- strony (pasek przewijany) ----
  lv_obj_t *view = ui_box(s, 0, 54, 480, 352, 0, P.bg, LV_OPA_TRANSP);
  lv_obj_add_flag(view, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(view, ui_gesture_cb, LV_EVENT_GESTURE, nullptr);
  uiDashStrip = ui_box(view, 0, 0, 480 * UI_PAGES, 352, 0, P.bg, LV_OPA_TRANSP);
  lv_obj_add_flag(uiDashStrip, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(uiDashStrip, LV_OBJ_FLAG_EVENT_BUBBLE);
  for (int i = 0; i < UI_PAGES; ++i) {
    uiDashPage[i] = ui_box(uiDashStrip, i * 480, 0, 480, 352, 0, P.bg, LV_OPA_TRANSP);
    lv_obj_add_flag(uiDashPage[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(uiDashPage[i], LV_OBJ_FLAG_EVENT_BUBBLE);
  }

  // ---- dolny pasek zakladek ----
  ui_card(s, 14, 412, 452, 62, 28);
  const int slotW = 452 / UI_PAGES;
  uiDashPill = ui_box(s, 14 + (slotW - 64) / 2, 416, 64, 54, 18, P.acc, 36);
  lv_obj_set_style_border_width(uiDashPill, 2, 0); lv_obj_set_style_border_color(uiDashPill, P.acc, 0); lv_obj_set_style_border_opa(uiDashPill, 120, 0);
  static const lv_image_dsc_t *tf[UI_PAGES] = {&ic_home_m_fill, &ic_bottle_m_fill, &ic_clock_m_fill, &ic_weight_m_fill, &ic_moon_m_fill, &ic_bath_m_fill};
  static const lv_image_dsc_t *tl[UI_PAGES] = {&ic_home_m_line, &ic_bottle_m_line, &ic_clock_m_line, &ic_weight_m_line, &ic_moon_m_line, &ic_bath_m_line};
  static const char *nm[UI_PAGES] = {"Dziś", "Mleko", "Odstępy", "Waga", "Sen", "Kąpiel"};
  for (int i = 0; i < UI_PAGES; ++i) {
    const int x = 14 + slotW * i;
    uiDashTabIcon[i][0] = ui_img(s, tf[i], x + (slotW - 32) / 2, 419);
    uiDashTabIcon[i][1] = ui_img(s, tl[i], x + (slotW - 32) / 2, 419);
    uiDashTabLbl[i] = ui_label(s, nm[i], &ui_f13, P.muted, x, 451, slotW, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *hit = ui_box(s, x, 414, slotW, 58, 0, P.bg, LV_OPA_TRANSP);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, ui_tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  uiDashCur = -1;
  uiDashAlarmPhase = false;
  uiDashCounterCard = nullptr;
  uiDashTimer = lv_timer_create(ui_dash_tick, 500, nullptr);
  ui_retire_screen(oldScr, uiDashScr);
}
static void uiDashDestroy() {
  uiWtViewA = uiWtViewB = uiWtSub = uiWtDot[0] = uiWtDot[1] = nullptr;
  if (uiDashTimer) { lv_timer_delete(uiDashTimer); uiDashTimer = nullptr; }
  ui_bars_stop_all();
  if (uiDashScr) { lv_obj_t *o = uiDashScr; uiDashScr = nullptr; uiDashCounterCard = nullptr; uiDashHasData = false; if (lv_screen_active() != o) lv_obj_delete(o); }
}

// Pokazuje panel na wskazanej karcie (z animacja wzrostu).
static void uiDashShow(int page = 0) {
  if (!uiDashScr) return;
  uiDashCur = -1;
  ui_dash_goto(page, false);
  ui_dash_build(uiDashCur, true);
  lv_screen_load(uiDashScr);
}
static void uiDashUpdate(const UiModel &m, bool rebuild) {
  if (!uiDashScr) return;
  uiDashM = m; uiDashHasData = true;
  ui_set(uiDashClock, "");
  char b[8]; snprintf(b, sizeof b, "%s:%s", m.clockHH, m.clockMM); ui_set(uiDashClock, b);
  char a[64]; snprintf(a, sizeof a, "Aleksander · %s", m.ageText); ui_set(uiDashAge, a);
  ui_tint(uiDashLedW, m.wifiOk ? P.good : P.danger); ui_tint(uiDashLedS, m.storageOk ? P.good : P.danger); ui_tint(uiDashLedT, m.timeOk ? P.good : P.danger);
  if (uiDashCur < 0) return;
  if (rebuild) { ui_dash_build(uiDashCur, false); return; }
  if (uiDashCur == 0 && uiDashCounterTxt) {
    ui_set(uiDashCounterTxt, m.hasLastFeed ? m.lastFeedTxt : "brak wpisu");
    const bool alarm = m.hasLastFeed && m.sinceFeedMin >= m.alarmMin, warn = m.hasLastFeed && m.sinceFeedMin >= m.warnMin;
    if (!alarm) {
      lv_obj_set_style_bg_color(uiDashCounterCard, P.card, 0);
      lv_obj_set_style_text_color(uiDashCounterTxt, P.ink, 0); lv_obj_set_style_text_color(uiDashCounterCap, P.muted, 0);
    }
    int w = m.hasLastFeed ? (m.sinceFeedMin * 420 / (m.alarmMin ? m.alarmMin : 240)) : 0; if (w > 420) w = 420; if (w < 12 && m.hasLastFeed) w = 12;
    lv_obj_set_width(uiDashCounterBar, w);
    lv_obj_set_style_bg_color(uiDashCounterBar, alarm ? P.danger : (warn ? P.warn : P.good), 0);
  }
}
