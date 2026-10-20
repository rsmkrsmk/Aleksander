// Scenariusze symulatora dla v5: prawdziwe store.cpp + stats.cpp + ui_modern.h na pliku CSV.
// Budowa (w katalogu symulatora z liblv.a, patrz tools/sim/README.md):
//   g++ -O2 -DLV_CONF_INCLUDE_SIMPLE -Iinc -Ilvgl -I<repo>/v5 -DSIM_SCENARIOS='"scen_v5.h"' -w sim.cpp liblv.a -lm -o sim_v5
// Uzycie: TZ=... ./sim_v5 <saver|saver_night|dash_N|dash_night_N|diag|weightB> out/x 1500   (CSV: /tmp/fw_data.csv)
#include <vector>
#include "store.cpp"
#include "stats.cpp"
#include "ui_modern.h"
static UiModel M;
static void scenario_build(const char *sc) {
  const bool night = strstr(sc, "night") != nullptr;
  FILE *f = fopen(getenv("CSV") ? getenv("CSV") : "/tmp/fw_data.csv", "rb");
  std::vector<uint8_t> b;
  int c;
  while (f && (c = fgetc(f)) != EOF) b.push_back((uint8_t)c);
  if (f) fclose(f);
  tzset();
  Store *s = b.empty() ? nullptr : storeParse(b.data(), b.size());
  WeatherData w; w.valid = true; w.tempNow = 11; w.feelsLike = 9; w.codeNow = 3; w.tempMinDay = 7; w.tempMaxDay = 12;
  for (int i = 0; i < 3; ++i) { w.next[i].has = true; w.next[i].hour = 18 + i; w.next[i].minute = 0; w.next[i].temp = 11 - i; }
  StatsInput in; in.store = s; in.now = getenv("NOW") ? atol(getenv("NOW")) : time(nullptr); in.timeOk = true; in.wifiOk = true; in.night = night; in.weather = &w;
  SleepState sl;
  statsCompute(in, M, true, &sl);
  fprintf(stderr, "nap='%s' sleeping=%d lastWake=%ld\n", M.napLine, M.sleeping, (long)sl.lastWake);
  uiApplyTheme(night);
  if (strstr(sc, "saver")) { uiSaverCreate(night); uiSaverUpdate(M); lv_screen_load(uiSaverScr); }
  else if (strstr(sc, "diag")) { uiDiagCreate(night, "DANE Z PANELU WWW (tylko odczyt)\n  wpisow: 701 (z WWW), 21034 B\n  rewizja: lokalna 812, zdalna 812\nOBRAZ\n  detektor przesuniecia: aktywny\n  twarde resynchronizacje: 3 (ostatni powod 8)\nURZADZENIE\n  wersja 5.0.0\n  RAM wewn.: 96 KB (min 71 KB)"); lv_screen_load(uiDiagScr); }
  else if (strstr(sc, "weightB")) { uiDashCreate(night); uiDashUpdate(M, false); uiDashShow(3); ui_weight_toggle(); uiDashUpdate(M, true); }
  else {
    int pg = 0; const char *q = strrchr(sc, '_'); if (q && isdigit(q[1])) pg = q[1] - '0';
    uiDashCreate(night); uiDashUpdate(M, false); uiDashShow(pg); uiDashUpdate(M, false);
  }
}
static void scenario_tick(const char *, uint32_t) {}
