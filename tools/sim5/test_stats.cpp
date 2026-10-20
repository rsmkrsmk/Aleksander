// Test na PC: v5 store+stats na pliku CSV. Wydruk w formacie scenariusza fw z v4 (porownanie parytetu).
// Budowa: g++ -std=c++17 -O1 -I../../v5 test_stats.cpp ../../v5/store.cpp ../../v5/stats.cpp -o test_stats
// Uzycie:  TZ="CET-1CEST,M3.5.0,M10.5.0/3" ./test_stats plik.csv [epoka_teraz]
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "stats.h"
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "uzycie: %s plik.csv [teraz]\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror("csv"); return 2; }
  std::vector<uint8_t> b;
  int c;
  while ((c = fgetc(f)) != EOF) b.push_back(static_cast<uint8_t>(c));
  fclose(f);
  tzset();
  Store *s = storeParse(b.data(), b.size());
  if (!s) { fprintf(stderr, "parse blad\n"); return 1; }
  WeatherData w; w.valid = true; w.tempNow = 14; w.feelsLike = 12; w.codeNow = 2; w.tempMinDay = 8; w.tempMaxDay = 16;
  StatsInput in; in.store = s; in.now = argc > 2 ? atol(argv[2]) : time(nullptr); in.timeOk = true; in.wifiOk = true; in.weather = &w;
  static UiModel M; SleepState sl;
  statsCompute(in, M, true, &sl);
  printf("entries=%u bad=%u valid=%d\n", s->n, s->badLines, csvLooksValid(b.data(), b.size()));
  printf("gaps=%d open=%d rh=%d/%d/%d\n", M.gapN, M.gapOpen, M.rhN[0], M.rhN[1], M.rhN[2]);
  for (int i = 0; i < M.gapN; i++) printf(" gap %02d:%02d +%d\n", M.gapStart[i] / 60, M.gapStart[i] % 60, M.gapLen[i]);
  for (int i = 0; i < UI_WEEKS; i++) printf(" week t%d has=%d val=%d part=%d\n", M.weekNo[i], M.weekHas[i], M.weekVal[i], M.weekPartial[i]);
  for (int i = 0; i < UI_NIGHTS; i++) printf(" night %s min=%d %02d:%02d-%02d:%02d\n", M.nightLabel[i], M.nightMin[i], M.nightFrom[i] / 60, M.nightFrom[i] % 60, M.nightTo[i] / 60, M.nightTo[i] % 60);
  printf("weight %d %s | bath %s dow=%d days=%d today=%d mask=%x last='%s' next='%s' nextDay=%d\n", M.lastWeightG, M.lastWeightDate, M.bathMonth, M.bathFirstDow, M.bathDays, M.bathToday, M.bathMask, M.bathLastTxt, M.bathNextTxt, M.bathNextDay);
  printf("feedEntry='%s' bottle='%s' age='%s' date='%s' dress='%s' stat=%s/%s/%s\n", M.lastFeedEntry, M.lastBottleEntry, M.ageText, M.dateLine, M.dress, M.statVal[0], M.statVal[1], M.statVal[2]);
  printf("live: since=%d last='%s' next=%s nap='%s'(%d) sleeping=%d today feed=%d ml=%d diap=%d sleep d/n=%d/%d need=%d/%d longest=%d\n",
         M.sinceFeedMin, M.lastFeedTxt, M.nextFeedClock, M.napLine, M.napState, M.sleeping, M.todayFeed, M.todayMl, M.todayDiapers, M.sleepTodayDay, M.sleepTodayNight, M.sleepNeedDay, M.sleepNeedNight, M.longestGapMin);
  for (int i = 0; i < 5; i++) printf(" day %s feed=%d mo=%d mod=%d mix=%d wet=%d dirty=%d bath=%d sleep=%d/%d\n", M.days[i].label, M.days[i].feed, M.days[i].milkMother, M.days[i].milkModified, M.days[i].milkMixed, M.days[i].wet, M.days[i].dirty, M.days[i].bath, M.days[i].sleepDayMin, M.days[i].sleepNightMin);
  printf("wpN=%d wLast=%d today=%d since=%d(%d) gain=%d w14=%d(%d) tip='%.40s'\n", M.wpN, M.wLastDay, M.wTodayDay, M.wSince, M.wHasSince, M.wGainTotal, M.w14, M.wHas14, M.tip);
  printf("hash=%08x\n", statsChartsHash(M));
  storeFree(s);
  return 0;
}
