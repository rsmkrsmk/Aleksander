/* ============================================================================
   WYŁĄCZONY KOD URZĄDZENIA (v4) — serwer WWW + mDNS + OTA.
   Ten plik jest dołączany do v3.ino WYŁĄCZNIE przez:  #include "web_server_disabled.h"
   JEDYNY przełącznik = obecność tego #include (aktualnie zakomentowany).
   Po odkomentowaniu kod jest kompilowany; o tym, czy będzie wywoływany,
   decydują flagi w v3.ino: FEATURE_DEVICE_WEB / FEATURE_MDNS / FEATURE_OTA.
   ============================================================================ */
#pragma once

#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// Globals serwera WWW urządzenia (przeniesione z v3.ino). Gdy ten plik nie jest
// dołączony, obiekt webServer nie powstaje — oszczędność RAM. webServerStarted
// zostaje w v3.ino (używa go ekran DIAGNOSTYKA).
WebServer webServer(80);
bool webRoutesConfigured = false;

// ----------------------------------------------------------------------------
// SERWER WWW URZĄDZENIA (v4: wyłączony) — handlery HTTP.
// ----------------------------------------------------------------------------
void sendJson(int statusCode, const String &payload) {
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(statusCode, "application/json; charset=utf-8", payload);
}

// Zabezpieczenie przed brakiem RAM wewnetrznego przy ciezkich zadaniach HTTP.
// Zwraca true (i wysyla 503) gdy wolnego heapu wewn. jest za malo, by bezpiecznie
// zbudowac/wystreamowac odpowiedz — lepiej odmowic obslugi niz zaryzykowac panic.
bool httpBailIfLowMemory() {
  const uint32_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (freeInt < HTTP_MIN_FREE_INTERNAL_B) {
    Serial.printf("HTTP: za malo RAM wewn. (%u B < %u B) — odpowiedz 503.\n",
                  static_cast<unsigned>(freeInt), static_cast<unsigned>(HTTP_MIN_FREE_INTERNAL_B));
    sendJson(503, "{\"message\":\"Urzadzenie chwilowo zajete (malo pamieci). Sprobuj ponownie.\"}");
    return true;
  }
  return false;
}

void handleWebRoot() {
  Serial.println("HTTP: obsluga /");
  if (httpBailIfLowMemory()) return;
  // Ograniczenie czasu pojedynczego write() przy slabym laczu: TCP retransmisje nie
  // moga zawiesic loop() (subskrybent WDT 30 s) na cale sekundy. Klient jest tutaj
  // juz polaczony, wiec timeout trafia na faktyczne gniazdo tej odpowiedzi.
  webServer.client().setTimeout(5000);
  // Wysylamy PROGMEM partiami, aby nie alokowac 32 KB Stringa.
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.setContentLength(strlen_P(WEB_APP_HTML));
  webServer.send(200, "text/html; charset=utf-8", "");
  constexpr size_t CHUNK = 1024;
  size_t pos = 0;
  const size_t total = strlen_P(WEB_APP_HTML);
  char buffer[CHUNK + 1];
  while (pos < total) {
    const size_t toRead = min(CHUNK, total - pos);
    memcpy_P(buffer, WEB_APP_HTML + pos, toRead);
    buffer[toRead] = '\0';
    webServer.sendContent(buffer);
    // Karmienie WDT w petli wysylki: przy slabym laczu pojedynczy sendContent()
    // moze czekac na TCP retransmisje. Feed co chunk strona sie nie mgrnie, ale
    // loop() pozostaje "zywy" dla watchdoga i nie nastapi bledny restart.
    feedWatchdog();
    pos += toRead;
  }
  Serial.println("HTTP: strona wyslana.");
}

void handleApiStatus() {
  if (httpBailIfLowMemory()) return;
  const time_t now = time(nullptr);
  String payload;
  // Realny rozmiar to ~1.8-2.2 KB (5 dni kalendarza + status + sysinfo). Rezerwacja
  // 2560 B pokrywa go z zapasem bez wczesniejszego blokowania przy kazdym pollingu
  // co 10 s. Jedna rezerwacja = brak serii realloc-ow fragmentujacych RAM wewn.
  // (wspoldzielony z Wi-Fi/TLS/LVGL/serwerem WWW — patrz bounce_buffer_size_px).
  payload.reserve(2560);
  payload = "{";
  // Tons: helpery jsonAppend* buduja pola bez tymczasowych String (P1).
  jsonAppendStr(payload, "now", timeIsValid ? formatDateTime(now) : String("Brak potwierdzonego czasu"));
  jsonAppendStr(payload, "nowIso", webDateTime(now));
  jsonAppendStr(payload, "ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(""));
  jsonAppendStr(payload, "age", calculateAgeText());
  jsonAppendStr(payload, "developmentTip", developmentTipForToday());
  jsonAppendInt(payload, "developmentDay", calculateAgeDays());
  jsonAppendStr(payload, "lastFeeding", lastFeeding);
  jsonAppendStr(payload, "lastMilk", lastMilk);
  jsonAppendStr(payload, "lastFeedingAgo", lastFeedingTime ? formatAgoText(lastFeedingTime) : String(""));
  jsonAppendInt(payload, "lastFeedingAgeMin", lastFeedingTime ? static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60) : -1);
  jsonAppendInt(payload, "avgFeedingGapMin", avgFeedingGapMin);
  jsonAppendStr(payload, "nextFeedingIso", nextFeedingEta ? webDateTime(nextFeedingEta) : String(""));
  jsonAppendInt(payload, "longestFeedingGapMin", longestFeedingGapMin);
  jsonAppendBool(payload, "sleepInProgress", sleepInProgress);
  // --- Sen (Napper): stan biezacy, okno czuwania, predykcja, bilans dnia ---
  {
    const long ageDays = calculateAgeDays();
    const WakeWindow ww = wakeWindowMinutes(ageDays);
    int needNight = 0, needDay = 0; sleepNeedMinutes(ageDays, needNight, needDay);
    DaySummary today; dayStats(dayOffsetFromToday(0), today);
    // Predykcja: okno czuwania liczone od ostatniego przebudzenia (SEN_STOP).
    time_t napStart = 0, napEnd = 0; String sleepState = "brak";
    if (sleepInProgress) {
      sleepState = "spi";
    } else if (lastWakeTime > 0) {
      napStart = lastWakeTime + static_cast<time_t>(ww.minMin) * 60;
      napEnd = lastWakeTime + static_cast<time_t>(ww.maxMin) * 60;
      const time_t nowT = time(nullptr);
      if (nowT < napStart) sleepState = "czuwa";       // za wczesnie na drzemke
      else if (nowT <= napEnd) sleepState = "okno";    // optymalne okno drzemki
      else sleepState = "przekroczone";                // ryzyko przemeczenia
    }
    const long sleepSinceMin = sleepInProgress && sleepStartedTime ? static_cast<long>(difftime(time(nullptr), sleepStartedTime) / 60) : -1;
    const long wakeSinceMin = (!sleepInProgress && lastWakeTime) ? static_cast<long>(difftime(time(nullptr), lastWakeTime) / 60) : -1;
    jsonAppendStr(payload, "sleepState", sleepState);
    jsonAppendInt(payload, "sleepSinceMin", sleepSinceMin);
    jsonAppendInt(payload, "wakeSinceMin", wakeSinceMin);
    jsonAppendInt(payload, "wakeWindowMinMin", ww.minMin);
    jsonAppendInt(payload, "wakeWindowMaxMin", ww.maxMin);
    jsonAppendStr(payload, "nextNapStartIso", napStart ? webDateTime(napStart) : String(""));
    jsonAppendStr(payload, "nextNapEndIso", napEnd ? webDateTime(napEnd) : String(""));
    jsonAppendInt(payload, "sleepDayMin", today.sleepDayMin);
    jsonAppendInt(payload, "sleepNightMin", today.sleepNightMin);
    jsonAppendInt(payload, "sleepNeedDayMin", needDay);
    jsonAppendInt(payload, "sleepNeedNightMin", needNight);
    jsonAppendInt(payload, "napCount", today.napCount);
    jsonAppendInt(payload, "napTarget", napTargetCount(ageDays));
    jsonAppendBool(payload, "sleepTelegram", sleepTelegramEnabled);
  }
  jsonAppendBool(payload, "wifi", WiFi.status() == WL_CONNECTED);
  jsonAppendBool(payload, "storage", storageReady);
  jsonAppendBool(payload, "dataFileHuge", dataFileHuge);
  jsonAppendBool(payload, "timeValid", timeIsValid);
  jsonAppendInt(payload, "minMl", ML_MIN);
  jsonAppendInt(payload, "maxMl", ML_MAX);
  jsonAppendInt(payload, "defaultMl", DEFAULT_ML);
  jsonAppendInt(payload, "milkMinMl", MILK_ML_MIN);
  jsonAppendInt(payload, "milkMaxMl", MILK_ML_MAX);
  jsonAppendInt(payload, "milkStepMl", MILK_ML_STEP);
  jsonAppendInt(payload, "milkDefaultMl", MILK_ML_DEFAULT);
  jsonAppendInt(payload, "birthWeightG", BIRTH_WEIGHT_G);
  jsonAppendInt(payload, "lastWeightG", lastWeightG);
  // Data ostatniej kapieli jako ISO (YYYY-MM-DD); puste, gdy brak wpisu.
  jsonAppendStr(payload, "lastBath", lastBathTime ? dateIso(lastBathTime) : String(""));
  payload += "\"calendar\":[";
  for (uint8_t i = 0; i < 5; ++i) {
    const time_t day = dayOffsetFromToday(i);
    DaySummary s;
    dayStats(day, s);
    if (i) payload += ',';
    payload += "{\"date\":\"" + dateIso(day) + "\",\"label\":\"" + jsonEscape(calendarDayTitle(day, i)) + "\",";
    jsonAppendInt(payload, "feedingCount", s.feedingCount);
    jsonAppendInt(payload, "milkMl", s.milkMl);
    jsonAppendInt(payload, "motherMilkMl", s.motherMilkMl);
    jsonAppendInt(payload, "modifiedMilkMl", s.modifiedMilkMl);
    jsonAppendInt(payload, "mixedMilkMl", s.mixedMilkMl);
    jsonAppendInt(payload, "piersLeftMin", s.piersLeftMin);
    jsonAppendInt(payload, "piersRightMin", s.piersRightMin);
    jsonAppendInt(payload, "diaperWet", s.diaperWet);
    jsonAppendInt(payload, "diaperDirty", s.diaperDirty);
    jsonAppendInt(payload, "pumpingMl", s.pumpingMl);
    jsonAppendBool(payload, "vitaminD", s.vitaminD);
    jsonAppendInt(payload, "weightG", s.weightG);
    jsonAppendInt(payload, "bathCount", s.bathCount);
    // jsonAppend* dopisuja "," po wartosci; usuwamy nadmiarowa przecinek po "bathCount".
    payload.remove(payload.length() - 1, 1);
    payload += "}";
  }
  payload += "],";
  jsonAppendBool(payload, "night", nightModeActive);
  payload += "\"mdns\":\"karmienie.local\",";
  jsonAppendInt(payload, "undoWindowSec", 60);
  jsonAppendInt(payload, "freeHeap", ESP.getFreeHeap() / 1024);
  jsonAppendInt(payload, "totalHeap", heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024);
  jsonAppendInt(payload, "freePsram", ESP.getFreePsram() / 1024);
  jsonAppendInt(payload, "totalPsram", ESP.getPsramSize() / 1024);
  jsonAppendInt(payload, "maxAlloc", ESP.getMaxAllocHeap() / 1024);
  jsonAppendInt(payload, "uptimeSec", millis() / 1000);
  jsonAppendInt(payload, "cpuLoad", cpuLoadPct);
  // Diagnostyka (te same dane co ekran DIAGNOSTYKA na urzadzeniu).
  jsonAppendInt(payload, "minFreeHeap", (minFreeHeapEver == 0xFFFFFFFFUL ? 0 : minFreeHeapEver) / 1024);
  jsonAppendInt(payload, "rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  jsonAppendInt(payload, "httpRequests", httpRequestCount);
  jsonAppendInt(payload, "bootCount", bootCount);
  jsonAppendInt(payload, "watchdogResets", watchdogResetCount);
  jsonAppendBool(payload, "watchdogReady", watchdogReady);
  jsonAppendStr(payload, "resetReason", resetReasonText(lastResetReason));
  // jsonAppendStr dopisalo "," — zastepujemy finalna "}" przy zamknieciu.
  payload.remove(payload.length() - 1, 1);
  payload += "}";
  sendJson(200, payload);
}

void handleApiEntries() {
  if (httpBailIfLowMemory()) return;
  if (!webServer.hasArg("date")) {
    sendJson(400, "{\"message\":\"Brakuje daty.\"}");
    return;
  }
  time_t day;
  if (!parseWebDateTime(webServer.arg("date") + "T12:00", day)) {
    sendJson(400, "{\"message\":\"Nieprawidłowy format daty.\"}");
    return;
  }
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamięć wewnętrzna jest niedostępna.\"}");
    return;
  }

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) {
    sendJson(500, "{\"message\":\"Nie można otworzyć historii.\"}");
    return;
  }
  const String targetDate = dateIso(day);
  // Budowa STRUMIENIOWA (jak handleApiWeightSeries): zamiast alokowac jeden duzy
  // String (dawniej reserve 4 KB) na deficytowym RAM wewnetrznym, wysylamy odpowiedz
  // partiami. Kazdy wpis to maly, krotkotrwaly String (~140 B), a nie rosnacy bufor —
  // eliminuje chwilowy skok heapu przy dniu z wieloma wpisami (wspoldzielimy heap
  // wewn. z Wi-Fi/TLS/LVGL/serwerem WWW, patrz komentarz przy bounce_buffer_size_px).
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/json; charset=utf-8", "");
  webServer.client().setTimeout(5000); // nie pozwol retransmisjom zamrozic loop()
  webServer.sendContent("{\"date\":\"" + targetDate + "\",\"entries\":[");
  bool firstEntry = true;
  int dataIndex = 0;
  uint32_t entriesFeedCounter = 0;
  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    // P3: koncowe CR/LF usuwamy w miejscu (bez trim()).
    while (line.length() && (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n')) {
      line.remove(line.length() - 1);
    }
    if (line.length() == 0) { ++dataIndex; continue; }
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) { ++dataIndex; continue; }
    if (!entry.date.startsWith(targetDate)) { ++dataIndex; continue; }
    String obj = firstEntry ? "" : ",";
    firstEntry = false;
    obj += "{\"time\":\"" + jsonEscape(entry.time) + "\",\"type\":\"" + jsonEscape(entry.type) + "\",\"label\":\"" + jsonEscape(isMilkType(entry.type) ? milkTypeLabel(entry.type) : entry.type) + "\",\"ml\":" + String(entry.ml) +
           ",\"piersLeftMin\":" + String(entry.piersLeft) + ",\"piersRightMin\":" + String(entry.piersRight) +
           ",\"lineIndex\":" + String(dataIndex) + "}";
    webServer.sendContent(obj);
    // Feed WDT: dzien z wieloma wpisami przez wolne lacze nie moze zawiesic loop().
    if ((++entriesFeedCounter & 0x3F) == 0) feedWatchdog();
    ++dataIndex;
  }
  file.close();
  webServer.sendContent("]}");
  webServer.sendContent("");
}

// Dzien zycia (0 = dzien urodzenia) dla podanej daty CSV "RRRR-MM-DD".
long dayOfLifeForDate(const String &isoDate) {
  if (isoDate.length() != 10) return -1;
  struct tm d = {};
  d.tm_year = isoDate.substring(0, 4).toInt() - 1900;
  d.tm_mon = isoDate.substring(5, 7).toInt() - 1;
  d.tm_mday = isoDate.substring(8, 10).toInt();
  d.tm_hour = 12;
  d.tm_isdst = -1;
  struct tm birth = {};
  birth.tm_year = BIRTH_YEAR - 1900;
  birth.tm_mon = BIRTH_MONTH - 1;
  birth.tm_mday = BIRTH_DAY;
  birth.tm_hour = 12;
  birth.tm_isdst = -1;
  const time_t td = mktime(&d);
  const time_t tb = mktime(&birth);
  return lround(difftime(td, tb) / 86400.0);
}

// Seria pomiarow wagi do wykresu w panelu WWW: [{day, date, g}] po dniu zycia.
// Jeden przebieg pliku, budowa strumieniowa (bez trzymania calego CSV w RAM).
void handleApiWeightSeries() {
  if (httpBailIfLowMemory()) return;
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamiec wewnetrzna jest niedostepna.\"}");
    return;
  }
  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) {
    sendJson(500, "{\"message\":\"Nie mozna otworzyc historii.\"}");
    return;
  }
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/json; charset=utf-8", "");
  webServer.client().setTimeout(5000); // nie pozwol retransmisjom zamrozic loop()
  webServer.sendContent("{\"birthWeightG\":");
  webServer.sendContent(String(BIRTH_WEIGHT_G));
  webServer.sendContent(",\"points\":[");
  bool first = true;
  uint32_t weightFeedCounter = 0;
  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    // P3: koncowe CR/LF usuwamy w miejscu (bez trim()).
    while (line.length() && (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n')) {
      line.remove(line.length() - 1);
    }
    if (line.length() == 0) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;
    if (entry.type != "WAGA") continue;
    const long dol = dayOfLifeForDate(entry.date);
    if (dol < 0) continue;
    String obj = first ? "" : ",";
    first = false;
    obj += "{\"day\":" + String(dol) + ",\"date\":\"" + entry.date + "\",\"g\":" + String(entry.ml) + "}";
    webServer.sendContent(obj);
    // Feed WDT w dlugiej liscie wag przez wolne lacze.
    if ((++weightFeedCounter & 0x3F) == 0) feedWatchdog();
  }
  file.close();
  webServer.sendContent("]}");
  webServer.sendContent("");
}

// Zwraca przez referencje ilosci mleka matki i modyfikowanego z argumentow HTTP.
// Mleko mieszane = obie > 0 (zapisywane jako dwa osobne wiersze). Zerowa ilosc = brak rodzaju.
//   NOWY format (osobne ilosci): milkMotherMl, milkModifiedMl.
//   WSTECZNA ZGODNOSC (jedna ilosc milkMl + flagi milkMother/milkModified albo milkType):
//     jeden rodzaj => cala ilosc; oba rodzaje => milkMl dzielone rowno (reszta do matki).
// Kazda ilosc > 0 ograniczana do zakresu MILK_ML_MIN..MILK_ML_MAX.
static int clampMilkMl(int v) { return v <= 0 ? 0 : constrain(v, MILK_ML_MIN, MILK_ML_MAX); }
void milkAmountsFromArgs(int &motherMl, int &modifiedMl) {
  motherMl = 0; modifiedMl = 0;
  // Preferuj nowy format z osobnymi ilosciami.
  if (webServer.hasArg("milkMotherMl") || webServer.hasArg("milkModifiedMl")) {
    motherMl = clampMilkMl(webServer.arg("milkMotherMl").toInt());
    modifiedMl = clampMilkMl(webServer.arg("milkModifiedMl").toInt());
    return;
  }
  // Wsteczna zgodnosc: jedna ilosc + wybor rodzaju.
  bool mother = webServer.hasArg("milkMother") && webServer.arg("milkMother") == "1";
  bool modified = webServer.hasArg("milkModified") && webServer.arg("milkModified") == "1";
  if (!mother && !modified && webServer.hasArg("milkType")) {
    const String mt = webServer.arg("milkType");
    mother = (mt == "MLEKO_MATKI" || mt == "MLEKO_MIESZANE");
    modified = (mt == "MLEKO_MODYFIKOWANE" || mt == "MLEKO_MIESZANE");
  }
  const int ml = webServer.arg("milkMl").toInt();
  if (ml <= 0 || (!mother && !modified)) return;
  if (mother && modified) {
    const int half = ml / 2;
    motherMl = clampMilkMl(ml - half); // reszta do matki
    modifiedMl = clampMilkMl(half);
  } else if (mother) {
    motherMl = clampMilkMl(ml);
  } else {
    modifiedMl = clampMilkMl(ml);
  }
}

void handleApiEntry() {
  struct tm currentTime;
  if (!currentLocalTime(currentTime)) {
    sendJson(409, "{\"message\":\"Nieprawidlowy czas. Sprawdz Wi-Fi i NTP.\"}");
    return;
  }
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamiec wewnetrzna jest niedostepna.\"}");
    return;
  }
  if (!webServer.hasArg("type") || !webServer.hasArg("when") || !webServer.hasArg("ml")) {
    sendJson(400, "{\"message\":\"Niepelne dane formularza.\"}");
    return;
  }

  const String type = webServer.arg("type");
  const int ml = webServer.arg("ml").toInt();
  time_t when;
  if (!parseWebDateTime(webServer.arg("when"), when)) {
    sendJson(400, "{\"message\":\"Nieprawidlowy czas wpisu.\"}");
    return;
  }
  // Zapis w przyszlosci nie ma sensu — odrzucamy z komunikatem (tolerancja +60 s).
  if (when > time(nullptr) + 60) {
    sendJson(400, "{\"message\":\"Czas wpisu nie moze byc w przyszlosci.\"}");
    return;
  }

  // Zachowuje obsluge starszych, samodzielnych wpisow mleka wysylanych przez poprzednia wersje WWW.
  if (isMilkType(type)) {
    if (ml < MILK_ML_MIN || ml > MILK_ML_MAX) {
      sendJson(400, "{\"message\":\"Nieprawidlowa ilosc mleka.\"}");
      return;
    }
    if (!appendEntry(type.c_str(), when, ml)) {
      sendJson(500, "{\"message\":\"Nie udalo sie zapisac wpisu mleka.\"}");
      return;
    }
    updateHomeInformation();
    sendJson(201, "{\"message\":\"Wpis mleka zapisany w pamieci urzadzenia.\"}");
    return;
  }
  // Waga: wartosc w gramach (osobny zakres, nie ML_MAX).
  if (type == "WAGA") {
    if (ml < WEIGHT_MIN_G || ml > WEIGHT_MAX_G) {
      sendJson(400, "{\"message\":\"Nieprawidlowa waga (gramy).\"}");
      return;
    }
    if (!appendEntry("WAGA", when, ml)) {
      sendJson(500, "{\"message\":\"Nie udalo sie zapisac wagi.\"}");
      return;
    }
    updateHomeInformation();
    sendJson(201, "{\"message\":\"Zapisano wage.\"}");
    return;
  }
  // Pompowanie i zdarzenia jednym dotknieciem przez panel WWW.
  if (!isMilkType(type) && type != "KARMIENIE") {
    const bool validType = type == "ODCIAGANIE" || type == "PIELUCHA_MOKRA" || type == "PIELUCHA_BRUDNA" || type == "WITAMINA_D";
    if (!validType) {
      sendJson(400, "{\"message\":\"Nieznany typ zdarzenia.\"}");
      return;
    }
    if (type == "WITAMINA_D") {
      DaySummary s;
      dayStats(dayOffsetFromToday(0), s);
      if (s.vitaminD) {
        sendJson(200, "{\"message\":\"Witamina D juz zapisana dzisiaj.\"}");
        return;
      }
    }
    if (!appendEntry(type.c_str(), when, ml)) {
      sendJson(500, "{\"message\":\"Nie udalo sie zapisac zdarzenia.\"}");
      return;
    }
    updateHomeInformation();
    sendJson(201, "{\"message\":\"Zapisano zdarzenie.\"}");
    return;
  }
  if (type != "KARMIENIE" || ml != 0) {
    sendJson(400, "{\"message\":\"Karmienie nie wymaga ilosci ml; podaj ja tylko dla Butelki.\"}");
    return;
  }

  const bool extraMilk = webServer.hasArg("extraMilk") && webServer.arg("extraMilk") == "1";
  // Mleko rozbite na DWIE osobne ilosci (mieszane = obie > 0 => dwa wiersze MLEKO_*).
  int motherMl = 0, modifiedMl = 0;
  if (extraMilk) {
    milkAmountsFromArgs(motherMl, modifiedMl);
    if (motherMl == 0 && modifiedMl == 0) {
      sendJson(400, "{\"message\":\"Zaznacz rodzaj mleka (matki i/lub modyfikowane) i podaj ilosc.\"}");
      return;
    }
  }

  // Minuty karmienia piersią są opcjonalne (0, gdy panel ich nie wysłał).
  const int piersLeftMin = constrain(webServer.arg("lewaMin").toInt(), 0, 120);
  const int piersRightMin = constrain(webServer.arg("prawaMin").toInt(), 0, 120);

  if (!appendEntry("KARMIENIE", when, ml, piersLeftMin, piersRightMin)) {
    sendJson(500, "{\"message\":\"Nie udalo sie zapisac karmienia.\"}");
    return;
  }
  // Mleko mieszane = dwa osobne wiersze o tej samej godzinie co karmienie.
  if (motherMl > 0 && !appendEntry("MLEKO_MATKI", when, motherMl)) {
    sendJson(500, "{\"message\":\"Karmienie zapisano, ale nie udalo sie zapisac mleka matki.\"}");
    return;
  }
  if (modifiedMl > 0 && !appendEntry("MLEKO_MODYFIKOWANE", when, modifiedMl)) {
    sendJson(500, "{\"message\":\"Karmienie zapisano, ale nie udalo sie zapisac mleka modyfikowanego.\"}");
    return;
  }

  // Auto-sen przy karmieniu (tylko przez WWW): SEN_STOP T-30 + SEN_START T+60,
  // zamknij biezacy sen jesli dziecko spalo. Ekran dotykowy LVGL pomija te logike.
  applyAutoSleepForFeeding(when);

  updateHomeInformation();
  sendJson(201, extraMilk ? "{\"message\":\"Zapisano karmienie i dodatkowe mleko.\"}" : "{\"message\":\"Karmienie zapisane w pamieci urzadzenia.\"}");
}

void handleApiDeleteEntry() {
  if (!webServer.hasArg("line")) {
    sendJson(400, "{\"message\":\"Brakuje indeksu linii do usuniecia.\"}");
    return;
  }
  const int lineIndex = webServer.arg("line").toInt();
  String removed;
  if (!deleteEntryByIndex(lineIndex, removed)) {
    sendJson(400, "{\"message\":\"Nie udalo sie usunac wpisu.\"}");
    return;
  }
  updateHomeInformation();
  requestHostSync(); // dane sie zmienily — zsynchronizuj CSV z panelem WWW
  sendJson(200, "{\"message\":\"Usunieto wpis.\",\"removed\":\"" + jsonEscape(removed) + "\"}");
}

// Edycja karmienia W MIEJSCU (mleko + godzina), ten sam kontrakt co panel WWW
// (POST /api/update-feeding): feedLine (indeks wiersza KARMIENIE), when (nowy czas),
// oraz jedno z: milkRemove=1 (usun mleko) albo osobne ilosci milkMotherMl/milkModifiedMl
// (mleko mieszane = obie > 0 => dwa wiersze). Wsteczna zgodnosc: milkMother/milkModified+milkMl.
// Minuty piersi zachowane bez zmian.
void handleApiUpdateFeeding() {
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamiec wewnetrzna jest niedostepna.\"}");
    return;
  }
  if (!webServer.hasArg("feedLine") || !webServer.hasArg("when")) {
    sendJson(400, "{\"message\":\"Niepelne dane edycji.\"}");
    return;
  }
  const int feedLine = webServer.arg("feedLine").toInt();
  time_t when;
  if (!parseWebDateTime(webServer.arg("when"), when)) {
    sendJson(400, "{\"message\":\"Nieprawidlowy czas wpisu.\"}");
    return;
  }
  // Zapis w przyszlosci nie ma sensu — odrzucamy (tolerancja +60 s).
  if (when > time(nullptr) + 60) {
    sendJson(400, "{\"message\":\"Czas wpisu nie moze byc w przyszlosci.\"}");
    return;
  }

  const bool milkRemove = webServer.hasArg("milkRemove") && webServer.arg("milkRemove") == "1";
  // Mleko rozbite na DWIE osobne ilosci (mieszane = obie > 0 => dwa wiersze).
  int motherMl = 0, modifiedMl = 0;
  if (!milkRemove) {
    milkAmountsFromArgs(motherMl, modifiedMl);
    if (motherMl == 0 && modifiedMl == 0) {
      sendJson(400, "{\"message\":\"Zaznacz rodzaj mleka i podaj ilosc albo usun mleko.\"}");
      return;
    }
  }

  String err;
  if (!updateFeeding(feedLine, when, motherMl, modifiedMl, err)) {
    sendJson(400, "{\"message\":\"" + jsonEscape(err.length() ? err : String("Nie udalo sie zapisac edycji.")) + "\"}");
    return;
  }
  updateHomeInformation();
  requestHostSync(); // dane sie zmienily — zsynchronizuj CSV z panelem WWW
  sendJson(200, "{\"message\":\"Zapisano edycje karmienia.\"}");
}

// Reczna wysylka backupu przez panel WWW. Wysylka wykonuje sie w pumpTelegramQueue()
// — tutaj tylko ustawiamy kolejke.
void handleApiSendBackup() {
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) {
    sendJson(400, "{\"message\":\"Telegram nie jest skonfigurowany (config.h).\"}");
    return;
  }
  if (!storageReady || !LittleFS.exists(BACKUP_FILE_PATH)) {
    sendJson(400, "{\"message\":\"Brak pliku backupu.\"}");
    return;
  }
  // Check-and-set stanu backupu pod mutexem (czyta go telegramTask na rdzeniu 0),
  // spojnie z appendBackupIfDue. Bez tego byl wyscig na backupState/backupFileName.
  bool alreadyBusy = false;
  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (backupState != B_IDLE) {
    alreadyBusy = true;
  } else {
    backupFileName = buildBackupFileName();
    backupState = B_WANTED;
    if (telegramNextAttemptMs == 0) telegramNextAttemptMs = millis();
  }
  if (telegramMutex) xSemaphoreGive(telegramMutex);

  if (alreadyBusy) {
    sendJson(200, "{\"message\":\"Wysylka backupu juz trwa.\"}");
    return;
  }
  wakeTelegramTask(); // nie czekaj do 5 s na timeout taska
  sendJson(200, "{\"message\":\"Zaplanowano wysylke backupu na Telegram.\"}");
}

// Uniwersalny zapis zdarzen bez parametrow: pieluchy, witamina D, odciganie.
void handleApiEvent() {
  struct tm currentTime;
  if (!currentLocalTime(currentTime)) {
    sendJson(409, "{\"message\":\"Nieprawidlowy czas. Sprawdz Wi-Fi i NTP.\"}");
    return;
  }
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamiec wewnetrzna jest niedostepna.\"}");
    return;
  }
  const String type = webServer.arg("type");
  const bool validType = type == "PIELUCHA_MOKRA" || type == "PIELUCHA_BRUDNA" ||
                         type == "WITAMINA_D" || type == "ODCIAGANIE" || type == "WAGA" ||
                         type == "SEN_START" || type == "SEN_STOP" || type == "KAPIEL";
  if (!validType) {
    sendJson(400, "{\"message\":\"Nieznany typ zdarzenia.\"}");
    return;
  }

  // Domyslnie biezacy czas; gdy klient poda "when", MUSI byc poprawny (inaczej 400,
  // zeby nie zapisac cicho zdarzenia z bledna/zastapiona data).
  time_t when = time(nullptr);
  if (webServer.hasArg("when") && !parseWebDateTime(webServer.arg("when"), when)) {
    sendJson(400, "{\"message\":\"Nieprawidlowy czas zdarzenia.\"}");
    return;
  }
  // Zapis w przyszlosci nie ma sensu — odrzucamy (tolerancja +60 s).
  if (webServer.hasArg("when") && when > time(nullptr) + 60) {
    sendJson(400, "{\"message\":\"Czas wpisu nie moze byc w przyszlosci.\"}");
    return;
  }

  // Waga: wartosc w gramach (osobny zakres, poza ML_MAX).
  if (type == "WAGA") {
    const int grams = webServer.arg("ml").toInt();
    if (grams < WEIGHT_MIN_G || grams > WEIGHT_MAX_G) {
      sendJson(400, "{\"message\":\"Nieprawidlowa waga (gramy).\"}");
      return;
    }
    if (!appendEntry("WAGA", when, grams)) {
      sendJson(500, "{\"message\":\"Nie udalo sie zapisac wagi.\"}");
      return;
    }
    updateHomeInformation();
    sendJson(201, "{\"message\":\"Zapisano wage.\"}");
    return;
  }

  int ml = constrain(webServer.arg("ml").toInt(), 0, ML_MAX);
  if (type == "ODCIAGANIE" && ml < ML_MIN) {
    sendJson(400, "{\"message\":\"Podaj ilosc odciagnietego mleka.\"}");
    return;
  }

  if (type == "WITAMINA_D") {
    DaySummary s;
    dayStats(dayOffsetFromToday(0), s);
    if (s.vitaminD) {
      sendJson(200, "{\"message\":\"Witamina D juz zapisana dzisiaj.\"}");
      return;
    }
    ml = 0;
  }

  if (!appendEntry(type.c_str(), when, ml)) {
    sendJson(500, "{\"message\":\"Nie udalo sie zapisac zdarzenia.\"}");
    return;
  }
  updateHomeInformation();
  sendJson(201, "{\"message\":\"Zapisano zdarzenie.\"}");
}

// Pobranie pelnej historii CSV przez przegladarke.
void handleExportCsv() {
  if (!storageReady) {
    webServer.send(503, "text/plain; charset=utf-8", "Pamiec niedostepna.");
    return;
  }
  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) {
    webServer.send(500, "text/plain; charset=utf-8", "Nie mozna otworzyc historii.");
    return;
  }
  webServer.sendHeader("Content-Disposition", "attachment; filename=karmienia.csv");
  webServer.setContentLength(file.size());
  webServer.send(200, "text/csv; charset=utf-8", "");
  webServer.client().setTimeout(5000); // nie pozwol retransmisjom zamrozic loop()
  uint8_t buffer[512];
  uint32_t csvFeedCounter = 0;
  while (file.available()) {
    const size_t readBytes = file.read(buffer, sizeof(buffer));
    if (readBytes == 0) break;
    webServer.client().write(buffer, readBytes);
    // Feed WDT co ~8 KB wyslanych: eksport calej historii przez wolne lacze
    // nie moze zawiesic loop() na tyle, by watchdog zrestartowal urzadzenie.
    if ((++csvFeedCounter & 0xF) == 0) feedWatchdog();
  }
  file.close();
}

// Kopiowanie plikow w obrębie LittleFS (uzywane przez backup i import).
// (definicja przeniesiona przed blok #if FEATURE_DEVICE_WEB — uzywa jej
//  archiveDataFileIfHuge/appendBackupIfDue, ktore sa poza serwerem WWW)

// Przywracanie historii: przyjmuje tresc CSV w ciele zapytania, sanityzuje,
// przed nadpisaniem robi kopie obecnych danych, zapis atomowy przez rename.
void handleApiImport() {
  if (!storageReady) {
    sendJson(503, "{\"message\":\"Pamiec wewnetrzna jest niedostepna.\"}");
    return;
  }

  String body = webServer.arg("plain");
  constexpr size_t IMPORT_MAX_BYTES = 512UL * 1024UL;
  if (body.length() == 0 || body.length() > IMPORT_MAX_BYTES) {
    sendJson(400, "{\"message\":\"Plik jest pusty lub przekracza limit 512 KB.\"}");
    return;
  }
  const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (static_cast<size_t>(body.length()) + 8192UL > freeBytes) {
    sendJson(507, "{\"message\":\"Za malo miejsca w pamieci urzadzenia.\"}");
    return;
  }
  // Ochrona OOM: plik nie moze byc wiekszy niz ~60% wolnego heapa.
  if (static_cast<size_t>(body.length()) > (ESP.getFreeHeap() * 3 / 5)) {
    sendJson(507, "{\"message\":\"Za malo ramu na przetworzenie tak duzego pliku.\"}");
    return;
  }

  // Backup aktualnych danych przed nadpisaniem.
  if (!copyLittleFsFile(DATA_FILE_PATH, BACKUP_FILE_PATH)) {
    sendJson(500, "{\"message\":\"Nie udalo sie zapisac kopii bezpieczenstwa — import przerwany.\"}");
    return;
  }

  // Sanityzacja strumieniowo wprost do pliku .tmp — bez drugiego pelnego bufora w RAM.
  File dst = LittleFS.open("/karmienia_import.tmp", FILE_WRITE);
  if (!dst) {
    sendJson(500, "{\"message\":\"Blad zapisu pliku tymczasowego.\"}");
    return;
  }

  // Kanoniczny naglowek.
  constexpr char HEADER[] = "data,godzina,typ,ml,piers_lewa_min,piers_prawa_min\n";
  dst.write(reinterpret_cast<const uint8_t *>(HEADER), sizeof(HEADER) - 1);

  int rowCount = 0;
  int skippedRows = 0;
  int start = 0;
  bool headerSeen = false;
  while (start <= static_cast<int>(body.length())) {
    const int nl = body.indexOf('\n', start);
    String line = (nl < 0) ? body.substring(start) : body.substring(start, nl);
    line.trim();
    if (!headerSeen) {
      if (line.startsWith("data,")) headerSeen = true;
    } else if (line.length() > 0 && line.length() <= 160) {
      CsvEntry probe;
      if (parseCsvLine(line, probe)) {
        dst.println(line);
        ++rowCount;
      } else {
        ++skippedRows;
      }
    }
    if (nl < 0) break;
    start = nl + 1;
  }
  dst.close();
  // Zwolnij roboczy bufor PRZED dalszymi operacjami (copy/rename tez alokuja).
  // P2: bez tego caly /api/import trzymalby pelny plik (do ~128 KB+ wewn. RAM)
  // przez kopie i podmiane, co na malym heapie spychalo go na przyspieszenie
  // fragmentacji (pamietamy: heap wewn. wspoldzielony z Wi-Fi/TLS/LVGL/serwerem).
  body.clear();
  body = String(); // minimalny cap zamiast trzymania 512 KB bufora

  if (!headerSeen || rowCount == 0) {
    LittleFS.remove("/karmienia_import.tmp");
    sendJson(400, "{\"message\":\"Plik nie zawiera zadnego poprawnego wiersza danych.\"}");
    return;
  }

  if (!LittleFS.rename("/karmienia_import.tmp", DATA_FILE_PATH)) {
    LittleFS.remove("/karmienia_import.tmp");
    sendJson(500, "{\"message\":\"Nie udalo sie podmienic pliku — dane pozostaly nietkiete.\"}");
    return;
  }

  invalidateDayStats();
  loadLatestEntries();
  updateHomeInformation();
  requestHostSync(); // po imporcie zsynchronizuj CSV z panelem WWW
  String payload = "{\"message\":\"Zaimportowano ";
  payload += rowCount;
  payload += " wpisow.";
  if (skippedRows > 0) {
    payload += " Pominieto ";
    payload += skippedRows;
    payload += " niepoprawnych.";
  }
  payload += "\"}";
  sendJson(200, payload);
}

// Zmiana trwalego ustawienia z panelu WWW. Body: key=..., value=... (0/1).
// Obecnie obslugiwane: sleepTelegram (powiadomienia o oknie snu).
void handleApiSetting() {
  const String key = webServer.arg("key");
  const String value = webServer.arg("value");
  if (key == "sleepTelegram") {
    sleepTelegramEnabled = (value.toInt() != 0);
    saveSettings();
    sendJson(200, String("{\"message\":\"Zapisano.\",\"sleepTelegram\":") +
                  (sleepTelegramEnabled ? "true" : "false") + "}");
    return;
  }
  sendJson(400, "{\"message\":\"Nieznane ustawienie.\"}");
}

void handleWebNotFound() {
  if (webServer.uri().startsWith("/api/")) {
    sendJson(404, "{\"message\":\"Nie znaleziono adresu API.\"}");
  } else {
    webServer.send(404, "text/plain; charset=utf-8", "Nie znaleziono strony.");
  }
}

void startWebServer() {
  if (webServerStarted || WiFi.status() != WL_CONNECTED) return;
  if (!webRoutesConfigured) {
    webServer.on("/", HTTP_GET, handleWebRoot);
    webServer.on("/api/status", HTTP_GET, handleApiStatus);
    webServer.on("/api/entries", HTTP_GET, handleApiEntries);
    webServer.on("/api/weight-series", HTTP_GET, handleApiWeightSeries);
    webServer.on("/api/entry", HTTP_POST, handleApiEntry);
    webServer.on("/api/delete-entry", HTTP_POST, handleApiDeleteEntry);
    webServer.on("/api/update-feeding", HTTP_POST, handleApiUpdateFeeding);
    webServer.on("/api/send-backup", HTTP_POST, handleApiSendBackup);
    webServer.on("/api/event", HTTP_POST, handleApiEvent);
    webServer.on("/export.csv", HTTP_GET, handleExportCsv);
    webServer.on("/api/import", HTTP_POST, handleApiImport);
    webServer.on("/api/setting", HTTP_POST, handleApiSetting);
    webServer.onNotFound(handleWebNotFound);
    webRoutesConfigured = true;
  }
  // Zamyka ewentualne gniazdo po poprzednim rozłączeniu, następnie otwiera port 80 od nowa.
  webServer.stop();
  delay(20);
  webServer.begin();
  webServerStarted = true;
  Serial.printf("HTTP: serwer gotowy pod adresem http://%s/\n", WiFi.localIP().toString().c_str());
}

// ----------------------------------------------------------------------------
// mDNS (v4: wyłączone). Wywoływane z initOptionalServices() pod #if FEATURE_MDNS.
// ----------------------------------------------------------------------------
void initMdns() {
  if (MDNS.begin("karmienie")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://karmienie.local gotowe.");
  } else {
    Serial.println("mDNS: start nieudany.");
  }
}

// ----------------------------------------------------------------------------
// OTA (v4: wyłączone). Wywoływane z initOptionalServices() pod #if FEATURE_OTA.
// ----------------------------------------------------------------------------
void initOta() {
  if (OTA_PASSWORD[0] != '\0') {
    ArduinoOTA.setHostname("karmienie");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
      otaInProgress = true;
      Serial.println("OTA: aktualizacja w toku...");
    });
    ArduinoOTA.onEnd([]() { otaInProgress = false; });
    ArduinoOTA.onError([](ota_error_t error) {
      otaInProgress = false;
      Serial.printf("OTA: blad %u.\n", error);
    });
    ArduinoOTA.begin();
    Serial.println("OTA: aktywne.");
  } else {
    Serial.println("OTA: wylaczone (brak hasla w config.h).");
  }
}

// W loop(): obsluga trwajacego transferu OTA (zamraza UI podczas wgrywania).
void otaTick() {
  if (otaInProgress) {
    ArduinoOTA.handle();
    delay(10);
    return;
  }
  ArduinoOTA.handle();
}
