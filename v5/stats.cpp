// =============================================================================
//  stats.cpp — wszystkie obliczenia do ekranu (jedno zrodlo prawdy na urzadzeniu)
//  Odpowiednik dawnych: loadLatestEntries, refreshDayStats, uiFillLive, uiFillCharts
//  z v4.1 — z poprawkami opisanymi w ANALIZA_V5.md (sekcja 6).
// =============================================================================
#include "stats.h"
#include "app_config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#ifndef PROGMEM
#define PROGMEM
#endif
#include "development_tips.h"

// ------------------------------------------------------------------ teksty PL
static const char *const PL_DAYS[7] = {"niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"};
static const char *const PL_MONTHS_GEN[12] = {"stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca",
                                              "lipca", "sierpnia", "września", "października", "listopada", "grudnia"};
static const char *const PL_MONTHS_NOM[12] = {"Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec",
                                              "Lipiec", "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień"};
static const char *const PL_WEATHER[7] = {"Słonecznie", "Częściowe zachmurzenie", "Pochmurno", "Mgła", "Deszcz", "Śnieg", "Burza"};

// Ubior noworodka wg temperatury (progi od najcieplejszego).
struct DressBand { int minTemp; const char *text; };
static const DressBand DRESS[] = {
    {35, "Upał: samo body z krótkim rękawem, nóżki gołe, cienka czapeczka, krem z filtrem"},
    {27, "Gorąco: body z krótkim rękawem, lekkie spodenki, cienka czapeczka"},
    {22, "Ciepło: body z długim rękawem, cienki pajacyk, bawełniana czapeczka"},
    {18, "Lekko chłodno: body z długim rękawem, pajacyk, skarpetki, lekka czapka"},
    {13, "Chłodno: body, pajacyk, sweterek, czapka, kocyk do wózka"},
    {6, "Zimno: trzy warstwy, ciepłe skarpetki, kombinezon przejściowy, czapka"},
    {0, "Mróz: body, polar, kombinezon zimowy, czapka na uszy, rożek lub śpiworek"},
    {-12, "Duży mróz: maksimum warstw, śpiworek zimowy, krótko na dworze"},
    {-1000, "Poniżej -12°: odłóż spacer – wychłodzenie grozi szybciej niż korzyść ze spaceru"}};

static int weatherKind(int c) {   // 0 slonce .. 6 burza (kody WMO)
  if (c == 0) return 0;
  if (c == 1 || c == 2) return 1;
  if (c == 3) return 2;
  if (c == 45 || c == 48) return 3;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return 4;
  if (c >= 71 && c <= 77) return 5;
  if (c >= 95) return 6;
  return 2;
}

// ------------------------------------------------------------------ pomocnicze
static int interpTable(long x, const int *xs, const int *ys, int n) {
  if (x <= xs[0]) return ys[0];
  if (x >= xs[n - 1]) return ys[n - 1];
  for (int i = 1; i < n; ++i) {
    if (x <= xs[i]) {
      const long x0 = xs[i - 1], x1 = xs[i], y0 = ys[i - 1], y1 = ys[i];
      if (x1 == x0) return static_cast<int>(y0);
      return static_cast<int>(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
    }
  }
  return ys[n - 1];
}

void statsWakeWindow(long ageDays, int &minMin, int &maxMin) {
  if (ageDays < 0) ageDays = 0;
  minMin = interpTable(ageDays, WAKE_WIN_AGE_DAYS, WAKE_WIN_MIN_MINUTES, WAKE_WIN_COUNT);
  maxMin = interpTable(ageDays, WAKE_WIN_AGE_DAYS, WAKE_WIN_MAX_MINUTES, WAKE_WIN_COUNT);
  if (maxMin < minMin) maxMin = minMin;
}

static int birthDay() { return daysFromCivil(BIRTH_YEAR, BIRTH_MONTH, BIRTH_DAY); }

static int localDayOf(time_t t, struct tm *out = nullptr) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  if (out) *out = tmv;
  return daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

long statsAgeDays(time_t now, bool timeOk) {
  if (!timeOk) return -1;
  return localDayOf(now) - birthDay();
}

static void fmtHm(char *dst, size_t n, int minutes) {   // "3h 04m" / "45m" / "–"
  if (minutes <= 0) { snprintf(dst, n, "–"); return; }
  if (minutes < 60) snprintf(dst, n, "%dm", minutes);
  else snprintf(dst, n, "%dh %02dm", minutes / 60, minutes % 60);
}
static void fmtAgo(char *dst, size_t n, long minutes) {
  if (minutes < 0) minutes = 0;
  if (minutes < 60) snprintf(dst, n, "%ld min temu", minutes);
  else snprintf(dst, n, "%ld h %ld min temu", minutes / 60, minutes % 60);
}
static void fmtClock(char *dst, size_t n, time_t t) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  snprintf(dst, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}
static void dayLabel(char *dst, size_t n, int day) {   // "18.10"
  int y, m, d;
  civilFromDays(day, y, m, d);
  snprintf(dst, n, "%02d.%02d", d, m);
}

// Najpozniejszy wpis danego rodzaju z czasem <= limit; gdy brak — najpozniejszy ogolem
// (zegar cofniety / dane "z przyszlosci": "ostatnie" nigdy nie znika).
template <typename Pred>
static const Entry *latestOf(const Store *s, time_t limit, Pred pred) {
  if (!s) return nullptr;
  const Entry *any = nullptr;
  for (uint32_t i = s->n; i-- > 0;) {
    const Entry &e = s->e[i];
    if (!pred(e.type)) continue;
    if (!any) any = &e;
    if (e.t <= limit) return &e;
  }
  return any;
}

// "15:20 · karmienie" / "17.10 15:20 · 120 ml" (data, gdy wpis nie jest z dzisiaj)
static void entryLine(char *dst, size_t n, const Entry *e, int todayDay, bool bottle) {
  dst[0] = 0;
  if (!e) return;
  char when[16];
  if (e->day != todayDay) {
    char dl[8];
    dayLabel(dl, sizeof dl, e->day);
    snprintf(when, sizeof when, "%s %02d:%02d", dl, e->mod / 60, e->mod % 60);
  } else {
    snprintf(when, sizeof when, "%02d:%02d", e->mod / 60, e->mod % 60);
  }
  if (bottle) snprintf(dst, n, "%s · %u ml", when, static_cast<unsigned>(e->ml));
  else snprintf(dst, n, "%s · karmienie", when);
}

// ------------------------------------------------------------------ agregaty dni
constexpr int WIN_DAYS = 15;   // dzis + 14 dni wstecz
struct DayAgg {
  int feed, milkMl, milkMother, milkModified, milkMixed, wet, dirty, bath;
  int sleepDay, sleepNight;
};

static bool isNightHour(int h) { return h >= SLEEP_NIGHT_START_HOUR || h < SLEEP_NIGHT_END_HOUR; }

// Dzieli sen [a,b) na minuty nocne/dzienne i przypisuje do dob kalendarzowych okna.
static void accrueSleep(time_t a, time_t b, int todayDay, DayAgg *agg) {
  time_t cur = a;
  int guard = 0;
  while (cur < b && guard++ < 2000) {
    struct tm t;
    const int day = localDayOf(cur, &t);
    time_t next = cur + (3600 - (t.tm_min * 60 + t.tm_sec));
    if (next <= cur) next = cur + 3600;
    const time_t segEnd = next < b ? next : b;
    const int segMin = static_cast<int>((segEnd - cur) / 60);
    const int k = todayDay - day;
    if (segMin > 0 && k >= 0 && k < WIN_DAYS) {
      if (isNightHour(t.tm_hour)) agg[k].sleepNight += segMin;
      else agg[k].sleepDay += segMin;
    }
    cur = segEnd;
  }
}

// Sesje snu: zdarzenia posortowane po czasie (Store jest posortowany), pomijamy zdarzenia
// z przyszlosci (auto-sen panelu WWW dopisuje SEN_START na +60 min), sesje < 1 min lub
// > SLEEP_MAX_SESSION_MIN odrzucamy (zapomniany STOP), zgodnie z panelem PHP.
static void sleepScan(const Store *s, time_t now, int todayDay, DayAgg *agg, SleepState &st) {
  st = SleepState();
  if (!s) return;
  const time_t limit = now + 60;
  const time_t winStart = now - static_cast<time_t>(WIN_DAYS + 1) * 86400;
  time_t open = 0;
  for (uint32_t i = 0; i < s->n; ++i) {
    const Entry &e = s->e[i];
    if (e.type != ET_SLEEP_START && e.type != ET_SLEEP_STOP) continue;
    if (e.t > limit) break;   // posortowane: dalej tylko przyszlosc
    if (e.type == ET_SLEEP_START) { open = e.t; continue; }
    if (!open) continue;
    const long dur = static_cast<long>((e.t - open) / 60);
    if (dur >= 1 && dur <= SLEEP_MAX_SESSION_MIN && agg && e.t > winStart) accrueSleep(open, e.t, todayDay, agg);
    st.lastWake = e.t;
    open = 0;
  }
  if (open) {
    const long dur = static_cast<long>((now - open) / 60);
    if (dur >= 0 && dur <= SLEEP_MAX_SESSION_MIN) {
      st.sleeping = true;
      st.sleepStart = open;
      if (agg && now > open) accrueSleep(open, now, todayDay, agg);
    }
  }
}

static void aggDays(const Store *s, int todayDay, DayAgg *agg) {
  memset(agg, 0, sizeof(DayAgg) * WIN_DAYS);
  if (!s) return;
  for (uint32_t i = 0; i < s->n; ++i) {
    const Entry &e = s->e[i];
    const int k = todayDay - e.day;
    if (k < 0 || k >= WIN_DAYS) continue;
    DayAgg &a = agg[k];
    switch (e.type) {
      case ET_FEED: ++a.feed; break;
      case ET_MILK: a.milkMl += e.ml; break;
      case ET_MILK_MOTHER: a.milkMl += e.ml; a.milkMother += e.ml; break;
      case ET_MILK_MODIFIED: a.milkMl += e.ml; a.milkModified += e.ml; break;
      case ET_MILK_MIXED: a.milkMl += e.ml; a.milkMixed += e.ml; break;
      case ET_DIAPER_WET: ++a.wet; break;
      case ET_DIAPER_DIRTY: ++a.dirty; break;
      case ET_BATH: ++a.bath; break;
      default: break;
    }
  }
}

// Minuty od polnocy karmien danego dnia, posortowane (max cap).
static int feedMods(const Store *s, int day, int *out, int cap) {
  int n = 0;
  if (!s) return 0;
  for (uint32_t i = 0; i < s->n && n < cap; ++i) {
    if (s->e[i].type == ET_FEED && s->e[i].day == day) out[n++] = s->e[i].mod;
  }
  std::sort(out, out + n);
  return n;
}

// ------------------------------------------------------------------ czesc "na zywo"
static void computeLive(const StatsInput &in, UiModel &m, int todayDay, const struct tm &tn,
                        const DayAgg *agg, const SleepState &sl) {
  const Store *s = in.store;
  m.night = in.night;
  m.wifiOk = in.wifiOk;
  m.storageOk = s != nullptr;
  m.timeOk = in.timeOk;
  if (in.timeOk) {
    snprintf(m.clockHH, sizeof m.clockHH, "%02d", tn.tm_hour);
    snprintf(m.clockMM, sizeof m.clockMM, "%02d", tn.tm_min);
    snprintf(m.dateLine, sizeof m.dateLine, "%s, %d %s", PL_DAYS[tn.tm_wday], tn.tm_mday, PL_MONTHS_GEN[tn.tm_mon]);
  } else {
    strcpy(m.clockHH, "--");
    strcpy(m.clockMM, "--");
    m.dateLine[0] = 0;
  }

  // Wiek: "10 tyg. i 3 dni | 2 mies."
  const long ageDays = in.timeOk ? todayDay - birthDay() : -1;
  if (!in.timeOk) snprintf(m.ageText, sizeof m.ageText, "Wiek: oczekiwanie na czas");
  else if (ageDays < 0) snprintf(m.ageText, sizeof m.ageText, "Data urodzenia w przyszłości");
  else {
    int months = (tn.tm_year + 1900 - BIRTH_YEAR) * 12 + (tn.tm_mon + 1 - BIRTH_MONTH);
    if (tn.tm_mday < BIRTH_DAY) --months;
    if (months < 0) months = 0;
    snprintf(m.ageText, sizeof m.ageText, "%ld tyg. i %ld dni | %d mies.", ageDays / 7, ageDays % 7, months);
  }

  // Karmienie i butelka.
  m.warnMin = COUNTER_WARN_MIN;
  m.alarmMin = COUNTER_ALARM_MIN;
  const time_t limit = in.now + 60;
  const Entry *lf = latestOf(s, limit, [](uint8_t t) { return t == ET_FEED; });
  const Entry *lb = latestOf(s, limit, [](uint8_t t) { return etIsMilk(t); });
  m.hasLastFeed = lf != nullptr;
  if (lf) {
    long el = static_cast<long>((in.now - lf->t) / 60);
    if (el < 0) el = 0;
    m.sinceFeedMin = static_cast<int>(el);
    fmtAgo(m.lastFeedTxt, sizeof m.lastFeedTxt, el);
    fmtClock(m.nextFeedClock, sizeof m.nextFeedClock, lf->t + static_cast<time_t>(COUNTER_ALARM_MIN) * 60);
  } else {
    m.sinceFeedMin = 0;
    m.lastFeedTxt[0] = 0;
    m.nextFeedClock[0] = 0;
  }
  entryLine(m.lastFeedEntry, sizeof m.lastFeedEntry, lf, todayDay, false);
  entryLine(m.lastBottleEntry, sizeof m.lastBottleEntry, lb, todayDay, true);
  snprintf(m.lastBottleTxt, sizeof m.lastBottleTxt, "%.39s", m.lastBottleEntry);

  // Drzemka / okno czuwania.
  m.sleeping = sl.sleeping;
  m.sleepingMin = 0;
  m.napLine[0] = 0;
  m.napState = 0;
  if (sl.sleeping) {
    m.sleepingMin = static_cast<int>((in.now - sl.sleepStart) / 60);
    if (m.sleepingMin < 0) m.sleepingMin = 0;
    char hm[8], d[16];
    fmtClock(hm, sizeof hm, sl.sleepStart);
    if (m.sleepingMin >= 1) {
      fmtHm(d, sizeof d, m.sleepingMin);
      snprintf(m.napLine, sizeof m.napLine, "Śpi od %s · %s", hm, d);
    } else {
      snprintf(m.napLine, sizeof m.napLine, "Śpi od %s", hm);
    }
    m.napState = 1;
  } else if (sl.lastWake > 0 && in.timeOk) {
    if (in.now < sl.napStart) {
      char hm[8];
      fmtClock(hm, sizeof hm, sl.napStart);
      snprintf(m.napLine, sizeof m.napLine, "Drzemka ok. %s", hm);
      m.napState = 1;
    } else if (in.now <= sl.napEnd) {
      snprintf(m.napLine, sizeof m.napLine, "Czas na drzemkę");
      m.napState = 2;
    } else {
      snprintf(m.napLine, sizeof m.napLine, "Przekroczone okno czuwania");
      m.napState = 3;
    }
  }

  // Dzis: liczba karmien, ml, przerwy (z posortowanych czasow — naprawa v4).
  const DayAgg &t0 = agg[0];
  int mods[64];
  const int nf = feedMods(s, todayDay, mods, 64);
  long sum = 0;
  int cnt = 0, longest = 0;
  for (int i = 1; i < nf; ++i) {
    const int g = mods[i] - mods[i - 1];
    if (g > 0) { sum += g; ++cnt; if (g > longest) longest = g; }
  }
  if (lf && m.sinceFeedMin > longest) longest = m.sinceFeedMin;
  m.feedCount = t0.feed;
  m.longestGapMin = longest;
  m.avgGapMin = cnt ? static_cast<int>(sum / cnt) : 0;
  snprintf(m.statVal[0], sizeof m.statVal[0], "%d", t0.feed);
  snprintf(m.statVal[1], sizeof m.statVal[1], "%d ml", t0.milkMl);
  fmtHm(m.statVal[2], sizeof m.statVal[2], m.avgGapMin);
  m.todayFeed = t0.feed;
  m.todayMl = t0.milkMl;
  m.todayDiapers = t0.wet + t0.dirty;
  m.todayBath = t0.bath > 0;
  m.sleepTodayDay = t0.sleepDay;
  m.sleepTodayNight = t0.sleepNight;
  const long ad = ageDays < 0 ? 0 : ageDays;
  m.sleepNeedNight = interpTable(ad, SLEEP_NEED_AGE_DAYS, SLEEP_NEED_NIGHT_MIN, SLEEP_NEED_COUNT);
  m.sleepNeedDay = interpTable(ad, SLEEP_NEED_AGE_DAYS, SLEEP_NEED_DAY_MIN, SLEEP_NEED_COUNT);

  // Pogoda.
  const WeatherData *w = in.weather;
  m.weatherValid = w && w->valid;
  if (m.weatherValid) {
    m.wmo = w->codeNow;
    m.temp = w->tempNow;
    m.feels = w->feelsLike;
    m.tmin = w->tempMinDay;
    m.tmax = w->tempMaxDay;
    snprintf(m.weatherDesc, sizeof m.weatherDesc, "%s", PL_WEATHER[weatherKind(w->codeNow)]);
    const char *dress = DRESS[sizeof(DRESS) / sizeof(DRESS[0]) - 1].text;
    for (const DressBand &b : DRESS) if (w->tempNow >= b.minTemp) { dress = b.text; break; }
    snprintf(m.dress, sizeof m.dress, "%s", dress);
    for (int i = 0; i < 3; ++i) {
      m.hours[i].has = w->next[i].has;
      m.hours[i].h = w->next[i].hour;
      m.hours[i].m = w->next[i].minute;
      m.hours[i].temp = w->next[i].temp;
    }
  } else {
    snprintf(m.weatherDesc, sizeof m.weatherDesc, "%s", in.wifiOk ? "Czekam na dane pogody…" : "Brak połączenia Wi-Fi");
    m.dress[0] = 0;
    for (int i = 0; i < 3; ++i) m.hours[i].has = false;
  }

  // Wskazowka dnia.
  if (ageDays < 0) snprintf(m.tip, sizeof m.tip, "Rozwoj: oczekiwanie na prawidlowy czas");
  else if (ageDays >= DEVELOPMENT_TIP_COUNT)
    snprintf(m.tip, sizeof m.tip, "Rozwoj po dniu %d: odkrywajcie swiat przez zabawe, ruch, rozmowe i bezpieczna bliskosc.", DEVELOPMENT_TIP_COUNT - 1);
  else snprintf(m.tip, sizeof m.tip, "Dzien %ld: %s", ageDays, DEVELOPMENT_TIPS[ageDays]);
}

// ------------------------------------------------------------------ czesc "wykresy"
struct WPt { int day; int g; };

static void computeCharts(const StatsInput &in, UiModel &m, int todayDay, const struct tm &tn, const DayAgg *agg) {
  const Store *s = in.store;
  const int bday = birthDay();
  m.gapN = 0; m.gapOpen = false;
  for (int d = 0; d < UI_RHYTHM_DAYS; ++d) { m.rhN[d] = 0; m.rhLabel[d][0] = 0; }
  for (int i = 0; i < UI_WEEKS; ++i) { m.weekHas[i] = false; m.weekPartial[i] = false; m.weekVal[i] = 0; m.weekNo[i] = 0; }
  for (int i = 0; i < UI_NIGHTS; ++i) { m.nightMin[i] = -1; m.nightFrom[i] = 0; m.nightTo[i] = 0; m.nightLabel[i][0] = 0; }
  m.lastWeightG = 0; m.lastWeightDate[0] = 0; m.weightN = 0;
  m.wpN = 0; m.wLastDay = 0; m.wTodayDay = todayDay - bday; m.wHasSince = false; m.wHas14 = false;
  m.wSince = 0; m.wGainTotal = 0; m.w14 = 0; m.wBirthG = BIRTH_WEIGHT_G;
  m.bathMask = 0; m.bathSinceDays = -1; m.bathNextDay = 0; m.bathLastTxt[0] = 0; m.bathNextTxt[0] = 0;

  snprintf(m.rhLabel[0], sizeof m.rhLabel[0], "dziś");
  for (int d = 1; d < UI_RHYTHM_DAYS; ++d) dayLabel(m.rhLabel[d], sizeof m.rhLabel[d], todayDay - d);

  // --- 5 dob (wykres mleka) ---
  for (int i = 0; i < 5; ++i) {
    const int back = 4 - i;
    UiDay &d = m.days[i];
    if (back == 0) snprintf(d.label, sizeof d.label, "dziś");
    else dayLabel(d.label, sizeof d.label, todayDay - back);
    const DayAgg &a = agg[back];
    d.feed = a.feed;
    d.milkMother = a.milkMother;
    d.milkModified = a.milkModified;
    d.milkMixed = a.milkMixed;
    d.sleepDayMin = a.sleepDay;
    d.sleepNightMin = a.sleepNight;
    d.wet = a.wet;
    d.dirty = a.dirty;
    d.bath = a.bath > 0;
  }

  // --- Rytm 3 dni + przerwy dzisiaj (czasy posortowane) ---
  for (int d = 0; d < UI_RHYTHM_DAYS; ++d) m.rhN[d] = feedMods(s, todayDay - d, m.rhT[d], UI_RHYTHM_MAX);
  {
    const int *t = m.rhT[0];
    const int cnt = m.rhN[0];
    for (int i = 1; i < cnt && m.gapN < UI_MAX_GAPS; ++i) {
      const int len = t[i] - t[i - 1];
      if (len <= 0) continue;
      m.gapStart[m.gapN] = t[i - 1];
      m.gapLen[m.gapN] = len;
      ++m.gapN;
    }
    if (cnt > 0 && in.timeOk) {
      const int len = tn.tm_hour * 60 + tn.tm_min - t[cnt - 1];
      if (len > 0) {
        if (m.gapN == UI_MAX_GAPS) {
          memmove(m.gapStart, m.gapStart + 1, (UI_MAX_GAPS - 1) * sizeof(int));
          memmove(m.gapLen, m.gapLen + 1, (UI_MAX_GAPS - 1) * sizeof(int));
          --m.gapN;
        }
        m.gapStart[m.gapN] = t[cnt - 1];
        m.gapLen[m.gapN] = len;
        ++m.gapN;
        m.gapOpen = true;
      }
    }
  }

  // --- Waga ---
  // Bufory robocze w PSRAM (alokowane raz) — RAM wewnetrzny jest deficytowy.
  static WPt *w = nullptr;
  if (!w) w = static_cast<WPt *>(storeAlloc(97 * sizeof(WPt)));
  int wN = 0;
  const Entry *lastW = nullptr;
  int lastBathDay = -1;
  if (s) {
    for (uint32_t i = 0; i < s->n; ++i) {
      const Entry &e = s->e[i];
      if (e.type == ET_WEIGHT && e.ml > 0 && w) {
        if (wN == 96) { memmove(w, w + 1, 95 * sizeof(WPt)); --wN; }
        w[wN].day = e.day - bday;
        w[wN].g = e.ml;
        ++wN;
        if (!lastW || e.day >= lastW->day) lastW = &e;
      } else if (e.type == ET_BATH) {
        if (e.day > lastBathDay) lastBathDay = e.day;
      }
    }
  }
  if (wN > 0) {
    std::stable_sort(w, w + wN, [](const WPt &a, const WPt &b) { return a.day < b.day; });
    if (w[0].day > 0) {   // punkt urodzeniowy
      if (wN == 96) --wN;
      memmove(w + 1, w, wN * sizeof(WPt));
      w[0].day = 0;
      w[0].g = BIRTH_WEIGHT_G;
      ++wN;
    }
    const int lastDay = w[wN - 1].day;
    const int from = wN > UI_WPTS ? wN - UI_WPTS : 0;
    m.wpN = wN - from;
    for (int i = 0; i < m.wpN; ++i) { m.wpDay[i] = w[from + i].day; m.wpG[i] = w[from + i].g; }
    m.wLastDay = lastDay;
    m.wGainTotal = w[wN - 1].g - BIRTH_WEIGHT_G;
    if (lastDay > 0) { m.wHasSince = true; m.wSince = static_cast<int>(lroundf(static_cast<float>(m.wGainTotal) / lastDay)); }
    // Regresja liniowa g/dobe na punktach z okna [lo, hi].
    auto slope = [&](float lo, float hi, int &out) -> bool {
      float sx = 0, sy = 0;
      int cnt = 0, dmin = 100000, dmax = -100000;
      for (int j = 0; j < wN; ++j) {
        if (w[j].day < lo || w[j].day > hi) continue;
        sx += w[j].day; sy += w[j].g; ++cnt;
        if (w[j].day < dmin) dmin = w[j].day;
        if (w[j].day > dmax) dmax = w[j].day;
      }
      if (cnt < 2 || dmax - dmin < 5) return false;
      const float mx = sx / cnt, my = sy / cnt;
      float sxy = 0, sxx = 0;
      for (int j = 0; j < wN; ++j) {
        if (w[j].day < lo || w[j].day > hi) continue;
        sxy += (w[j].day - mx) * (w[j].g - my);
        sxx += (w[j].day - mx) * (w[j].day - mx);
      }
      if (sxx <= 0) return false;
      out = static_cast<int>(lroundf(sxy / sxx));
      return true;
    };
    m.wHas14 = slope(static_cast<float>(lastDay - 14), 1e9f, m.w14);
    const int kLast = lastDay / 7;
    int k0 = kLast - (UI_WEEKS - 1);
    if (k0 < 1) k0 = 1;
    for (int i = 0; i < UI_WEEKS; ++i) {
      const int k = k0 + i;
      m.weekNo[i] = k;
      if (k > kLast) continue;
      const float c = k * 7 + 3.5f;
      int v = 0;
      if (!slope(c - 7, c + 7, v)) continue;
      m.weekHas[i] = true;
      m.weekVal[i] = v;
      m.weekPartial[i] = (k * 7 + 7 > lastDay);
    }
    m.weightN = wN;
  }
  if (lastW) {
    int y, mo, d;
    civilFromDays(lastW->day, y, mo, d);
    m.lastWeightG = lastW->ml;
    snprintf(m.lastWeightDate, sizeof m.lastWeightDate, "%02d.%02d.%04d", d, mo, y);
  }

  // --- Noce: najdluzsza przerwa od karmienia wieczornego (17-20) do pierwszego po polnocy ---
  {
    static time_t *ft = nullptr;
    if (!ft) ft = static_cast<time_t *>(storeAlloc(400 * sizeof(time_t)));
    int fN = 0;
    if (s && ft) {
      for (uint32_t i = 0; i < s->n; ++i) {
        const Entry &e = s->e[i];
        if (!(e.type == ET_FEED || etIsMilk(e.type))) continue;
        if (todayDay - e.day > WIN_DAYS - 1 || e.day > todayDay) continue;
        if (fN > 0 && e.t - ft[fN - 1] <= 25 * 60) continue;   // karmienia w odstepie <= 25 min = jedno
        if (fN == 400) { memmove(ft, ft + 1, 399 * sizeof(time_t)); --fN; }
        ft[fN++] = e.t;
      }
    }
    int bestMin[WIN_DAYS] = {};
    time_t bestFrom[WIN_DAYS] = {}, bestTo[WIN_DAYS] = {};
    for (int i = 0; i + 1 < fN; ++i) {
      const time_t a = ft[i], b = ft[i + 1];
      struct tm ta;
      const int dayA = localDayOf(a, &ta);
      if (ta.tm_hour < 17 || ta.tm_hour >= 20) continue;
      struct tm t1 = ta;
      t1.tm_mday += 1; t1.tm_hour = 0; t1.tm_min = 0; t1.tm_sec = 0; t1.tm_isdst = -1;
      const time_t mid = mktime(&t1);
      struct tm t2 = ta;
      t2.tm_mday += 1; t2.tm_hour = 9; t2.tm_min = 0; t2.tm_sec = 0; t2.tm_isdst = -1;
      const time_t lim = mktime(&t2);
      if (b < mid || b > lim) continue;
      const time_t f = a + 90 * 60, to = b - 10 * 60;
      const int mins = static_cast<int>((to - f) / 60);
      const int k = todayDay - dayA;
      if (mins <= 0 || k < 0 || k >= WIN_DAYS) continue;
      if (mins > bestMin[k]) { bestMin[k] = mins; bestFrom[k] = f; bestTo[k] = to; }
    }
    const int firstBack = bestMin[0] > 0 ? 13 : 14;   // noc zaczeta dzis — tylko gdy juz sie skonczyla
    for (int i = 0; i < UI_NIGHTS; ++i) {
      const int k = firstBack - i;
      dayLabel(m.nightLabel[i], sizeof m.nightLabel[i], todayDay - k);
      if (bestMin[k] > 0) {
        struct tm f, t;
        localtime_r(&bestFrom[k], &f);
        localtime_r(&bestTo[k], &t);
        m.nightMin[i] = bestMin[k];
        m.nightFrom[i] = f.tm_hour * 60 + f.tm_min;
        m.nightTo[i] = t.tm_hour * 60 + t.tm_min;
      }
    }
  }

  // --- Kapiele: kalendarz biezacego miesiaca ---
  const int year = tn.tm_year + 1900, month = tn.tm_mon + 1;
  snprintf(m.bathMonth, sizeof m.bathMonth, "%s %d", PL_MONTHS_NOM[tn.tm_mon], year);
  const int firstDfc = daysFromCivil(year, month, 1);
  const int nextMonthDfc = month == 12 ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, month + 1, 1);
  m.bathFirstDow = ((firstDfc + 3) % 7 + 7) % 7;   // 1970-01-01 = czwartek; poniedzialek = 0
  m.bathDays = nextMonthDfc - firstDfc;
  m.bathToday = tn.tm_mday;
  m.bathInterval = BATH_INTERVAL_DAYS;
  if (s) {
    for (uint32_t i = 0; i < s->n; ++i) {
      const Entry &e = s->e[i];
      if (e.type != ET_BATH) continue;
      if (e.day >= firstDfc && e.day < nextMonthDfc) m.bathMask |= (1u << (e.day - firstDfc));
    }
  }
  if (lastBathDay >= 0) {
    const int since = todayDay - lastBathDay;
    m.bathSinceDays = since;
    char dl[8];
    dayLabel(dl, sizeof dl, lastBathDay);
    if (since <= 0) snprintf(m.bathLastTxt, sizeof m.bathLastTxt, "%s · dziś", dl);
    else if (since == 1) snprintf(m.bathLastTxt, sizeof m.bathLastTxt, "%s · wczoraj", dl);
    else snprintf(m.bathLastTxt, sizeof m.bathLastTxt, "%s · %d dni temu", dl, since);
    int nextDfc = lastBathDay + BATH_INTERVAL_DAYS;
    if (nextDfc < todayDay) nextDfc = todayDay;   // zalegla — proponujemy dzis
    const int delta = nextDfc - todayDay;
    if (nextDfc >= firstDfc && nextDfc < nextMonthDfc) m.bathNextDay = nextDfc - firstDfc + 1;
    dayLabel(dl, sizeof dl, nextDfc);
    if (delta == 0) snprintf(m.bathNextTxt, sizeof m.bathNextTxt, "%s · dziś", dl);
    else if (delta == 1) snprintf(m.bathNextTxt, sizeof m.bathNextTxt, "%s · jutro", dl);
    else snprintf(m.bathNextTxt, sizeof m.bathNextTxt, "%s · za %d dni", dl, delta);
  }
}

// ------------------------------------------------------------------ wejscie
void statsCompute(const StatsInput &in, UiModel &m, bool charts, SleepState *sleepOut) {
  struct tm tn;
  const int todayDay = localDayOf(in.now, &tn);
  DayAgg agg[WIN_DAYS];
  aggDays(in.store, todayDay, agg);
  SleepState sl;
  sleepScan(in.store, in.now, todayDay, agg, sl);
  if (!sl.sleeping && sl.lastWake > 0) {
    int wmin = 0, wmax = 0;
    statsWakeWindow(statsAgeDays(in.now, in.timeOk), wmin, wmax);
    sl.napStart = sl.lastWake + static_cast<time_t>(wmin) * 60;
    sl.napEnd = sl.lastWake + static_cast<time_t>(wmax) * 60;
  }
  computeLive(in, m, todayDay, tn, agg, sl);
  if (charts) computeCharts(in, m, todayDay, tn, agg);
  if (sleepOut) *sleepOut = sl;
}

// ------------------------------------------------------------------ hash wykresow
static uint32_t fnv(uint32_t h, const void *p, size_t n) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  return h;
}
#define H(x) h = fnv(h, &(m.x), sizeof(m.x))
uint32_t statsChartsHash(const UiModel &m) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 5; ++i) {
    const UiDay &d = m.days[i];
    h = fnv(h, d.label, sizeof d.label);
    const int v[9] = {d.feed, d.milkMother, d.milkModified, d.milkMixed, d.sleepDayMin, d.sleepNightMin, d.wet, d.dirty, d.bath ? 1 : 0};
    h = fnv(h, v, sizeof v);
  }
  H(gapN); H(gapStart); H(gapLen); H(gapOpen); H(rhN); H(rhT); H(rhLabel);
  H(weightN); H(weekVal); H(weekHas); H(weekPartial); H(weekNo); H(lastWeightG); H(lastWeightDate);
  H(wpN); H(wpDay); H(wpG); H(wTodayDay); H(wLastDay); H(wHasSince); H(wSince); H(wGainTotal); H(wHas14); H(w14);
  H(nightMin); H(nightFrom); H(nightTo); H(nightLabel);
  H(bathMonth); H(bathFirstDow); H(bathDays); H(bathToday); H(bathMask); H(bathSinceDays); H(bathNextDay); H(bathLastTxt); H(bathNextTxt);
  H(todayFeed); H(todayMl); H(todayDiapers); H(todayBath); H(sleepTodayDay); H(sleepTodayNight);
  H(lastFeedEntry); H(lastBottleEntry);
  return h;
}
#undef H
