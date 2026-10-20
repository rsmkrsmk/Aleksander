// =============================================================================
//  ui_model.h — dane do wyswietlenia (wspolne dla UI, statystyk i symulatora)
//  Bez zaleznosci od LVGL: liczy je stats.cpp, rysuje ui_modern.h.
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

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

// Ekran DIAGNOSTYKA: gotowy tekst (budowany przez diag.cpp).
struct UiDiag {
  char text[1800] = "";
};
