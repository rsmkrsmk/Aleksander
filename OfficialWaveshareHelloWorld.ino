/*
  Rejestr karmienia Aleksandra
  Płytka: Waveshare ESP32-S3-Touch-LCD-4B
  Środowisko: Arduino IDE, ESP32 by Espressif Systems >= 3.2.0, LVGL 9.x

  Przed kompilacją:
  1. Skopiuj secrets.h.example jako secrets.h.
  2. Uzupełnij SSID i hasło do Wi-Fi.
  3. Nie są potrzebne żadne dodatkowe moduły ani przewody do przechowywania danych.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
// Te biblioteki rdzenia sa wymagane przez szkic: mDNS, OTA, pogoda i Telegram.
#define FEATURE_MDNS 1
#define FEATURE_OTA 1
#define FEATURE_HTTPCLIENT 1

#include <time.h>
#include <math.h>
#include <LittleFS.h>
#include <Wire.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <TouchDrv.hpp>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#include "config.h"
#include "secrets.h"
#include "development_tips.h"
#include "dressing_guide.h"
#include "web_panel.h"

// -------------------- Sprzęt Waveshare ESP32-S3-Touch-LCD-4B --------------------
constexpr uint8_t I2C_SDA = 47;
constexpr uint8_t I2C_SCL = 48;
constexpr uint8_t GT911_ADDRESS = 0x14;
constexpr uint16_t SCREEN_WIDTH = 480;
constexpr uint16_t SCREEN_HEIGHT = 480;

// Ekspander pozostaje używany do sekwencji zasilania i komend ST7701.
Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(7, 0, 2, 1, &Wire, 0x20);
esp_lcd_panel_handle_t rgbPanel = nullptr;
SemaphoreHandle_t rgbColorTransferDoneSemaphore = nullptr;
void *rgbFrameBuffer0 = nullptr;
void *rgbFrameBuffer1 = nullptr;

lv_display_t *displayDriver = nullptr;
lv_indev_t *touchDriver = nullptr;

// Oficjalny sterownik GT911 z biblioteki SensorLib wymagany dla dotyku Waveshare.
TouchDrvGT911 GT911;
int16_t gt911X[5] = {};
int16_t gt911Y[5] = {};
bool touchReady = false;
bool backlightReady = false;
bool screenDimmed = false;
uint32_t lastUserActivityMillis = 0;

constexpr uint8_t BACKLIGHT_PIN = 4;
constexpr uint32_t BACKLIGHT_PWM_HZ = 5000;
constexpr uint8_t BACKLIGHT_PWM_BITS = 8;
constexpr uint8_t BACKLIGHT_FULL_DUTY = 0;    // Sterownik AP3032: PWM aktywne niskim poziomem.
constexpr uint8_t BACKLIGHT_DIM_DUTY = 119;   // Ustawienie przyciemnienia bez zmian.
constexpr uint32_t SCREEN_DIM_TIMEOUT_MS = 60UL * 1000UL;

// ------------------------------ Stan aplikacji ---------------------------------
bool storageReady = false;
bool dataFileHuge = false;          // true gdy plik danych przekroczyl prog rotacji (ostrzezenie)
bool timeIsValid = false;
bool extraMilkEnabled = false;
bool extraMilkModified = false;
int selectedMilkMl = DEFAULT_ML;

// Minuty karmienia piersią w bieżącym wpisie (lewa/prawa); edytowane przyciskami -5/+5.
struct PiersControl {
  int value;
  lv_obj_t *label;
};
PiersControl piersLeftCtl = {0, nullptr};
PiersControl piersRightCtl = {0, nullptr};

// Ekran odciągania mleka oraz stan przycisku COFNIJ.
int selectedPumpingMl = DEFAULT_ML;
lv_obj_t *pumpingValueLabel = nullptr;
String deletedEntryDescription; // opis ostatnio usunietego wpisu
int summaryExtraDays = 0; // ile dodatkowych dni pokazuje PODSUMOWANIE (lazy historia)
constexpr int SUMMARY_MAX_EXTRA_DAYS = 56;

// --------------------------- Wygaszacz ekranu i pogoda --------------------------
struct WeatherState {
  bool valid;
  int tempNow;
  int feelsLike;
  int codeNow;
  int tempMinDay;
  int tempMaxDay;
  struct {
    uint8_t hour;
    uint8_t minute;
    int temp;
    bool has;
  } next[3];
};
WeatherState weatherState = {};
volatile bool weatherFetchPending = false;
volatile bool weatherDataReady = false;
volatile bool weatherBusyFlag = false;
uint32_t weatherLastTryMs = 0;
uint32_t weatherNextTryMs = 0;
TaskHandle_t weatherTaskHandle = nullptr;
SemaphoreHandle_t weatherMutex = nullptr;
// UWAGA: definicje snapshotWeather()/weatherValidNow() celowo NIE tutaj, lecz
// ponizej struct CsvEntry. Arduino wstawia generowane prototypy tuz nad PIERWSZA
// funkcja pliku — gdyby pierwsza funkcja byla powyzej definicji typow (CsvEntry,
// WeatherKind), prototypy trafilyby nad te typy i kompilacja by sie wywalila.

bool screensaverActive = false;
lv_obj_t *ssClockLabel = nullptr;
lv_obj_t *ssClockShadowLabel = nullptr;
lv_obj_t *ssDateLabel = nullptr;
lv_obj_t *ssIconBox = nullptr;
lv_obj_t *ssTempLabel = nullptr;
lv_obj_t *ssDescLabel = nullptr;
lv_obj_t *ssMinMaxLabel = nullptr;
lv_obj_t *ssDressLabel = nullptr;
lv_obj_t *ssLastFeedingLabel = nullptr;
lv_obj_t *ssSleepLabel = nullptr;   // wygaszacz: 2. wiersz (informacja o drzemce)
lv_obj_t *homeCounterBar = nullptr;
lv_obj_t *ssClockCard = nullptr;
lv_obj_t *ssWeatherCard = nullptr;
lv_obj_t *ssHourLabels[3] = {nullptr, nullptr, nullptr};
lv_obj_t *ssNextFeedLabel = nullptr;   // (nieuzywany osobno) — scalony w linii karmienia
lv_obj_t *ssDayBandCard = nullptr;     // karta statystyk dnia
lv_obj_t *ssDayBandTrack = nullptr;    // (alias karty statystyk)
lv_obj_t *ssStatValue[3] = {nullptr, nullptr, nullptr}; // KARMIENIA / NAJDL. PRZERWA / SR. PRZERWA
int ssDayBandStamp = -1;               // sygnatura ostatnio odswiezonych statystyk
String ssRenderedNextFeed;
String ssRenderedStat[3];
int ssLastIconCode = -999;
lv_timer_t *ssClockTimer = nullptr;
String ssRenderedClock;
String ssRenderedDate;
String ssRenderedTemp;
String ssRenderedDescription;
String ssRenderedMinMax;
String ssRenderedDress;
String ssRenderedLastFeeding;
String ssRenderedSleep;
String ssRenderedHours[3];

// Dolna czesci home ukrywane w trybie wygaszacza.
lv_obj_t *feedingCard = nullptr;
lv_obj_t *milkCard = nullptr;
lv_obj_t *feedFormButton = nullptr;
lv_obj_t *otherHomeButton = nullptr;   // "INNE" -> ekran z pielucha i odciagiem
lv_obj_t *weightHomeButton = nullptr;  // "WAGA" -> ekran wpisu wagi
lv_obj_t *sleepHomeButton = nullptr;   // "SEN" -> ekran snu (wake windows, bilans)
lv_obj_t *calendarButton = nullptr;
lv_obj_t *chartButton = nullptr;

// Waga: aktualny wybor w gramach na ekranie WAGA (domyslnie DEFAULT_WEIGHT_G).
int selectedWeightG = DEFAULT_WEIGHT_G;
lv_obj_t *weightValueLabel = nullptr;

// Zagregowana statystyka jednego dnia — zasila ekran główny, kalendarz,
// PODSUMOWANIE oraz API WWW. Rozpoznaje wszystkie typy wpisów CSV.
struct DaySummary {
  int feedingCount;
  int milkCount;
  int milkMl;
  int motherMilkMl;
  int modifiedMilkMl;
  int piersLeftMin;
  int piersRightMin;
  int diaperWet;
  int diaperDirty;
  int pumpingMl;
  bool vitaminD;
  int weightG;   // ostatnia zapisana waga danego dnia (g); 0 = brak
  int sleepDayMin;   // minuty snu dziennego (drzemki) przypisane do tego dnia
  int sleepNightMin; // minuty snu nocnego przypisane do tego dnia
  int napCount;      // liczba drzemek dziennych rozpoczetych tego dnia
};

// Grupa pogody dla ikony/opisu wygaszacza (kody wttr.in mapowane na WMO).
// Musi być przed pierwszą funkcją pliku — patrz uwaga o prototypach Arduino.
enum WeatherKind : uint8_t { W_SUN, W_PARTLY, W_CLOUD, W_FOG, W_RAIN, W_SNOW, W_STORM };
time_t selectedEntryTime = 0;
time_t selectedCalendarDay = 0;
time_t calendarDays[3] = {};
bool formReturnToCalendar = false;
bool lastUiWifiConnected = false;
bool webServerStarted = false;
bool webRoutesConfigured = false;
uint32_t lastReconnectAttempt = 0;
uint32_t lastLvglTickMs = 0;
WebServer webServer(80);

bool nightModeActive = false;
uint32_t lastNtpSyncMs = 0;
long lastBackupDayStamp = 0;        // yyyymmdd ostatniej kopii zapasowej
String pendingTelegramText;         // max 1 wiadomosc w kolejce (chroniona telegramMutex)
uint32_t telegramNextAttemptMs = 0;
// Automatyczny backup przez Telegram: B_IDLE (wolny), B_WANTED (czeka), B_SENDING.
enum BackupState { B_IDLE, B_WANTED, B_SENDING };
BackupState backupState = B_IDLE;   // chroniony telegramMutex
String backupFileName;              // chroniony telegramMutex
// Wysylka Telegrama (TLS) jest blokujaca — wynosimy ja do osobnego zadania FreeRTOS
// na rdzeniu 0, aby nie zamrazac loop()/LVGL/dotyku. Kolejka i stan backupu sa
// wspoldzielone miedzy rdzeniami, wiec dostep chronimy mutexem.
TaskHandle_t telegramTaskHandle = nullptr;
SemaphoreHandle_t telegramMutex = nullptr;
bool otaInProgress = false;         // podczas OTA wstrzymujemy odswiezanie LVGL
time_t lastFeedingTime = 0;         // czas ostatniego KARMIENIE (do licznika "temu")
time_t lastMilkTime = 0;
int lastWeightG = 0;                // ostatnia zapisana waga (g); 0 = brak wpisu
int avgFeedingGapMin = 0;           // sredni odstep miedzy karmieniami DZIS (min); 0 = za malo danych
int longestFeedingGapMin = 0;       // najdluzsza przerwa miedzy karmieniami DZIS (min)
int todayFeedingCount = 0;          // liczba karmien DZIS (tylko typ KARMIENIE)
time_t nextFeedingEta = 0;          // przewidywany czas nastepnego karmienia = ostatnie + 4h
bool sleepInProgress = false;       // true gdy ostatnie zdarzenie snu to SEN_START (dziecko spi)
time_t sleepStartedTime = 0;        // czas rozpoczecia biezacego snu (0 = nie spi)
time_t lastWakeTime = 0;            // czas ostatniego przebudzenia (SEN_STOP) — start okna czuwania
bool sleepTelegramEnabled = false;  // powiadomienia Telegram o oknie snu (ustawienie trwale)
// Flagi jednorazowej wysylki powiadomien o oknie snu (reset przy zasnieciu/nowym czuwaniu).
time_t sleepNotifyAnchor = 0;       // dla ktorego lastWakeTime wyslano powiadomienia
bool sleepNotifiedWindow = false;   // wyslano "okno drzemki"
bool sleepNotifiedOver = false;     // wyslano "przekroczone okno"
bool deleteModeActive = false;         // tryb wyboru wpisu do usuniecia
int pendingDeleteIndex = -1;            // indeks oczekujacy na potwierdzenie (-1 = brak)

String lastFeeding = "Brak zapisanego wpisu";
String lastMilk = "Brak zapisanego wpisu";

// CPU load estimation: mierzymy czas aktywny vs czas sciany.
static uint64_t cpuBusyUs = 0;
static uint64_t cpuTotalUs = 0;
static uint32_t cpuCalcLastMs = 0;
static int cpuLoadPct = 0;

// ------------------------------ Diagnostyka / watchdog ------------------------------
// Watchdog zadaniowy pilnuje petli loop(): jesli cos zablokuje ja na dluzej niz
// WATCHDOG_TIMEOUT_S, ESP32 wykonuje kontrolowany restart (zamiast wisiec).
constexpr uint32_t WATCHDOG_TIMEOUT_S = 30;
bool watchdogReady = false;
// Liczniki przetrwaja miekki restart (RTC slow memory nie jest zerowane przy reboot).
RTC_NOINIT_ATTR uint32_t bootCount;          // ile razy urzadzenie sie uruchomilo
RTC_NOINIT_ATTR uint32_t watchdogResetCount; // ile razy zresetowal watchdog (TG*WDT)
RTC_NOINIT_ATTR uint32_t rtcMagic;           // znacznik poprawnej inicjalizacji RTC
constexpr uint32_t RTC_MAGIC_VALUE = 0xA1EC5AADUL;
int lastResetReason = 0;                     // esp_reset_reason() z tego uruchomienia
uint32_t minFreeHeapEver = 0xFFFFFFFFUL;     // najnizszy zaobserwowany wolny heap wewn.
uint32_t httpRequestCount = 0;               // ile zadan HTTP obsluzono (licznik ogolny)
uint32_t lastHttpMillis = 0;                 // millis() ostatniego obsluzonego zadania HTTP
uint32_t telegramFailCount = 0;              // nieudane proby Telegrama z rzedu (limit ponawiania)
constexpr uint32_t TELEGRAM_MAX_FAILS = 5;   // po tylu bledach z rzedu odpuszczamy wpis

// Zapamiętane teksty ograniczają odrysowywanie ekranu RGB tylko do faktycznie zmienionych danych.
String renderedClock;
String renderedAge;
String renderedFeeding;
String renderedMilk;

lv_obj_t *homeScreen = nullptr;
lv_obj_t *formScreen = nullptr;
lv_obj_t *calendarScreen = nullptr;
lv_obj_t *dayDetailScreen = nullptr;
lv_obj_t *chartScreen = nullptr;
lv_obj_t *pumpingScreen = nullptr;
lv_obj_t *diaperScreen = nullptr;
lv_obj_t *otherScreen = nullptr;   // ekran INNE: pielucha + odciag pokarmu
lv_obj_t *weightScreen = nullptr;  // ekran WAGA: wybor wagi w gramach
lv_obj_t *sleepScreen = nullptr;   // ekran SEN: wake windows, predykcja drzemki, bilans
lv_obj_t *diagnosticsScreen = nullptr; // ekran DIAGNOSTYKA (stan urzadzenia)
lv_obj_t *homeLedWifi = nullptr;
lv_obj_t *homeLedMemory = nullptr;
lv_obj_t *homeLedTime = nullptr;
lv_obj_t *homeClockLabel = nullptr;
lv_obj_t *homeAgeLabel = nullptr;
lv_obj_t *homeFeedingLabel = nullptr;
lv_obj_t *homeMilkLabel = nullptr;
lv_obj_t *homeCounterLabel = nullptr;
String renderedCounter;
bool counterAlarmPhase = false;
int16_t counterRemainMin = -1;   // minuty do nastepnego karmienia; -1 = brak danych
lv_timer_t *counterAlarmTimer = nullptr;
lv_obj_t *formDateTimeLabel = nullptr;
lv_obj_t *formMlLabel = nullptr;
lv_obj_t *formMilkMlLabel = nullptr;
lv_obj_t *formMilkCard = nullptr;
lv_obj_t *formMilkMatkiButton = nullptr;
lv_obj_t *formMilkModifiedButton = nullptr;
lv_obj_t *formBottleToggleButton = nullptr;
lv_obj_t *formBottleToggleLabel = nullptr;
lv_obj_t *formSaveButton = nullptr;
lv_obj_t *formCancelButton = nullptr;
lv_obj_t *formStatusLabel = nullptr;
lv_obj_t *formConfirmOverlay = nullptr;

// ------------------------------- Kolory interfejsu ------------------------------
// Zmienne (nie stałe): applyTheme() przełącza paletę dzienną/nocną.
lv_color_t COLOR_BACKGROUND = lv_color_hex(0xF6F8F1);
lv_color_t COLOR_TEXT = lv_color_hex(0x243528);
lv_color_t COLOR_MUTED = lv_color_hex(0x4E6252);
lv_color_t COLOR_BLUE = lv_color_hex(0x3E5E9B);
lv_color_t COLOR_GREEN = lv_color_hex(0x356D43);
lv_color_t COLOR_ORANGE = lv_color_hex(0xA85432);
lv_color_t COLOR_RED = lv_color_hex(0xB84C4C);
lv_color_t COLOR_YELLOW = lv_color_hex(0xA9902C);
lv_color_t COLOR_CARD = lv_color_hex(0xFFFFFF);
lv_color_t COLOR_BORDER = lv_color_hex(0xCBDFC4);
lv_color_t COLOR_TONAL_GREEN = lv_color_hex(0xE6F1E0);

// UWAGA: applyTheme() musi pozostać PONIŻEJ struct CsvEntry — Arduino wstawia
// automatyczne prototypy przed pierwszą funkcją pliku. Definicja jest przy sekcji UI.

// ----------------------------- Deklaracje funkcji --------------------------------
void touchRead(lv_indev_t *indev, lv_indev_data_t *data);
void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixelMap);
void resyncRgbPanelIfDue();
void requestRgbResync();
void initialiseDisplayPanel();
void initialiseBacklight();
void setScreenDimmed(bool dimmed);
void registerUserActivity();
void updateScreenDimming();

void connectWiFi();
void retryWiFiConnection();
bool syncTimeFromNTP();
void beginNtp();
bool waitForNtp(uint32_t timeoutMs);
bool initialiseStorage();
void loadLatestEntries();
void recomputeFeedingRhythm();
// --- Sen (Napper): wake windows, predykcja drzemki, ustawienia ---
struct WakeWindow { int minMin; int maxMin; };
WakeWindow wakeWindowMinutes(long ageDays);
void sleepNeedMinutes(long ageDays, int &nightOut, int &dayOut);
int napTargetCount(long ageDays);
int interpTable(long x, const int *xs, const int *ys, int n);
void loadSettings();
bool saveSettings();
void checkSleepNotifications();
String nextFeedingClock();
String formatGapShort(int minutes);
void initWatchdog();
void feedWatchdog();
void drawBabyFace(lv_obj_t *box);
void showBootScreen();
void bootStep(uint8_t idx, uint8_t state);
void bootPumpLvgl();
bool appendEntry(const char *entryType, time_t when, int ml, int piersLeft = -1, int piersRight = -1);
bool copyLittleFsFile(const char *srcPath, const char *dstPath);
void archiveDataFileIfHuge();
bool isMilkType(const String &entryType);
String milkTypeLabel(const String &entryType);
String entriesForDay(time_t day, bool compact);
void populateDayEntries(lv_obj_t *container, time_t day);
void invalidateDayStats();
void accrueNapCount(time_t start, const String iso[]);
void accrueSleepInterval(time_t start, time_t stop, const String iso[]);
void dayStats(time_t day, DaySummary &out);
String formatDaySummaryLine(const DaySummary &s);
String formatDayExtraLine(const DaySummary &s);
bool deleteEntryByIndex(int entryIndex, String &removedDescription);
time_t dayOffsetFromToday(uint8_t daysBack);
time_t beginningOfDay(time_t value);
String dateIso(time_t value);
String calendarDayTitle(time_t value, uint8_t index);

String formatDateTime(time_t value);
String formatEntryForUi(const String &csvLine);
long calculateAgeDays();
String calculateAgeText();
String developmentTipForToday();
void updateCounterAlarmVisuals();
void counterAlarmTickCb(lv_timer_t *timer);
void updateHomeInformation();
void createHomeScreen();
void createCalendarScreen();
void createDayDetailScreen(time_t day);
void createFeedingChartScreen();
void openEntryForm();
void openEntryFormForDay(time_t day);
void createFormScreen();
void showFormConfirm();
void hideFormConfirm();
void performSaveForm();
void confirmFormSave(lv_event_t *event);
void confirmFormCancel(lv_event_t *event);
void updateExtraMilkVisibility();
void updateMilkTypeButtons();
void toggleBottleEvent(lv_event_t *event);
void diaperOpenEvent(lv_event_t *event);
void deleteToggleEvent(lv_event_t *event);
void deleteEntryEvent(lv_event_t *event);
void moreDaysEvent(lv_event_t *event);
void diaperQuickEvent(lv_event_t *event);
void pumpingOpenEvent(lv_event_t *event);
void pumpingSaveEvent(lv_event_t *event);
void vitaminToggleEvent(lv_event_t *event);
void createDiaperScreen();
void createPumpingScreen();
void createOtherScreen();
void otherOpenEvent(lv_event_t *event);
void backToOtherEvent(lv_event_t *event);
void createSleepScreen();
void sleepOpenEvent(lv_event_t *event);
String formatDurationShort(long minutes);
void createDiagnosticsScreen();
void diagnosticsOpenEvent(lv_event_t *event);
void sleepTelegramSwitchEvent(lv_event_t *event);
const char *resetReasonText(int reason);
void createWeightScreen();
void weightOpenEvent(lv_event_t *event);
void weightSaveEvent(lv_event_t *event);
void weightStepEvent(lv_event_t *event);

void startWebServer();
void handleWebRoot();
void handleApiStatus();
void handleApiEntries();
void handleApiWeightSeries();
void handleApiEntry();
void handleApiDeleteEntry();
void handleApiSendBackup();
void handleApiEvent();
void handleExportCsv();
void handleApiImport();
void handleApiSetting();
void handleWebNotFound();

bool appendBackupIfDue();
String buildBackupFileName();
bool sendBackupViaTelegram(const String &fileName);
void resyncNtpIfDue();
void queueTelegram(const String &text);
String telegramTextFor(const String &type, int ml, int piersLeft, int piersRight, time_t when);
void pumpTelegramQueue();
void telegramTask(void *parameter);
void wakeTelegramTask();
void queueTelegramStartup();
void initOptionalServices();
void updateNightMode();
void agingTickCb(lv_timer_t *timer);
void enterScreensaver();
void exitScreensaver();
void updateScreensaverContent();
void applyScreensaverVisibility();
void fetchWeatherNow();
void weatherTask(void *parameter);
void requestWeatherFetch();
bool loadWeatherCache();
bool saveWeatherCache();
String jsonEscape(const String &value);
String webDateTime(time_t value);
bool parseWebDateTime(const String &value, time_t &result);
WeatherState snapshotWeather(); // WeatherState zdefiniowany wyzej (linia ~104)
bool weatherValidNow();

// ------------------------------- Struktura wpisu CSV -----------------------------
// Musi być zdefiniowana przed pierwszą funkcją pliku: Arduino wstawia generowane
// prototypy tuż nad nią, więc sygnatura parseCsvLine() musi już znać ten typ.
struct CsvEntry {
  String date;
  String time;
  String type;
  int ml;
  int piersLeft;
  int piersRight;
};

// Bezpieczny odczyt weatherState (zapisywanego z weatherTask na rdzeniu 0).
// Umieszczone TU (za definicjami typow), bo to pierwsza funkcja pliku — patrz uwaga
// o generowanych prototypach Arduino przy deklaracji weatherMutex.
// Kopia calej struktury pod mutexem; przy braku mutexa/timeoutcie zwraca ostatnia
// widoczna wartosc (spojnosc pojedynczych intow wystarcza jako fallback).
WeatherState snapshotWeather() {
  WeatherState copy;
  if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    copy = weatherState;
    xSemaphoreGive(weatherMutex);
  } else {
    copy = weatherState;
  }
  return copy;
}

// Skrot: samo pole valid (te same reguly dostepu co snapshotWeather).
bool weatherValidNow() { return snapshotWeather().valid; }

// -------------------------------- Pomocnicze UI ----------------------------------
void prepareScreen(lv_obj_t *screen) {
  lv_obj_set_style_bg_color(screen, COLOR_BACKGROUND, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void createReusableScreenRoots() {
  lv_obj_t **screens[] = {&homeScreen, &formScreen, &calendarScreen, &dayDetailScreen, &chartScreen,
                          &pumpingScreen, &diaperScreen, &otherScreen, &weightScreen, &sleepScreen,
                          &diagnosticsScreen};
  for (lv_obj_t **screen : screens) {
    *screen = lv_obj_create(nullptr);
    prepareScreen(*screen);
  }
}

void resetReusableScreen(lv_obj_t *screen) {
  // Korzeń ekranu pozostaje stały; wymieniamy wyłącznie jego bieżącą zawartość.
  lv_obj_clean(screen);
  prepareScreen(screen);
}

void loadReusableScreen(lv_obj_t *screen) {
  // auto_del=false zachowuje poprzednie korzenie ekranów do ponownego użycia.
  lv_screen_load_anim(screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false);
}

lv_obj_t *createLabel(lv_obj_t *parent, const char *text, lv_color_t color, lv_align_t align, int x, int y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, align, x, y);
  return label;
}

void setLabelTextIfChanged(lv_obj_t *label, String &previous, const String &value) {
  if (!label || previous == value) return;
  lv_label_set_text(label, value.c_str());
  previous = value;
}

lv_obj_t *createCard(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 24, 0);
  lv_obj_set_style_border_color(card, COLOR_BORDER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0xAAB9A7), 0);
  lv_obj_set_style_shadow_width(card, 10, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

lv_obj_t *createButton(lv_obj_t *parent, const char *caption, int x, int y, int width, int height, lv_color_t color) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_bg_color(button, color, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_shadow_color(button, lv_color_mix(color, COLOR_TEXT, 65), 0);
  lv_obj_set_style_shadow_width(button, 5, 0);
  lv_obj_set_style_shadow_opa(button, LV_OPA_20, 0);
  // Efekt wciśnięcia używa wyłącznie stylów dostępnych w LVGL 9.3.
  lv_obj_set_style_shadow_width(button, 1, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_90, LV_STATE_PRESSED);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  return button;
}

// --------------------------- Sterownik ekranu i dotyku ---------------------------
void applyTheme(bool night) {
  if (night) {
    COLOR_BACKGROUND = lv_color_hex(0x1C2420);
    COLOR_TEXT = lv_color_hex(0xE8EFE4);
    COLOR_MUTED = lv_color_hex(0x9DB3A0);
    COLOR_BLUE = lv_color_hex(0x7C9BD1);
    COLOR_GREEN = lv_color_hex(0x7FB88A);
    COLOR_ORANGE = lv_color_hex(0xD98B5F);
    COLOR_YELLOW = lv_color_hex(0xD9C24A);
    COLOR_RED = lv_color_hex(0xD97C7C);
    COLOR_CARD = lv_color_hex(0x26302A);
    COLOR_BORDER = lv_color_hex(0x3A473E);
    COLOR_TONAL_GREEN = lv_color_hex(0x24322A);
  } else {
    COLOR_BACKGROUND = lv_color_hex(0xF6F8F1);
    COLOR_TEXT = lv_color_hex(0x243528);
    COLOR_MUTED = lv_color_hex(0x4E6252);
    COLOR_BLUE = lv_color_hex(0x3E5E9B);
    COLOR_GREEN = lv_color_hex(0x356D43);
    COLOR_ORANGE = lv_color_hex(0xA85432);
    COLOR_RED = lv_color_hex(0xB84C4C);
    COLOR_YELLOW = lv_color_hex(0xA9902C);
    COLOR_CARD = lv_color_hex(0xFFFFFF);
    COLOR_BORDER = lv_color_hex(0xCBDFC4);
    COLOR_TONAL_GREEN = lv_color_hex(0xE6F1E0);
  }
}

// Wymagana sekwencja ekspandera TCA9554 z oficjalnego demo Waveshare.
// Bez niej panel ST7701 pozostaje w stanie resetu/zasilania i ekran jest czarny.
void initialiseDisplayPanel() {
  expander->pinMode(5, OUTPUT);
  expander->pinMode(6, OUTPUT);
  expander->digitalWrite(6, LOW);
  delay(200);
  expander->digitalWrite(5, LOW);
  delay(200);
  expander->digitalWrite(5, HIGH);
  delay(200);
}

void initialiseBacklight() {
  // Oficjalny schemat 4B: GPIO4 (BL) steruje wejściem CTRL układu AP3032.
  // Dla tego sterownika niższe wypełnienie PWM oznacza większą jasność.
  ledcAttach(BACKLIGHT_PIN, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
  ledcWrite(BACKLIGHT_PIN, BACKLIGHT_FULL_DUTY);
  backlightReady = true;
  screenDimmed = false;
  lastUserActivityMillis = millis();
  Serial.println("Podswietlenie: PWM GPIO4 gotowe (100%).");
}

void setScreenDimmed(bool dimmed) {
  if (!backlightReady || screenDimmed == dimmed) return;
  // Powrót z przyciemnienia w nocy prowadzi do poziomu motywu nocnego, nie pelnej jasnosci.
  const uint8_t targetDuty = dimmed ? BACKLIGHT_DIM_DUTY
                                    : (nightModeActive ? BACKLIGHT_NIGHT_DUTY : BACKLIGHT_FULL_DUTY);
  ledcWrite(BACKLIGHT_PIN, targetDuty);
  screenDimmed = dimmed;
  Serial.printf("Podswietlenie: %s (PWM %u).\n", dimmed ? "przyciemnione" : "pelne", targetDuty);
}

void registerUserActivity() {
  lastUserActivityMillis = millis();
  if (screenDimmed) setScreenDimmed(false);
}

void updateScreenDimming() {
  // W trybie nocnym bazowa jasnosc motywu wystarcza — pomijamy dodatkowe przyciemnienie,
  // aby nie przełączać PWM podczas pracy panelu RGB.
  if (!backlightReady || screenDimmed || nightModeActive) return;
  if (millis() - lastUserActivityMillis >= SCREEN_DIM_TIMEOUT_MS) {
    setScreenDimmed(true);
  }
}

bool rgbColorTransferDoneCallback(esp_lcd_panel_handle_t panel,
                                  const esp_lcd_rgb_panel_event_data_t *edata,
                                  void *userCtx) {
  (void)panel;
  (void)edata;
  SemaphoreHandle_t semaphore = static_cast<SemaphoreHandle_t>(userCtx);
  BaseType_t highPriorityTaskWoken = pdFALSE;
  if (semaphore) {
    xSemaphoreGiveFromISR(semaphore, &highPriorityTaskWoken);
  }
  return highPriorityTaskWoken == pdTRUE;
}

bool initialiseNativeRgbPanel() {
  // Zachowujemy kolejność inicjalizacji używaną wcześniej przez Arduino_GFX:
  // magistrala ekspandera, reset programowy ST7701, komendy init, potem RGB DMA.
  if (!expander->begin()) {
    Serial.println("LCD: nie mozna uruchomic magistrali ekspandera.");
    return false;
  }
  expander->sendCommand(0x01);
  delay(120);
  expander->batchOperation(st7701_type1_init_operations,
                           sizeof(st7701_type1_init_operations));

  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.timings.pclk_hz = 6000000;
  config.timings.h_res = SCREEN_WIDTH;
  config.timings.v_res = SCREEN_HEIGHT;
  config.timings.hsync_pulse_width = 8;
  config.timings.hsync_back_porch = 50;
  config.timings.hsync_front_porch = 14;
  config.timings.vsync_pulse_width = 8;
  config.timings.vsync_back_porch = 20;
  config.timings.vsync_front_porch = 10;
  config.timings.flags.hsync_idle_low = 0;
  config.timings.flags.vsync_idle_low = 0;
  config.timings.flags.de_idle_high = 0;
  config.timings.flags.pclk_active_neg = 0;
  config.timings.flags.pclk_idle_high = 0;
  config.data_width = 16;
  config.bits_per_pixel = 16;
  // Dwa framebuffery w PSRAM (double_fb) + bufory bounce DMA. LVGL renderuje
  // do niewidocznego bufora, esp_lcd przelacza je bez kopiowania i bez tearingu.
  config.num_fbs = 2;
  // bounce_buffer_size_px musi dzielic SCREEN_WIDTH * SCREEN_HEIGHT bez reszty.
  // 80 linii × 480 = 38400 pikseli; 230400 / 38400 = 6 (calkowite).
  // DMA uzywa 2 buforow bounce w RAM WEWNETRZNYM (nie PSRAM!): 2 × 80 × 480 × 2 = 150 KB.
  // Zostawiamy 80 linii (a nie wiecej): bounce zajmuje deficytowy RAM wewnetrzny
  // wspoldzielony z Wi-Fi/TLS/LVGL/stosami zadan, a glownym zabezpieczeniem przed
  // DRYFEM obrazu jest teraz cykliczny restart DMA panelu (resyncRgbPanelIfDue) —
  // wiec nie ryzykujemy braku RAM. Liczba linii musi dzielic 230400 bez reszty
  // (dozwolone m.in. 48/60/80/96/120 linii).
  config.bounce_buffer_size_px = SCREEN_WIDTH * 80;
  config.sram_trans_align = 8;
  // 64 = sprawdzona w przykladach Espressif wartosc (musi byc potega 2).
  // Nie zwiekszamy: glowna bronia przeciw artefaktom jest wiekszy bounce buffer,
  // a zbyt duzy burst wydluza pojedyncze zajecie magistrali PSRAM.
  config.dma_burst_size = 64;
  config.hsync_gpio_num = 46;
  config.vsync_gpio_num = 3;
  config.de_gpio_num = 17;
  config.pclk_gpio_num = 9;
  config.disp_gpio_num = GPIO_NUM_NC;
  // Kolejność odpowiada wcześniejszemu mapowaniu Arduino_ESP32RGBPanel.
  config.data_gpio_nums[0] = 40;
  config.data_gpio_nums[1] = 41;
  config.data_gpio_nums[2] = 42;
  config.data_gpio_nums[3] = 2;
  config.data_gpio_nums[4] = 1;
  config.data_gpio_nums[5] = 21;
  config.data_gpio_nums[6] = 8;
  config.data_gpio_nums[7] = 18;
  config.data_gpio_nums[8] = 45;
  config.data_gpio_nums[9] = 38;
  config.data_gpio_nums[10] = 39;
  config.data_gpio_nums[11] = 10;
  config.data_gpio_nums[12] = 11;
  config.data_gpio_nums[13] = 12;
  config.data_gpio_nums[14] = 13;
  config.data_gpio_nums[15] = 14;
  config.flags.disp_active_low = 1;
  config.flags.refresh_on_demand = 0;
  config.flags.fb_in_psram = 1;
  config.flags.double_fb = 1;
  config.flags.no_fb = 0;
  config.flags.bb_invalidate_cache = 1;
  

  esp_err_t error = esp_lcd_new_rgb_panel(&config, &rgbPanel);
  if (error != ESP_OK) {
    Serial.printf("LCD: esp_lcd_new_rgb_panel blad 0x%x.\n", static_cast<unsigned>(error));
    return false;
  }

  rgbColorTransferDoneSemaphore = xSemaphoreCreateBinary();
  if (!rgbColorTransferDoneSemaphore) {
    Serial.println("LCD: nie mozna utworzyc semafora transferu.");
    return false;
  }
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_color_trans_done = rgbColorTransferDoneCallback;
  error = esp_lcd_rgb_panel_register_event_callbacks(
      rgbPanel, &callbacks, rgbColorTransferDoneSemaphore);
  if (error != ESP_OK) {
    Serial.printf("LCD: rejestracja VSYNC blad 0x%x.\n", static_cast<unsigned>(error));
    return false;
  }

  error = esp_lcd_panel_reset(rgbPanel);
  if (error == ESP_OK) error = esp_lcd_panel_init(rgbPanel);
  if (error != ESP_OK) {
    Serial.printf("LCD: inicjalizacja panelu blad 0x%x.\n", static_cast<unsigned>(error));
    return false;
  }
  error = esp_lcd_rgb_panel_get_frame_buffer(rgbPanel, 2, &rgbFrameBuffer0, &rgbFrameBuffer1);
  if (error != ESP_OK || !rgbFrameBuffer0 || !rgbFrameBuffer1) {
    Serial.printf("LCD: pobranie dwoch framebufferow blad 0x%x.\n", static_cast<unsigned>(error));
    return false;
  }
  Serial.printf("LCD: esp_lcd PSRAM framebuffer + bounce DMA OK (%p), VSYNC aktywny.\n",
                rgbFrameBuffer0);
  // Diagnostyka RAM: bounce buffery zajmuja deficytowy RAM wewnetrzny. Log pozwala
  // sprawdzic zapas po inicjalizacji panelu (istotne po zmianie bounce_buffer_size_px).
  Serial.printf("LCD: wolny RAM wewnetrzny po init panelu: %u KB.\n",
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
  return true;
}

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixelMap) {
  // DIRECT render mode: LVGL renderuje calą klatkę do niewidocznego framebuffera,
  // ktorego adres przekazuje w pixelMap. draw_bitmap na pelnym obszarze przelacza
  // framebuffery po stronie esp_lcd (bez kopiowania i bez tearingu przy krawedzi).
  // Odrysowujemy dopiero na ostatnim wywolaniu flush danej klatki.
  if (lv_display_flush_is_last(display)) {
    const esp_err_t error = esp_lcd_panel_draw_bitmap(
        rgbPanel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, pixelMap);
    if (error != ESP_OK) {
      Serial.printf("LCD: draw_bitmap blad 0x%x.\n", static_cast<unsigned>(error));
    }
  }
  lv_display_flush_ready(display);
}

// --- Profilaktyka dryfu obrazu (panel RGB) ---------------------------------------
// Panel RGB moze stracic synchronizacje DMA przy chwilowym niedoborze pasma
// (PSRAM/Flash wspoldzielone z Wi-Fi/LittleFS): kontroler LCD zaczyna czytac piksele
// z przesunietego adresu i caly obraz przesuwa sie pionowo "jak na rolce".
// esp_lcd_rgb_panel_restart() (tylko ESP32-S3) NIE restartuje natychmiast — ustawia
// flage, a wlasciwy restart DMA nastepuje przy NASTEPNYM VSYNC, wiec jest bezpieczny
// (bez migotania/blokowania). Wolamy go cyklicznie jako profilaktyke — to programowy
// odpowiednik CONFIG_LCD_RGB_RESTART_IN_VSYNC, ktorego nie ustawimy w Arduino IDE.
// Natychmiast zglasza restart DMA panelu (tania operacja — ustawia tylko flage,
// wlasciwy restart nastapi przy najblizszym VSYNC). Wolane punktowo po operacjach
// szczególnie obciazajacych magistrale (np. zapis do LittleFS/Flash).
void requestRgbResync() {
  if (!rgbPanel) return;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  esp_lcd_rgb_panel_restart(rgbPanel);
#else
#warning "RGB resync (naprawa dryfu obrazu) wylaczony: to nie jest target ESP32-S3."
#endif
}

// Cykliczna profilaktyka: zglasza restart co RGB_RESYNC_INTERVAL_MS.
void resyncRgbPanelIfDue() {
  static uint32_t lastRestartMs = 0;
  if (millis() - lastRestartMs < RGB_RESYNC_INTERVAL_MS) return;
  lastRestartMs = millis();
  requestRgbResync();
}

void touchRead(lv_indev_t *indev, lv_indev_data_t *data) {
  static int16_t lastX = 0;
  static int16_t lastY = 0;
  // Po wyjsciu z wygaszacza tlumimy WSZYSTKIE odczyty az do puszczenia palca, aby
  // to samo dotkniecie (odczytywane teraz kilka razy na iteracje) nie "przecieklo"
  // jako klikniecie w widok pod spodem. Zdejmujemy blokade na release albo po
  // uplywie SUPPRESS_MAX_MS (bezpiecznik: gdyby kontroler nigdy nie zglosil braku
  // dotyku, ekran nie moze zostac na stale zablokowany).
  static bool suppressUntilRelease = false;
  static uint32_t suppressStartMs = 0;
  constexpr uint32_t SUPPRESS_MAX_MS = 2000;
  if (suppressUntilRelease && millis() - suppressStartMs > SUPPRESS_MAX_MS) {
    suppressUntilRelease = false;
  }

  if (!touchReady) {
    suppressUntilRelease = false;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = lastX;
    data->point.y = lastY;
    return;
  }

  const uint8_t touched = GT911.getPoint(gt911X, gt911Y, GT911.getSupportTouchPoint());
  if (touched > 0) {
    // Pierwszy dotyk przywraca jasność i równocześnie pozostaje normalnym kliknięciem LVGL.
    registerUserActivity();
    // Dotyk w trybie wygaszacza tylko przywraca standardowy widok (bez klikania pod spodem).
    if (screensaverActive && homeScreen && lv_screen_active() == homeScreen) {
      exitScreensaver();
      suppressUntilRelease = true; // nie przetwarzaj tego dotkniecia jako kliku
      suppressStartMs = millis();
      data->state = LV_INDEV_STATE_RELEASED;
      data->point.x = lastX;
      data->point.y = lastY;
      return;
    }
    // Trwajace dotkniecie po wyjsciu z wygaszacza: raportuj RELEASED do puszczenia.
    if (suppressUntilRelease) {
      data->state = LV_INDEV_STATE_RELEASED;
      data->point.x = lastX;
      data->point.y = lastY;
      return;
    }
    // Ekran jest używany w rotacji 0, identycznie jak w oficjalnym przykładzie.
    lastX = constrain(gt911X[0], 0, static_cast<int16_t>(SCREEN_WIDTH - 1));
    lastY = constrain(gt911Y[0], 0, static_cast<int16_t>(SCREEN_HEIGHT - 1));
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    suppressUntilRelease = false; // palec puszczony — kolejne dotkniecia dzialaja normalnie
    data->state = LV_INDEV_STATE_RELEASED;
  }

  data->point.x = lastX;
  data->point.y = lastY;
}

// ---------------------------- Wi-Fi i aktualny czas -----------------------------
const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "oczekiwanie";
    case WL_NO_SSID_AVAIL: return "nie znaleziono sieci (SSID)";
    case WL_SCAN_COMPLETED: return "skan zakonczony";
    case WL_CONNECTED: return "polaczono";
    case WL_CONNECT_FAILED: return "blad uwierzytelnienia";
    case WL_CONNECTION_LOST: return "utracono polaczenie";
    case WL_DISCONNECTED: return "rozlaczono";
    default: return "nieznany";
  }
}

void connectWiFi() {
  // Minimalna, sprawdzona inicjalizacja stacji. Dane pozostają wyłącznie w secrets.h.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  Serial.printf("RAM wewnetrzny wolny przed Wi-Fi: %u B\n",
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  Serial.printf("Wi-Fi: laczenie z siecia %s...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi: polaczono, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("Wi-Fi: nie polaczono: %s (kod %d).\n", wifiStatusName(WiFi.status()), static_cast<int>(WiFi.status()));
  }
  Serial.printf("RAM wewnetrzny wolny po Wi-Fi: %u B\n",
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void retryWiFiConnection() {
  // reconnect() restartuje próbę dla wcześniej przekazanej konfiguracji, bez nowego ustawiania SSID.
  // Gdy sterownik odmawia (np. po WL_NO_SSID_AVAIL), wykonywany jest pełny restart próby.
  Serial.printf("Wi-Fi: ponawianie — %s (kod %d).\n", wifiStatusName(WiFi.status()), static_cast<int>(WiFi.status()));
  if (!WiFi.reconnect()) {
    Serial.println("Wi-Fi: reconnect odrzucony — pelny restart proby (disconnect + begin).");
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// Uruchamia klienta SNTP (nieblokujaco). SNTP dziala w tle i sam dokona
// synchronizacji, gdy tylko serwer odpowie — nie musimy na to czekac w petli.
bool ntpConfigured = false;
void beginNtp() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTzTime(TIMEZONE_RULE, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  ntpConfigured = true;
}

// Czeka na wazny czas maksymalnie timeoutMs (krotko przy starcie). Zwraca true,
// gdy czas jest juz poprawny. Nieudane oczekiwanie nie jest bledem — SNTP
// dokonczy synchronizacje w tle, a loop() wykryje wazny czas pozniej.
bool waitForNtp(uint32_t timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!ntpConfigured) beginNtp();
  struct tm timeInfo;
  const uint32_t start = millis();
  do {
    if (getLocalTime(&timeInfo, 100) && timeInfo.tm_year + 1900 >= 2025) return true;
    delay(100);
  } while (millis() - start < timeoutMs);
  return false;
}

// Zgodnosc wsteczna: pelna proba (uzywana poza startem). Krotsza niz dawne 10 s.
bool syncTimeFromNTP() {
  beginNtp();
  return waitForNtp(3000);
}

bool currentLocalTime(struct tm &timeInfo) {
  if (!getLocalTime(&timeInfo, 10)) return false;
  return timeInfo.tm_year + 1900 >= 2025;
}

// -------------------- Wewnętrzna pamięć Flash: LittleFS ------------------------
bool initialiseStorage() {
  // Priorytet: działająca pamięć. Najpierw montaż bez formatu (ochrona historii);
  // przy uszkodzeniu (np. -84 corrupted dir pair) naprawa przez format, bo bez niej
  // urządzenie na stałe traci możliwość zapisu.
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS: blad montowania — uszkodzony system plikow.");
    Serial.println("LittleFS: naprawiam przez format. Historia z tego wolumenu jest nieodczytywalna.");
    delay(3000); // szansa na odczyt komunikatu przed wymazaniem
    if (!LittleFS.begin(true)) {
      Serial.println("LittleFS: format i ponowne montowanie nieudane. Sprawdz Partition Scheme.");
      return false;
    }
    Serial.println("LittleFS: po formacie pamiec dziala. Plik danych zostanie utworzony od nowa.");
  } else {
    Serial.println("LittleFS: zamontowano poprawnie bez formatowania.");
  }

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) {
    file = LittleFS.open(DATA_FILE_PATH, FILE_WRITE);
    if (!file) return false;
    file.println("data,godzina,typ,ml,piers_lewa_min,piers_prawa_min");
    file.close();
  } else {
    // Sygnal, gdy istniejacy plik jest juz duzy (widoczne w diagnostyce od startu).
    dataFileHuge = file.size() >= DATA_FILE_ROTATE_BYTES;
    file.close();
  }
  return true;
}

bool isMilkType(const String &entryType) {
  return entryType == "MLEKO" || entryType == "MLEKO_MATKI" || entryType == "MLEKO_MODYFIKOWANE";
}

String milkTypeLabel(const String &entryType) {
  if (entryType == "MLEKO_MATKI") return "MLEKO MATKI";
  if (entryType == "MLEKO_MODYFIKOWANE") return "MLEKO MODYFIKOWANE";
  return "MLEKO";
}

String compactHomeEntry(const String &entry, bool bottle) {
  if (entry.startsWith("Brak")) return "Brak wpisu";
  const int firstLineEnd = entry.indexOf('\n');
  String firstLine = firstLineEnd >= 0 ? entry.substring(0, firstLineEnd) : entry;
  const int timeSeparator = firstLine.lastIndexOf("  ");
  String timePart = timeSeparator >= 0 ? firstLine.substring(timeSeparator + 2) : firstLine;
  String detail = firstLineEnd >= 0 ? entry.substring(firstLineEnd + 1) : "";
  if (!bottle) return timePart + "\nZapisano";
  detail.replace("Mleko ", "");
  detail.replace("MLEKO ", "");
  detail.replace(" | ", " ");
  if (detail.length() > 16) detail = detail.substring(0, 16);
  return timePart + "\n" + detail;
}

String formatEntryForUi(const String &csvLine) {
  // Oczekiwany zapis: RRRR-MM-DD,GG:MM,TYP,ML
  const int first = csvLine.indexOf(',');
  const int second = csvLine.indexOf(',', first + 1);
  const int third = csvLine.indexOf(',', second + 1);
  if (first < 0 || second < 0 || third < 0) return "Nie można odczytać wpisu";

  const String dateIso = csvLine.substring(0, first);
  const String timePart = csvLine.substring(first + 1, second);
  const String type = csvLine.substring(second + 1, third);
  const String amount = csvLine.substring(third + 1);

  if (dateIso.length() != 10) return csvLine;
  const String datePl = dateIso.substring(8, 10) + "." + dateIso.substring(5, 7) + "." + dateIso.substring(0, 4);
  if (type == "KARMIENIE" && amount.toInt() == 0) return datePl + "  " + timePart + "\nKARMIENIE";
  return datePl + "  " + timePart + "\n" + (isMilkType(type) ? milkTypeLabel(type) + " | " : String()) + amount + " ml";
}

// ------------------------- Wspólny parser wiersza CSV ----------------------------
// Oczekiwany zapis: RRRR-MM-DD,GG:MM,TYP,ML. Struktura CsvEntry jest zadeklarowana
// na górze pliku, przed miejscem wstawienia automatycznych prototypów Arduino.
bool parseCsvLine(const String &line, CsvEntry &entry) {
  const int first = line.indexOf(',');
  const int second = line.indexOf(',', first + 1);
  const int third = line.indexOf(',', second + 1);
  if (first < 0 || second < 0 || third < 0) return false;
  entry.date = line.substring(0, first);
  entry.time = line.substring(first + 1, second);
  entry.type = line.substring(second + 1, third);
  entry.ml = line.substring(third + 1).toInt();
  // Kolumny 5 i 6 są opcjonalne (starsze wpisy mają 4 pola, minuty = 0).
  entry.piersLeft = 0;
  entry.piersRight = 0;
  const int fourth = line.indexOf(',', third + 1);
  if (fourth < 0) return true;
  const int fifth = line.indexOf(',', fourth + 1);
  entry.piersLeft = line.substring(fourth + 1, fifth < 0 ? line.length() : fifth).toInt();
  if (fifth < 0) return true;
  entry.piersRight = line.substring(fifth + 1).toInt();
  return true;
}

// "RRRR-MM-DD","GG:MM" -> czas lokalny (mktime z isdst=-1)
time_t csvDateTimeToEpoch(const String &date, const String &timeStr) {
  if (date.length() != 10 || timeStr.length() < 5) return 0;
  struct tm t = {};
  t.tm_year = date.substring(0, 4).toInt() - 1900;
  t.tm_mon = date.substring(5, 7).toInt() - 1;
  t.tm_mday = date.substring(8, 10).toInt();
  t.tm_hour = timeStr.substring(0, 2).toInt();
  t.tm_min = timeStr.substring(3, 5).toInt();
  t.tm_isdst = -1;
  return mktime(&t);
}

void loadLatestEntries() {
  lastFeeding = "Brak zapisanego wpisu";
  lastMilk = "Brak zapisanego wpisu";
  lastFeedingTime = 0;
  lastMilkTime = 0;
  lastWeightG = 0;
  sleepInProgress = false;
  sleepStartedTime = 0;
  lastWakeTime = 0;
  // Statystyki rytmu dnia liczymy w TYM SAMYM przebiegu pliku (dawniej osobny skan
  // przez recomputeFeedingRhythm). Jeden odczyt pliku zamiast dwoch.
  avgFeedingGapMin = 0;
  longestFeedingGapMin = 0;
  todayFeedingCount = 0;
  nextFeedingEta = 0;
  if (!storageReady) return;

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) return;

  const String today = dateIso(dayOffsetFromToday(0));
  time_t prevFeedingToday = 0;
  long sumGapMin = 0;
  int gapCount = 0;
  bool sawSleepStart = false; // czy w pliku byl SEN_START przed danym SEN_STOP

  file.readStringUntil('\n'); // pominięcie nagłówka
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;
    const time_t stamp = csvDateTimeToEpoch(entry.date, entry.time);
    if (entry.type == "KARMIENIE") {
      lastFeeding = formatEntryForUi(line);
      lastFeedingTime = stamp;
      // Rytm dnia: tylko wpisy KARMIENIE z dzisiejsza data (plik jest append-only
      // => kolejnosc chronologiczna, wiec przerwy liczymy w locie).
      if (entry.date == today) {
        ++todayFeedingCount;
        if (prevFeedingToday > 0) {
          const long gap = static_cast<long>(difftime(stamp, prevFeedingToday) / 60);
          if (gap > 0) {
            sumGapMin += gap;
            ++gapCount;
            if (gap > longestFeedingGapMin) longestFeedingGapMin = static_cast<int>(gap);
          }
        }
        prevFeedingToday = stamp;
      }
    }
    if (isMilkType(entry.type)) {
      lastMilk = formatEntryForUi(line);
      lastMilkTime = stamp;
    }
    if (entry.type == "WAGA") {
      lastWeightG = entry.ml; // gramy zapisane w kolumnie ml
    }
    if (entry.type == "SEN_START") {
      sleepInProgress = true;
      sleepStartedTime = stamp;
      sawSleepStart = true;
    } else if (entry.type == "SEN_STOP") {
      sleepInProgress = false;
      sleepStartedTime = 0;
      // lastWakeTime tylko dla SPAROWANEGO STOP (spojnie z bilansem w refreshDayStats):
      // osierocony STOP (np. po rotacji pliku) nie kotwiczy okna czuwania.
      if (sawSleepStart) { lastWakeTime = stamp; sawSleepStart = false; }
    }
  }
  file.close();

  // Podsumowanie rytmu — identyczne jak w recomputeFeedingRhythm, ale bez 2. skanu.
  if (gapCount >= 1) avgFeedingGapMin = static_cast<int>(sumGapMin / gapCount);
  if (lastFeedingTime) nextFeedingEta = lastFeedingTime + static_cast<time_t>(COUNTER_BLINK_MIN) * 60;
}

// Statystyki karmien DZIS (tylko typ KARMIENIE): liczba, najdluzsza i srednia
// przerwa. Nastepne karmienie liczone SZTYWNO jako ostatnie + 4 h (COUNTER_BLINK_MIN).
// Jeden przebieg pliku dnia.
void recomputeFeedingRhythm() {
  avgFeedingGapMin = 0;
  longestFeedingGapMin = 0;
  todayFeedingCount = 0;
  nextFeedingEta = 0;

  // Nastepne karmienie = ostatnie + 4 h (niezaleznie od danych statystycznych).
  if (lastFeedingTime) nextFeedingEta = lastFeedingTime + static_cast<time_t>(COUNTER_BLINK_MIN) * 60;

  if (!storageReady) return;
  const String today = dateIso(dayOffsetFromToday(0));

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) return;
  file.readStringUntil('\n');
  time_t prev = 0;
  long sumMin = 0;
  int gapCount = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (!line.startsWith(today + ",")) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;
    if (entry.type != "KARMIENIE") continue;
    ++todayFeedingCount;
    const time_t stamp = csvDateTimeToEpoch(entry.date, entry.time);
    if (prev > 0) {
      const long gap = static_cast<long>(difftime(stamp, prev) / 60);
      if (gap > 0) {
        sumMin += gap;
        ++gapCount;
        if (gap > longestFeedingGapMin) longestFeedingGapMin = static_cast<int>(gap);
      }
    }
    prev = stamp;
  }
  file.close();

  if (gapCount >= 1) avgFeedingGapMin = static_cast<int>(sumMin / gapCount);
}

// ------------------------------- Sen (Napper) -----------------------------------
// Interpolacja liniowa po tabeli progow (xs rosnace). Ponizej 1. progu -> ys[0],
// powyzej ostatniego -> ys[n-1]. Zwraca wartosc calkowita (minuty/sztuki).
int interpTable(long x, const int *xs, const int *ys, int n) {
  if (n <= 0) return 0;
  if (x <= xs[0]) return ys[0];
  if (x >= xs[n - 1]) return ys[n - 1];
  for (int i = 1; i < n; ++i) {
    if (x <= xs[i]) {
      const long x0 = xs[i - 1], x1 = xs[i];
      const long y0 = ys[i - 1], y1 = ys[i];
      if (x1 == x0) return static_cast<int>(y0);
      return static_cast<int>(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
    }
  }
  return ys[n - 1];
}

// Okno czuwania (min-max, w minutach) dla wieku w dniach. Wiek < 0 => brak czasu -> zwroc newborn.
WakeWindow wakeWindowMinutes(long ageDays) {
  if (ageDays < 0) ageDays = 0;
  WakeWindow w;
  w.minMin = interpTable(ageDays, WAKE_WIN_AGE_DAYS, WAKE_WIN_MIN_MINUTES, WAKE_WIN_COUNT);
  w.maxMin = interpTable(ageDays, WAKE_WIN_AGE_DAYS, WAKE_WIN_MAX_MINUTES, WAKE_WIN_COUNT);
  if (w.maxMin < w.minMin) w.maxMin = w.minMin;
  return w;
}

// Zapotrzebowanie na sen (minuty noc/dzien) dla wieku w dniach.
void sleepNeedMinutes(long ageDays, int &nightOut, int &dayOut) {
  if (ageDays < 0) ageDays = 0;
  nightOut = interpTable(ageDays, SLEEP_NEED_AGE_DAYS, SLEEP_NEED_NIGHT_MIN, SLEEP_NEED_COUNT);
  dayOut = interpTable(ageDays, SLEEP_NEED_AGE_DAYS, SLEEP_NEED_DAY_MIN, SLEEP_NEED_COUNT);
}

// Orientacyjna liczba drzemek dziennych wg wieku.
int napTargetCount(long ageDays) {
  if (ageDays < 0) ageDays = 0;
  return interpTable(ageDays, NAP_TARGET_AGE_DAYS, NAP_TARGET_NAPS, NAP_TARGET_COUNT);
}

// Czy dana godzina nalezy do "nocy" snu (NIGHT_START..24 lub 0..NIGHT_END).
static bool sleepHourIsNight(int hour) {
  return hour >= SLEEP_NIGHT_START_HOUR || hour < SLEEP_NIGHT_END_HOUR;
}

// Powiadomienia Telegram o oknie snu — raz na okno czuwania. Wolane cyklicznie z loop().
// Wysyla: przy wejsciu w okno drzemki (czuwanie >= wakeWindow.min) oraz przy jego
// przekroczeniu (>= wakeWindow.max). Flagi resetuja sie, gdy zmieni sie lastWakeTime
// (nowe przebudzenie) albo gdy dziecko spi.
void checkSleepNotifications() {
  if (!sleepTelegramEnabled || !timeIsValid) return;
  // Reset flag przy nowym oknie czuwania lub podczas snu.
  if (sleepInProgress || lastWakeTime != sleepNotifyAnchor) {
    sleepNotifyAnchor = lastWakeTime;
    sleepNotifiedWindow = false;
    sleepNotifiedOver = false;
  }
  if (sleepInProgress || lastWakeTime <= 0) return;

  const WakeWindow ww = wakeWindowMinutes(calculateAgeDays());
  const time_t nowS = time(nullptr);
  const time_t napStart = lastWakeTime + static_cast<time_t>(ww.minMin) * 60;
  const time_t napEnd = lastWakeTime + static_cast<time_t>(ww.maxMin) * 60;

  // Ochrona przed falszywym alarmem po restarcie: lastWakeTime bywa stary (z historii).
  // Nie powiadamiamy o oknie, ktore zamknelo sie dawno (> 30 min po napEnd) — traktujemy
  // je jako "juz nieaktualne" (dziecko zapewne dawno spi/nie dotyczy). Oznaczamy flagi
  // jako wyslane, aby nie retro-strzelic po pierwszej synchronizacji NTP.
  constexpr long STALE_MARGIN_SEC = 30 * 60;
  if (nowS > napEnd + STALE_MARGIN_SEC) {
    sleepNotifiedWindow = true;
    sleepNotifiedOver = true;
    return;
  }

  if (!sleepNotifiedOver && nowS > napEnd) {
    sleepNotifiedOver = true;
    sleepNotifiedWindow = true; // przekroczenie implikuje, ze okno juz bylo
    queueTelegram("Aleksander: przekroczone okno czuwania — mozliwe przemeczenie. Warto uspic.");
  } else if (!sleepNotifiedWindow && nowS >= napStart) {
    sleepNotifiedWindow = true;
    queueTelegram(String("Aleksander: okno drzemki (~") +
                  formatDateTime(napStart).substring(12, 17) + "-" +
                  formatDateTime(napEnd).substring(12, 17) + "). Dobry moment na sen.");
  }
}

// Krotki format przerwy "Xh Ymin" / "Ymin".
String formatGapShort(int minutes) {
  if (minutes <= 0) return String("-");
  const int h = minutes / 60, m = minutes % 60;
  if (h == 0) return String(m) + "min";
  return String(h) + "h " + m + "min";
}

// Godzina nastepnego karmienia jako "HH:MM" (ostatnie + 4h). Pusty gdy brak danych.
String nextFeedingClock() {
  if (!nextFeedingEta) return String();
  struct tm t;
  localtime_r(&nextFeedingEta, &t);
  char hhmm[6];
  strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
  return String(hhmm);
}

// --------------------------- Cofanie ostatniego wpisu ----------------------------
String describeCsvEntry(const CsvEntry &e) {
  const String shortDate = e.date.substring(8, 10) + "." + e.date.substring(5, 7) + ". ";
  String t = shortDate + e.time + " ";
  if (isMilkType(e.type)) {
    t += milkTypeLabel(e.type) + " " + String(e.ml) + " ml";
  } else if (e.type == "KARMIENIE") {
    t += "KARMIENIE";
    if (e.piersLeft > 0 || e.piersRight > 0) t += " L" + String(e.piersLeft) + "/P" + String(e.piersRight);
  } else {
    t += e.type;
  }
  return t;
}

bool deleteEntryByIndex(int entryIndex, String &removedDescription) {
  removedDescription = "";
  if (!storageReady || entryIndex < 0) return false;

  // Przetwarzanie STRUMIENIOWE wiersz po wierszu: nie trzymamy calego pliku w RAM.
  // Jeden przebieg zrodla -> zapis do pliku tymczasowego z pominieciem wybranego
  // wiersza. Dzieki temu operacja dziala niezaleznie od rozmiaru historii i nie
  // tworzy duzego, fragmentujacego bloku w RAM wewnetrznym (dawniej caly plik + substringi).
  File src = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!src) return false;

  File dst = LittleFS.open("/karmienia.tmp", FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }
  dst.println("data,godzina,typ,ml,piers_lewa_min,piers_prawa_min");

  // WAZNE: lineIndex z panelu WWW (handleApiEntries) to fizyczna pozycja wiersza po
  // naglowku, liczona dla KAZDEJ linii — takze pustej i nieparsowalnej. Musimy liczyc
  // tak samo, inaczej usuniemy niewlasciwy wpis. Zachowujemy wiersze bez zmian
  // (bez trim), pomijamy wylacznie ten o pasujacym indeksie.
  src.readStringUntil('\n'); // pomijamy naglowek zrodla
  int dataIndex = 0;
  bool removedFound = false;
  while (src.available()) {
    String line = src.readStringUntil('\n');
    // Usuwamy tylko koncowy CR/LF, bez naruszania tresci wiersza.
    while (line.length() && (line[line.length() - 1] == '\r' || line[line.length() - 1] == '\n')) {
      line.remove(line.length() - 1);
    }
    if (dataIndex == entryIndex) {
      String trimmed = line;
      trimmed.trim();
      CsvEntry removed;
      if (parseCsvLine(trimmed, removed)) removedDescription = describeCsvEntry(removed);
      removedFound = true;
      // pomijamy ten wiersz w zapisie
    } else {
      dst.println(line);
    }
    ++dataIndex;
  }
  src.close();
  dst.close();

  if (!removedFound || entryIndex >= dataIndex) {
    LittleFS.remove("/karmienia.tmp");
    return false;
  }

  if (!LittleFS.rename("/karmienia.tmp", DATA_FILE_PATH)) {
    LittleFS.remove("/karmienia.tmp");
    return false;
  }

  invalidateDayStats();
  loadLatestEntries();
  return true;
}

// Miekka rotacja: gdy plik danych przekroczy prog, tworzymy jednorazowo kopie
// archiwalna ze znacznikiem daty i podnosimy flage ostrzegawcza. DANYCH NIE
// USUWAMY — na tej platformie jest ~11 MB miejsca, wiec chodzi wylacznie o sygnal,
// ze historia urosla i skany CSV staja sie kosztowne (uzytkownik moze wyeksportowac
// i zaimportowac skrocony plik). Kopia sluzy tez jako dodatkowy backup.
void archiveDataFileIfHuge() {
  if (!storageReady || dataFileHuge) return;
  File probe = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!probe) return;
  const size_t sizeBytes = probe.size();
  probe.close();
  if (sizeBytes < DATA_FILE_ROTATE_BYTES) return;

  // Nazwa archiwum ze znacznikiem daty (bez usuwania oryginalu).
  char archivePath[40];
  struct tm nowInfo;
  if (currentLocalTime(nowInfo)) {
    strftime(archivePath, sizeof(archivePath), "/karmienia_arch_%Y-%m-%d.csv", &nowInfo);
  } else {
    snprintf(archivePath, sizeof(archivePath), "/karmienia_arch.csv");
  }
  const bool copied = copyLittleFsFile(DATA_FILE_PATH, archivePath);
  dataFileHuge = true; // ostrzezenie widoczne w diagnostyce; nie powtarzamy w tej sesji
  Serial.printf("Dane: plik osiagnal %u KB (prog %u KB). %s\n",
                static_cast<unsigned>(sizeBytes / 1024),
                static_cast<unsigned>(DATA_FILE_ROTATE_BYTES / 1024),
                copied ? "Utworzono kopie archiwalna." : "Nie udalo sie utworzyc kopii archiwalnej.");
}

bool appendEntry(const char *entryType, time_t when, int ml, int piersLeft, int piersRight) {
  if (!storageReady) return false;

  struct tm entryTime;
  localtime_r(&when, &entryTime);

  char datePart[11];
  char timePart[6];
  strftime(datePart, sizeof(datePart), "%Y-%m-%d", &entryTime);
  strftime(timePart, sizeof(timePart), "%H:%M", &entryTime);

  File file = LittleFS.open(DATA_FILE_PATH, FILE_APPEND);
  if (!file) return false;

  // Budujemy caly wiersz, a potem sprawdzamy, czy zapisano DOKLADNIE tyle bajtow
  // (zapis czesciowy przy zapelnionej pamieci NIE moze uchodzic za sukces).
  char row[96];
  int rowLen;
  if (piersLeft >= 0 || piersRight >= 0) {
    // Nowy format z minutami piersi (wartość -1 oznacza: użyj zera).
    rowLen = snprintf(row, sizeof(row), "%s,%s,%s,%d,%d,%d\n", datePart, timePart, entryType, ml,
                      max(piersLeft, 0), max(piersRight, 0));
  } else {
    // Wywołania bez minut (mleko, starsze ścieżki) zostawiają 4 kolumny.
    rowLen = snprintf(row, sizeof(row), "%s,%s,%s,%d\n", datePart, timePart, entryType, ml);
  }
  bool saved = false;
  if (rowLen > 0 && rowLen < static_cast<int>(sizeof(row))) {
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(row), static_cast<size_t>(rowLen));
    saved = (written == static_cast<size_t>(rowLen));
    if (!saved) Serial.printf("Zapis: niepelny wiersz (%u z %d B) — pamiec pelna?\n",
                              static_cast<unsigned>(written), rowLen);
  } else {
    Serial.println("Zapis: wiersz przekroczyl bufor — pominieto.");
  }
  file.flush();
  file.close();
  // Zapis do LittleFS chwilowo obciaza magistrale Flash/PSRAM — zglaszamy restart
  // DMA panelu (przy najblizszym VSYNC), by zapobiec ewentualnemu dryfowi obrazu.
  requestRgbResync();

  if (saved) {
    const String displayEntry = String(datePart).substring(8, 10) + "." + String(datePart).substring(5, 7) + "." + String(datePart).substring(0, 4) +
                                "  " + timePart + "\n" + (String(entryType) == "KARMIENIE" && ml == 0 ? "KARMIENIE" : String(ml) + " ml");
    if (String(entryType) == "KARMIENIE") {
      lastFeeding = displayEntry;
      lastFeedingTime = when;
    } else if (isMilkType(String(entryType))) {
      lastMilk = formatEntryForUi(String(datePart) + "," + timePart + "," + entryType + "," + String(ml));
      lastMilkTime = when;
    }
  }
  if (saved) {
    invalidateDayStats();
    appendBackupIfDue();
    archiveDataFileIfHuge(); // miekka rotacja: kopia + ostrzezenie przy duzym pliku
    queueTelegram(telegramTextFor(String(entryType), ml, piersLeft, piersRight, when));
  }
  return saved;
}

// ---------------------------- Odczyt danych kalendarza ---------------------------
time_t beginningOfDay(time_t value) {
  struct tm dayInfo;
  localtime_r(&value, &dayInfo);
  dayInfo.tm_hour = 0;
  dayInfo.tm_min = 0;
  dayInfo.tm_sec = 0;
  dayInfo.tm_isdst = -1;
  return mktime(&dayInfo);
}

String dateIso(time_t value) {
  struct tm dateInfo;
  localtime_r(&value, &dateInfo);
  char buffer[11];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &dateInfo);
  return String(buffer);
}

time_t dayOffsetFromToday(uint8_t daysBack) {
  const time_t now = time(nullptr);
  struct tm dayInfo;
  localtime_r(&now, &dayInfo);
  dayInfo.tm_mday -= daysBack;
  dayInfo.tm_hour = 12;
  dayInfo.tm_min = 0;
  dayInfo.tm_sec = 0;
  dayInfo.tm_isdst = -1;
  return beginningOfDay(mktime(&dayInfo));
}

String calendarDayTitle(time_t value, uint8_t index) {
  struct tm dateInfo;
  localtime_r(&value, &dateInfo);
  char dateBuffer[24];
  strftime(dateBuffer, sizeof(dateBuffer), "%d.%m.%Y", &dateInfo);
  if (index == 0) return String("DZISIAJ - ") + dateBuffer;
  if (index == 1) return String("WCZORAJ - ") + dateBuffer;
  if (index == 2) return String("2 DNI TEMU - ") + dateBuffer;
  return String(index) + " DNI TEMU - " + dateBuffer;
}

String entriesForDay(time_t day, bool compact) {
  if (!storageReady) return "Pamiec niedostepna";

  // Podsumowanie pochodzi z cache statystyk (jeden przebieg pliku dla wszystkich widoków).
  DaySummary s;
  dayStats(day, s);

  // Tryb compact (kalendarz) zwraca samo podsumowanie z cache — NIE skanujemy pliku.
  // (Wczesniej plik byl otwierany i skanowany, a wynik i tak odrzucany przez early return.)
  if (compact) {
    const String summaryOnly = formatDaySummaryLine(s) + "\n" + formatDayExtraLine(s);
    if (s.feedingCount == 0 && s.milkCount == 0) return summaryOnly + "\nBrak wpisow";
    return summaryOnly;
  }

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) return "Nie mozna otworzyc historii";

  String details;
  const String targetDate = dateIso(day);

  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.startsWith(targetDate + ",")) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;
    String rowText;
    if (isMilkType(entry.type)) {
      rowText = milkTypeLabel(entry.type) + "  " + String(entry.ml) + " ml";
    } else if (entry.type == "KARMIENIE" && (entry.piersLeft > 0 || entry.piersRight > 0)) {
      rowText = "KARMIENIE  L" + String(entry.piersLeft) + "/P" + String(entry.piersRight);
    } else {
      rowText = entry.type;
    }
    details += entry.time + "  " + rowText + "\n";
  }
  file.close();

  const String summary = formatDaySummaryLine(s) + "\n" + formatDayExtraLine(s);
  if (s.feedingCount == 0 && s.milkCount == 0) return summary + "\nBrak wpisow";
  return summary + "\n" + details;
}

// ------------------- Cache statystyk dnia: deklaracje globalne --------------------
// Zadeklarowane przed funkcjami dostępowymi, aby były widoczne w ich ciałach.
constexpr uint8_t STATS_DAY_COUNT = 8; // dziś + 7 dni wstecz
time_t statsDays[STATS_DAY_COUNT] = {};
DaySummary statsData[STATS_DAY_COUNT];
time_t statsAnchorDay = 0;
bool statsValid = false;

int statsIndexForDay(time_t day);

// --------------- Cache statystyk dnia: jeden przebieg pliku CSV ------------------
// Zasila ekran główny (DZISIAJ), kalendarz, wykres oraz API WWW. Po zapisie wpisu
// cache jest unieważniany i odbudowywany przy pierwszym dostępie. Tablice statystyk
// oraz statsIndexForDay() są zadeklarowane wyżej, przed funkcjami dostępowymi.
void invalidateDayStats() { statsValid = false; }

// Dolicza liczbe drzemek: SEN_START rozpoczety w porze DZIENNEJ liczymy jako drzemke
// tego dnia (sen nocny nie jest drzemka). Przypisanie do dnia wg daty startu.
void accrueNapCount(time_t start, const String iso[]) {
  if (start <= 0) return;
  struct tm t;
  localtime_r(&start, &t);
  if (sleepHourIsNight(t.tm_hour)) return; // sen nocny — nie drzemka
  const String startDate = dateIso(beginningOfDay(start));
  for (uint8_t i = 0; i < STATS_DAY_COUNT; ++i) {
    if (startDate == iso[i]) { ++statsData[i].napCount; return; }
  }
}

// Rozdziela sen [start,stop] na minuty nocne/dzienne i przypisuje do wlasciwych dni
// w oknie statystyk. Iterujemy krokami do najblizszej granicy godzinowej noc/dzien,
// aby dokladnie policzyc podzial nawet dla snu przez polnoc/wielogodzinnego.
void accrueSleepInterval(time_t start, time_t stop, const String iso[]) {
  if (stop <= start) return;
  time_t cur = start;
  int guard = 0;
  while (cur < stop && guard++ < 4000) { // guard: bezpiecznik (max ~ kilka dni w krokach)
    struct tm t;
    localtime_r(&cur, &t);
    const bool night = sleepHourIsNight(t.tm_hour);
    // Wyznacz koniec biezacego jednorodnego segmentu (do zmiany noc<->dzien lub do stop).
    // Nastepna granica: najblizsza pelna godzina rowna NIGHT_START lub NIGHT_END.
    struct tm nb = t; nb.tm_sec = 0; nb.tm_min = 0;
    // krok do najblizszej pelnej godziny
    time_t nextHour = cur + (3600 - (t.tm_min * 60 + t.tm_sec));
    if (t.tm_min == 0 && t.tm_sec == 0) nextHour = cur + 3600;
    time_t segEnd = nextHour < stop ? nextHour : stop;
    const long segMin = static_cast<long>(difftime(segEnd, cur) / 60);
    if (segMin > 0) {
      const String segDate = dateIso(beginningOfDay(cur));
      for (uint8_t i = 0; i < STATS_DAY_COUNT; ++i) {
        if (segDate == iso[i]) {
          if (night) statsData[i].sleepNightMin += static_cast<int>(segMin);
          else statsData[i].sleepDayMin += static_cast<int>(segMin);
          break;
        }
      }
    }
    cur = segEnd;
  }
}

void refreshDayStats() {
  statsAnchorDay = dayOffsetFromToday(0);
  String iso[STATS_DAY_COUNT];
  for (uint8_t i = 0; i < STATS_DAY_COUNT; ++i) {
    statsDays[i] = dayOffsetFromToday(i);
    iso[i] = dateIso(statsDays[i]);
    DaySummary &s = statsData[i];
    s.feedingCount = 0;
    s.milkCount = 0;
    s.milkMl = 0;
    s.motherMilkMl = 0;
    s.modifiedMilkMl = 0;
    s.piersLeftMin = 0;
    s.piersRightMin = 0;
    s.diaperWet = 0;
    s.diaperDirty = 0;
    s.pumpingMl = 0;
    s.vitaminD = false;
    s.weightG = 0;
    s.sleepDayMin = 0;
    s.sleepNightMin = 0;
    s.napCount = 0;
  }
  if (storageReady) {
    File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
    if (file) {
      file.readStringUntil('\n');
      time_t openSleepStart = 0; // otwarty SEN_START (plik jest chronologiczny)
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        CsvEntry entry;
        if (!parseCsvLine(line, entry)) continue;
        // Sen paruje SEN_START->SEN_STOP i moze wykraczac poza okno dni — obslugujemy osobno.
        if (entry.type == "SEN_START") {
          openSleepStart = csvDateTimeToEpoch(entry.date, entry.time);
          accrueNapCount(openSleepStart, iso);
          continue;
        } else if (entry.type == "SEN_STOP") {
          if (openSleepStart > 0) {
            const time_t stop = csvDateTimeToEpoch(entry.date, entry.time);
            accrueSleepInterval(openSleepStart, stop, iso);
            openSleepStart = 0;
          }
          continue;
        }
        for (uint8_t i = 0; i < STATS_DAY_COUNT; ++i) {
          if (entry.date != iso[i]) continue;
          DaySummary &s = statsData[i];
          if (entry.type == "KARMIENIE") {
            ++s.feedingCount;
            s.piersLeftMin += entry.piersLeft;
            s.piersRightMin += entry.piersRight;
          } else if (isMilkType(entry.type)) {
            ++s.milkCount;
            s.milkMl += entry.ml;
            if (entry.type == "MLEKO_MATKI") s.motherMilkMl += entry.ml;
            else if (entry.type == "MLEKO_MODYFIKOWANE") s.modifiedMilkMl += entry.ml;
          } else if (entry.type == "PIELUCHA_MOKRA") {
            ++s.diaperWet;
          } else if (entry.type == "PIELUCHA_BRUDNA") {
            ++s.diaperDirty;
          } else if (entry.type == "ODCIAGANIE") {
            s.pumpingMl += entry.ml;
          } else if (entry.type == "WITAMINA_D") {
            s.vitaminD = true;
          } else if (entry.type == "WAGA") {
            s.weightG = entry.ml; // ostatni wpis danego dnia nadpisuje (kolejnosc chronologiczna)
          }
          break;
        }
      }
      // Sen trwajacy do teraz (brak SEN_STOP): dolicz do biezacej chwili.
      if (openSleepStart > 0) accrueSleepInterval(openSleepStart, time(nullptr), iso);
      file.close();
    }
  }
  statsValid = true;
}

int statsIndexForDay(time_t day) {
  // Zmiana doby unieważnia okno statystyk (przejście przez północ).
  if (!statsValid || dayOffsetFromToday(0) != statsAnchorDay) refreshDayStats();
  const time_t target = beginningOfDay(day);
  for (uint8_t i = 0; i < STATS_DAY_COUNT; ++i) {
    if (statsDays[i] == target) return i;
  }
  return -1;
}

void dayStats(time_t day, DaySummary &out) {
  out.feedingCount = 0;
  out.milkCount = 0;
  out.milkMl = 0;
  out.motherMilkMl = 0;
  out.modifiedMilkMl = 0;
  out.piersLeftMin = 0;
  out.piersRightMin = 0;
  out.diaperWet = 0;
  out.diaperDirty = 0;
  out.pumpingMl = 0;
  out.vitaminD = false;
  out.weightG = 0;
  out.sleepDayMin = 0;
  out.sleepNightMin = 0;
  out.napCount = 0;
  const int index = statsIndexForDay(day);
  if (index < 0) return;
  out = statsData[index];
}

// Jednolity format podsumowania dnia — identyczny na urządzeniu i w panelu WWW.
String formatDaySummaryLine(const DaySummary &s) {
  return String("KARM.: ") + s.feedingCount + " | MLEKO: " + s.milkMl +
         " ml | PIERS: L" + s.piersLeftMin + "/P" + s.piersRightMin;
}

String formatDayExtraLine(const DaySummary &s) {
  return String("PIELUCHY: ") + s.diaperWet + "/" + s.diaperDirty +
         " | ODCIAG.: " + s.pumpingMl + " ml | WIT.D: " + (s.vitaminD ? "TAK" : "BRAK");
}

// "2 godz. 5 min temu" dla licznika od ostatniego karmienia.
String formatAgoText(time_t then) {
  if (!then) return String();
  long delta = static_cast<long>(difftime(time(nullptr), then));
  if (delta < -60) return String("w przyszlosci");
  if (delta < 0) delta = 0;
  const long hours = delta / 3600;
  const long minutes = (delta % 3600) / 60;
  if (hours == 0) return String(minutes) + " min temu";
  return String(hours) + " godz. " + minutes + " min temu";
}

// Progi licznika: zielony < 3 h, zolty 3-4 h, czerwony >= 4 h od karmienia.
lv_color_t feedingAgeColor(time_t then) {
  long delta = static_cast<long>(difftime(time(nullptr), then));
  if (delta < 0) delta = 0;
  const long minutes = delta / 60;
  if (minutes >= 240) return COLOR_RED;
  if (minutes >= 180) return COLOR_YELLOW;
  return COLOR_GREEN;
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 12);
  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value.charAt(i);
    if (character == '\\' || character == '"') {
      escaped += '\\';
      escaped += character;
    } else if (character == '\n') {
      escaped += "\\n";
    } else if (character == '\r') {
      escaped += "\\r";
    } else if (character == '\t') {
      escaped += "\\t";
    } else {
      escaped += character;
    }
  }
  return escaped;
}

String webDateTime(time_t value) {
  struct tm timeInfo;
  localtime_r(&value, &timeInfo);
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", &timeInfo);
  return String(buffer);
}

bool parseWebDateTime(const String &value, time_t &result) {
  if (value.length() != 16 || value.charAt(4) != '-' || value.charAt(7) != '-' || value.charAt(10) != 'T' || value.charAt(13) != ':') return false;
  const uint8_t numberPositions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15};
  for (uint8_t position : numberPositions) {
    const char character = value.charAt(position);
    if (character < '0' || character > '9') return false;
  }

  const int expectedYear = value.substring(0, 4).toInt() - 1900;
  const int expectedMonth = value.substring(5, 7).toInt() - 1;
  const int expectedDay = value.substring(8, 10).toInt();
  const int expectedHour = value.substring(11, 13).toInt();
  const int expectedMinute = value.substring(14, 16).toInt();

  struct tm timeInfo = {};
  timeInfo.tm_year = expectedYear;
  timeInfo.tm_mon = expectedMonth;
  timeInfo.tm_mday = expectedDay;
  timeInfo.tm_hour = expectedHour;
  timeInfo.tm_min = expectedMinute;
  timeInfo.tm_sec = 0;
  timeInfo.tm_isdst = -1;
  result = mktime(&timeInfo);
  if (result < 1735689600) return false;

  struct tm verifiedTime;
  localtime_r(&result, &verifiedTime);
  return verifiedTime.tm_year == expectedYear && verifiedTime.tm_mon == expectedMonth &&
         verifiedTime.tm_mday == expectedDay && verifiedTime.tm_hour == expectedHour &&
         verifiedTime.tm_min == expectedMinute;
}

void sendJson(int statusCode, const String &payload) {
  webServer.sendHeader("Cache-Control", "no-store, max-age=0");
  webServer.send(statusCode, "application/json; charset=utf-8", payload);
}

void handleWebRoot() {
  Serial.println("HTTP: obsluga /");
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
    pos += toRead;
  }
  Serial.println("HTTP: strona wyslana.");
}

void handleApiStatus() {
  const time_t now = time(nullptr);
  String payload;
  // Realny rozmiar to ~1.8-2.2 KB (5 dni kalendarza + status + sysinfo). Rezerwacja
  // 2560 B pokrywa go z zapasem bez wczesniejszego blokowania 6.5 KB przy kazdym
  // pollingu co 10 s. Jedna rezerwacja = brak serii realloc-ow fragmentujacych RAM.
  payload.reserve(3072);
  payload = "{";
  payload += "\"now\":\"" + jsonEscape(timeIsValid ? formatDateTime(now) : "Brak potwierdzonego czasu") + "\",";
  payload += "\"nowIso\":\"" + jsonEscape(webDateTime(now)) + "\",";
  payload += "\"ip\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
  payload += "\"age\":\"" + jsonEscape(calculateAgeText()) + "\",";
  payload += "\"developmentTip\":\"" + jsonEscape(developmentTipForToday()) + "\",";
  payload += "\"developmentDay\":" + String(calculateAgeDays()) + ",";
  payload += "\"lastFeeding\":\"" + jsonEscape(lastFeeding) + "\",";
  payload += "\"lastMilk\":\"" + jsonEscape(lastMilk) + "\",";
  payload += "\"lastFeedingAgo\":\"" + jsonEscape(lastFeedingTime ? formatAgoText(lastFeedingTime) : String()) + "\",";
  payload += "\"lastFeedingAgeMin\":" + String(lastFeedingTime ? static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60) : -1) + ",";
  payload += "\"avgFeedingGapMin\":" + String(avgFeedingGapMin) + ",";
  payload += "\"nextFeedingIso\":\"" + jsonEscape(nextFeedingEta ? webDateTime(nextFeedingEta) : String()) + "\",";
  payload += "\"longestFeedingGapMin\":" + String(longestFeedingGapMin) + ",";
  payload += "\"sleepInProgress\":" + String(sleepInProgress ? "true" : "false") + ",";
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
    payload += "\"sleepState\":\"" + sleepState + "\",";
    payload += "\"sleepSinceMin\":" + String(sleepSinceMin) + ",";
    payload += "\"wakeSinceMin\":" + String(wakeSinceMin) + ",";
    payload += "\"wakeWindowMinMin\":" + String(ww.minMin) + ",";
    payload += "\"wakeWindowMaxMin\":" + String(ww.maxMin) + ",";
    payload += "\"nextNapStartIso\":\"" + jsonEscape(napStart ? webDateTime(napStart) : String()) + "\",";
    payload += "\"nextNapEndIso\":\"" + jsonEscape(napEnd ? webDateTime(napEnd) : String()) + "\",";
    payload += "\"sleepDayMin\":" + String(today.sleepDayMin) + ",";
    payload += "\"sleepNightMin\":" + String(today.sleepNightMin) + ",";
    payload += "\"sleepNeedDayMin\":" + String(needDay) + ",";
    payload += "\"sleepNeedNightMin\":" + String(needNight) + ",";
    payload += "\"napCount\":" + String(today.napCount) + ",";
    payload += "\"napTarget\":" + String(napTargetCount(ageDays)) + ",";
    payload += "\"sleepTelegram\":" + String(sleepTelegramEnabled ? "true" : "false") + ",";
  }
  payload += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  payload += "\"storage\":" + String(storageReady ? "true" : "false") + ",";
  payload += "\"dataFileHuge\":" + String(dataFileHuge ? "true" : "false") + ",";
  payload += "\"timeValid\":" + String(timeIsValid ? "true" : "false") + ",";
  payload += "\"minMl\":" + String(ML_MIN) + ",";
  payload += "\"maxMl\":" + String(ML_MAX) + ",";
  payload += "\"defaultMl\":" + String(DEFAULT_ML) + ",";
  payload += "\"birthWeightG\":" + String(BIRTH_WEIGHT_G) + ",";
  payload += "\"lastWeightG\":" + String(lastWeightG) + ",";
  payload += "\"calendar\":[";
  for (uint8_t i = 0; i < 5; ++i) {
    const time_t day = dayOffsetFromToday(i);
    DaySummary s;
    dayStats(day, s);
    if (i) payload += ',';
    payload += "{\"date\":\"" + dateIso(day) + "\",\"label\":\"" + jsonEscape(calendarDayTitle(day, i)) +
               "\",\"feedingCount\":" + String(s.feedingCount) + ",\"milkMl\":" + String(s.milkMl) +
               ",\"motherMilkMl\":" + String(s.motherMilkMl) + ",\"modifiedMilkMl\":" + String(s.modifiedMilkMl) +
               ",\"piersLeftMin\":" + String(s.piersLeftMin) + ",\"piersRightMin\":" + String(s.piersRightMin) +
               ",\"diaperWet\":" + String(s.diaperWet) + ",\"diaperDirty\":" + String(s.diaperDirty) +
               ",\"pumpingMl\":" + String(s.pumpingMl) + ",\"vitaminD\":" + String(s.vitaminD ? "true" : "false") +
               ",\"weightG\":" + String(s.weightG) +
               "}";
  }
  payload += "],";
  payload += "\"night\":" + String(nightModeActive ? "true" : "false") + ",";
  payload += "\"mdns\":\"karmienie.local\",";
  payload += "\"undoWindowSec\":60,";
  payload += "\"freeHeap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  payload += "\"totalHeap\":" + String(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024) + ",";
  payload += "\"freePsram\":" + String(ESP.getFreePsram() / 1024) + ",";
  payload += "\"totalPsram\":" + String(ESP.getPsramSize() / 1024) + ",";
  payload += "\"maxAlloc\":" + String(ESP.getMaxAllocHeap() / 1024) + ",";
  payload += "\"uptimeSec\":" + String(millis() / 1000) + ",";
  payload += "\"cpuLoad\":" + String(cpuLoadPct) + ",";
  // Diagnostyka (te same dane co ekran DIAGNOSTYKA na urzadzeniu).
  payload += "\"minFreeHeap\":" + String((minFreeHeapEver == 0xFFFFFFFFUL ? 0 : minFreeHeapEver) / 1024) + ",";
  payload += "\"rssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
  payload += "\"httpRequests\":" + String(httpRequestCount) + ",";
  payload += "\"bootCount\":" + String(bootCount) + ",";
  payload += "\"watchdogResets\":" + String(watchdogResetCount) + ",";
  payload += "\"watchdogReady\":" + String(watchdogReady ? "true" : "false") + ",";
  payload += "\"resetReason\":\"" + jsonEscape(resetReasonText(lastResetReason)) + "\"}";
  sendJson(200, payload);
}

void handleApiEntries() {
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
  String payload;
  // Rezerwacja z gory: typowy dzien ma kilkanascie wpisow po ~140 B JSON. 4 KB
  // pokrywa to z zapasem i eliminuje serie realloc-ow fragmentujacych heap wewn.
  payload.reserve(4096);
  payload = "{\"date\":\"" + targetDate + "\",\"entries\":[";
  bool firstEntry = true;
  int dataIndex = 0;
  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) { ++dataIndex; continue; }
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) { ++dataIndex; continue; }
    if (!entry.date.startsWith(targetDate)) { ++dataIndex; continue; }
    if (!firstEntry) payload += ',';
    firstEntry = false;
    payload += "{\"time\":\"" + jsonEscape(entry.time) + "\",\"type\":\"" + jsonEscape(entry.type) + "\",\"label\":\"" + jsonEscape(isMilkType(entry.type) ? milkTypeLabel(entry.type) : entry.type) + "\",\"ml\":" + String(entry.ml) +
               ",\"piersLeftMin\":" + String(entry.piersLeft) + ",\"piersRightMin\":" + String(entry.piersRight) +
               ",\"lineIndex\":" + String(dataIndex) + "}";
    ++dataIndex;
  }
  file.close();
  payload += "]}";
  sendJson(200, payload);
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
  webServer.sendContent("{\"birthWeightG\":");
  webServer.sendContent(String(BIRTH_WEIGHT_G));
  webServer.sendContent(",\"points\":[");
  bool first = true;
  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
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
  }
  file.close();
  webServer.sendContent("]}");
  webServer.sendContent("");
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

  // Zachowuje obsluge starszych, samodzielnych wpisow mleka wysylanych przez poprzednia wersje WWW.
  if (isMilkType(type)) {
    if (ml < ML_MIN || ml > ML_MAX) {
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
  String milkType;
  int milkMl = 0;
  if (extraMilk) {
    if (!webServer.hasArg("milkType") || !webServer.hasArg("milkMl")) {
      sendJson(400, "{\"message\":\"Brakuje typu lub ilosci dodatkowego mleka.\"}");
      return;
    }
    milkType = webServer.arg("milkType");
    milkMl = webServer.arg("milkMl").toInt();
    if ((milkType != "MLEKO_MATKI" && milkType != "MLEKO_MODYFIKOWANE") || milkMl < ML_MIN || milkMl > ML_MAX) {
      sendJson(400, "{\"message\":\"Nieprawidlowe dodatkowe mleko.\"}");
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
  if (extraMilk && !appendEntry(milkType.c_str(), when, milkMl)) {
    sendJson(500, "{\"message\":\"Karmienie zapisano, ale nie udalo sie zapisac dodatkowego mleka.\"}");
    return;
  }

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
  sendJson(200, "{\"message\":\"Usunieto wpis.\",\"removed\":\"" + jsonEscape(removed) + "\"}");
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
                         type == "SEN_START" || type == "SEN_STOP";
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
  uint8_t buffer[512];
  while (file.available()) {
    const size_t readBytes = file.read(buffer, sizeof(buffer));
    if (readBytes == 0) break;
    webServer.client().write(buffer, readBytes);
  }
  file.close();
}

// Kopiowanie plikow w obrębie LittleFS (uzywane przez backup i import).
bool copyLittleFsFile(const char *srcPath, const char *dstPath) {
  File src = LittleFS.open(srcPath, FILE_READ);
  if (!src) return false;
  File dst = LittleFS.open(dstPath, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }
  uint8_t buffer[512];
  while (true) {
    const int readBytes = src.read(buffer, sizeof(buffer));
    if (readBytes <= 0) break;
    dst.write(buffer, readBytes);
  }
  src.close();
  dst.close();
  return true;
}

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

// Tworzy osobną kartę dla każdego wiersza danego dnia; kontener można przewijać palcem.
void populateDayEntries(lv_obj_t *container, time_t day) {
  if (!storageReady) {
    createLabel(container, "Pamiec niedostepna", COLOR_RED, LV_ALIGN_TOP_MID, 0, 12);
    return;
  }

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) {
    createLabel(container, "Nie mozna otworzyc historii", COLOR_RED, LV_ALIGN_TOP_MID, 0, 12);
    return;
  }

  const String targetDate = dateIso(day);
  int rowY = 0;
  int recordCount = 0;
  file.readStringUntil('\n');

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.startsWith(targetDate + ",")) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;

    const String timePart = entry.time;
    const String type = entry.type;
    const String typeDisplay = isMilkType(type) ? milkTypeLabel(type) : type;
    const lv_color_t accent = isMilkType(type) ? COLOR_BLUE : COLOR_ORANGE;

    lv_obj_t *row = createCard(container, 0, rowY, 416, 56);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_mix(COLOR_CARD, accent, 26), 0);

    lv_obj_t *timeLabel = lv_label_create(row);
    lv_label_set_text(timeLabel, timePart.c_str());
    lv_obj_set_style_text_color(timeLabel, COLOR_TEXT, 0);
    lv_obj_set_pos(timeLabel, 8, 18);

    lv_obj_t *typeLabel = lv_label_create(row);
    lv_label_set_text(typeLabel, typeDisplay.c_str());
    lv_obj_set_style_text_color(typeLabel, COLOR_TEXT, 0);
    lv_obj_set_pos(typeLabel, 82, 9);
    lv_obj_set_width(typeLabel, 225);
    lv_label_set_long_mode(typeLabel, LV_LABEL_LONG_WRAP);

    lv_obj_t *amountLabel = lv_label_create(row);
    String amountText;
    if (type != "KARMIENIE") {
      amountText = entry.ml > 0 ? String(entry.ml) + " ml" : "";
    } else if (entry.piersLeft > 0 || entry.piersRight > 0) {
      amountText = "L" + String(entry.piersLeft) + "/P" + String(entry.piersRight);
    }
    lv_label_set_text(amountLabel, amountText.c_str());
    lv_obj_set_style_text_color(amountLabel, COLOR_TEXT, 0);
    lv_obj_set_pos(amountLabel, 345, 18);

    rowY += 62;
    ++recordCount;
  }
  file.close();

  if (recordCount == 0) {
    createLabel(container, "Brak wpisow dla tego dnia", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 12);
  }
}

// ---------------------------------- Data i wiek ---------------------------------
String formatDateTime(time_t value) {
  struct tm timeInfo;
  localtime_r(&value, &timeInfo);
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%d.%m.%Y  %H:%M", &timeInfo);
  return String(buffer);
}

long calculateAgeDays() {
  struct tm nowInfo;
  if (!currentLocalTime(nowInfo)) return -1;

  struct tm birthInfo = {};
  birthInfo.tm_year = BIRTH_YEAR - 1900;
  birthInfo.tm_mon = BIRTH_MONTH - 1;
  birthInfo.tm_mday = BIRTH_DAY;
  birthInfo.tm_hour = 12;

  nowInfo.tm_hour = 12;
  nowInfo.tm_min = 0;
  nowInfo.tm_sec = 0;
  const time_t birth = mktime(&birthInfo);
  const time_t now = mktime(&nowInfo);
  return lround(difftime(now, birth) / 86400.0);
}

String developmentTipForToday() {
  const long days = calculateAgeDays();
  if (days < 0) return "Rozwoj: oczekiwanie na prawidlowy czas";
  if (days >= DEVELOPMENT_TIP_COUNT) {
    return "Rozwoj po dniu 600: odkrywajcie swiat przez zabawe, ruch, rozmowe i bezpieczna bliskosc.";
  }
  return "Dzien " + String(days) + ": " + String(DEVELOPMENT_TIPS[days]);
}

String calculateAgeText() {
  struct tm nowInfo;
  if (!currentLocalTime(nowInfo)) return "Wiek: oczekiwanie na prawidłowy czas";

  struct tm birthInfo = {};
  birthInfo.tm_year = BIRTH_YEAR - 1900;
  birthInfo.tm_mon = BIRTH_MONTH - 1;
  birthInfo.tm_mday = BIRTH_DAY;
  birthInfo.tm_hour = 12; // południe minimalizuje wpływ zmiany czasu letniego

  nowInfo.tm_hour = 12;
  nowInfo.tm_min = 0;
  nowInfo.tm_sec = 0;

  const time_t birth = mktime(&birthInfo);
  const time_t now = mktime(&nowInfo);
  const long days = lround(difftime(now, birth) / 86400.0);

  if (days < 0) return "Wiek: data urodzenia jest w przyszłości";

  int fullMonths = (nowInfo.tm_year - birthInfo.tm_year) * 12 + (nowInfo.tm_mon - birthInfo.tm_mon);
  if (nowInfo.tm_mday < birthInfo.tm_mday) --fullMonths;
  if (fullMonths < 0) fullMonths = 0;

  const long weeks = days / 7;
  const long extraDays = days % 7;
  return "Aleksander ma " + String(days) + " dni\n" + String(weeks) + " tyg. i " + String(extraDays) + " dni | " + String(fullMonths) + " mies.";
}

// -------------------------------- Ekran główny ----------------------------------
void feedingButtonEvent(lv_event_t *event) {
  openEntryForm();
}

void calendarButtonEvent(lv_event_t *event) {
  createCalendarScreen();
}

void backHomeEvent(lv_event_t *event) {
  deleteModeActive = false;
  pendingDeleteIndex = -1;
  createHomeScreen();
}

// Wlacza/wylacza tryb wyboru wpisu do usuniecia.
void deleteToggleEvent(lv_event_t *event) {
  if (pendingDeleteIndex >= 0) {
    // Drugie klikniecie "POTWIERDZ" — wykonaj usuniecie
    String removed;
    if (deleteEntryByIndex(pendingDeleteIndex, removed)) {
      deletedEntryDescription = removed;
    }
    pendingDeleteIndex = -1;
    updateHomeInformation();
    createFeedingChartScreen();
    return;
  }
  // Przełącz tryb zaznaczania
  deleteModeActive = !deleteModeActive;
  pendingDeleteIndex = -1;
  deletedEntryDescription = "";
  createFeedingChartScreen();
}

// Zaznacza wpis do usuniecia (pierwsze klikniecie) lub go usuwa (jesli juz potwierdzony).
void deleteEntryEvent(lv_event_t *event) {
  if (!deleteModeActive) return;
  const int entryIndex = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (entryIndex < 0) return;
  // Jesli klikniety ten sam wpis co poprzednio — usun
  if (pendingDeleteIndex == entryIndex) {
    String removed;
    if (!deleteEntryByIndex(entryIndex, removed)) return;
    deletedEntryDescription = removed;
    pendingDeleteIndex = -1;
    deleteModeActive = false;
    updateHomeInformation();
    createFeedingChartScreen();
    return;
  }
  // Ustaw indeks oczekujacy na potwierdzenie i odrysuj
  pendingDeleteIndex = entryIndex;
  createFeedingChartScreen();
}

void diaperOpenEvent(lv_event_t *event) {
  createDiaperScreen();
}

void diaperQuickEvent(lv_event_t *event) {
  const char *type = static_cast<const char *>(lv_event_get_user_data(event));
  if (!type || !timeIsValid || !storageReady) return;
  if (!appendEntry(type, time(nullptr), 0)) return;
  deleteModeActive = false;
  createHomeScreen();
}

void vitaminToggleEvent(lv_event_t *event) {
  if (!timeIsValid || !storageReady) return;
  DaySummary s;
  dayStats(dayOffsetFromToday(0), s);
  if (s.vitaminD) return; // juz podano dzisiaj
  appendEntry("WITAMINA_D", time(nullptr), 0);
  createFeedingChartScreen(); // odswiez etykiete WIT.D
}

void calendarEditEvent(lv_event_t *event) {
  time_t *day = static_cast<time_t *>(lv_event_get_user_data(event));
  if (day) createDayDetailScreen(*day);
}

void addFeedingForDayEvent(lv_event_t *event) {
  openEntryFormForDay(selectedCalendarDay);
}

void chartButtonEvent(lv_event_t *event) {
  summaryExtraDays = 0;
  createFeedingChartScreen();
}

void moreDaysEvent(lv_event_t *event) {
  summaryExtraDays = min(summaryExtraDays + 7, SUMMARY_MAX_EXTRA_DAYS);
  createFeedingChartScreen();
}

void pumpingOpenEvent(lv_event_t *event) {
  selectedPumpingMl = DEFAULT_ML;
  createPumpingScreen();
}

void pumpingSliderEvent(lv_event_t *event) {
  lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
  selectedPumpingMl = lv_slider_get_value(slider);
  if (pumpingValueLabel) lv_label_set_text_fmt(pumpingValueLabel, "%d ml", selectedPumpingMl);
}

void pumpingSaveEvent(lv_event_t *event) {
  if (!timeIsValid || !storageReady) return;
  appendEntry("ODCIAGANIE", time(nullptr), selectedPumpingMl);
  deleteModeActive = false;
  createHomeScreen();
}

void createFeedingChartScreen() {
  resetReusableScreen(chartScreen);

  // Naglowek: tryb usuwania lub normalny
  if (pendingDeleteIndex >= 0) {
    createLabel(chartScreen, "DOTKNIJ PONOWNIE ABY USUNAC", COLOR_RED, LV_ALIGN_TOP_MID, 0, 10);
  } else if (deleteModeActive) {
    createLabel(chartScreen, "DOTKNIJ WPIS DO USUNIECIA", COLOR_RED, LV_ALIGN_TOP_MID, 0, 10);
  } else if (deletedEntryDescription.length() > 0) {
    createLabel(chartScreen, ("USUNIETO: " + deletedEntryDescription).c_str(), COLOR_RED, LV_ALIGN_TOP_MID, 0, 10);
  } else {
    createLabel(chartScreen, "PODSUMOWANIE - DZIS I WCZORAJ", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  }
  lv_obj_t *backButton = createButton(chartScreen, "POWROT", 14, 42, 110, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);
  // Guzik glowny: POTWIERDZ (jesli pending) / ANULUJ (tryb) / USUN WPIS (normalny)
  const char *deleteBtnText;
  lv_color_t deleteBtnColor;
  if (pendingDeleteIndex >= 0) {
    deleteBtnText = "POTWIERDZ";
    deleteBtnColor = COLOR_RED;
  } else if (deleteModeActive) {
    deleteBtnText = "ANULUJ";
    deleteBtnColor = COLOR_MUTED;
  } else {
    deleteBtnText = "USUN WPIS";
    deleteBtnColor = COLOR_RED;
  }
  lv_obj_t *deleteBtn = createButton(chartScreen, deleteBtnText, 132, 42, 160, 36, deleteBtnColor);
  lv_obj_add_event_cb(deleteBtn, deleteToggleEvent, LV_EVENT_CLICKED, nullptr);
  DaySummary todaySummary;
  dayStats(dayOffsetFromToday(0), todaySummary);
  lv_obj_t *vitButton = createButton(chartScreen, todaySummary.vitaminD ? "WIT.D OK" : "+ WIT.D",
                                     300, 42, 152, 36, todaySummary.vitaminD ? COLOR_GREEN : COLOR_ORANGE);
  lv_obj_add_event_cb(vitButton, vitaminToggleEvent, LV_EVENT_CLICKED, nullptr);

  // Przewijana lista: naglowek dnia + podsumowanie + kazde karmienie osobno.
  lv_obj_t *listCard = createCard(chartScreen, 14, 94, 452, 360);
  lv_obj_add_flag(listCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(listCard, LV_DIR_VER);

  const int totalDays = 2 + summaryExtraDays;
  std::vector<String> iso(totalDays);
  for (int d = 0; d < totalDays; ++d) iso[d] = dateIso(dayOffsetFromToday(static_cast<uint8_t>(d)));

// Jeden przebieg pliku: wiersze i agregaty dla wszystkich widocznych dni naraz.
  // Kazdy wpis ma globalny indeks linii (do usuwania) + pola do zbudowania tekstu
  // przy renderze — bez trzymania gotowych Stringow display (oszczednosc RAM).
  struct ChartEntry {
    int lineIndex;
    String time;
    uint8_t kind;   // 0=karmienie, 1=mleko, 2=pielucha, 3=odciaganie, 4=witD
    int ml;
    int piersLeft;
    int piersRight;
    uint8_t milkKind; // 0=MLEKO_MATKI, 1=MLEKO_MODYFIKOWANE, 2=inny/MLEKO
  };
  std::vector<std::vector<ChartEntry>> dayEntries(totalDays);
  std::vector<int> aggFeed(totalDays, 0), aggMilk(totalDays, 0), aggL(totalDays, 0), aggR(totalDays, 0);
  if (storageReady) {
    File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
    if (file) {
      file.readStringUntil('\n');
      int dataIndex = 0;
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) { ++dataIndex; continue; }
        CsvEntry entry;
        if (!parseCsvLine(line, entry)) { ++dataIndex; continue; }
        for (int d = 0; d < totalDays; ++d) {
          if (entry.date != iso[d]) continue;
          if (isMilkType(entry.type)) {
            aggMilk[d] += entry.ml;
            const uint8_t mkind = (entry.type == "MLEKO_MATKI") ? 0 : (entry.type == "MLEKO_MODYFIKOWANE" ? 1 : 2);
            dayEntries[d].push_back({dataIndex, entry.time, 1, entry.ml, 0, 0, mkind});
          } else if (entry.type == "KARMIENIE") {
            ++aggFeed[d];
            aggL[d] += entry.piersLeft;
            aggR[d] += entry.piersRight;
            dayEntries[d].push_back({dataIndex, entry.time, 0, 0, entry.piersLeft, entry.piersRight, 0});
          } else if (entry.type == "PIELUCHA_MOKRA" || entry.type == "PIELUCHA_BRUDNA") {
            dayEntries[d].push_back({dataIndex, entry.time, 2, entry.type == "PIELUCHA_MOKRA" ? 1 : 0, 0, 0, 0});
          } else if (entry.type == "ODCIAGANIE") {
            dayEntries[d].push_back({dataIndex, entry.time, 3, entry.ml, 0, 0, 0});
          } else if (entry.type == "WITAMINA_D") {
            dayEntries[d].push_back({dataIndex, entry.time, 4, 0, 0, 0, 0});
          }
          break;
        }
        ++dataIndex;
      }
      file.close();
    }
  }

  int rowY = 0;
  for (int d = 0; d < totalDays; ++d) {
    const time_t day = dayOffsetFromToday(static_cast<uint8_t>(d));
    struct tm dayInfo;
    localtime_r(&day, &dayInfo);
    char dayText[6];
    strftime(dayText, sizeof(dayText), "%d.%m", &dayInfo);

    String dayTitle = String(d == 0 ? "DZISIAJ " : (d == 1 ? "WCZORAJ " : "")) + dayText;
    if (d == 0 && lastFeedingTime) {
      dayTitle += "  |  " + formatAgoText(lastFeedingTime);
    }
    createLabel(listCard, dayTitle.c_str(), d == 0 ? COLOR_GREEN : COLOR_TEXT, LV_ALIGN_TOP_LEFT, 0, rowY);
    rowY += 24;

    const String summaryLine = String("KARM.: ") + aggFeed[d] + " | MLEKO: " + aggMilk[d] +
                               " ml | PIERS: L" + aggL[d] + "/P" + aggR[d];
    lv_obj_t *sumLabel = createLabel(listCard, summaryLine.c_str(), COLOR_MUTED, LV_ALIGN_TOP_LEFT, 0, rowY);
    lv_obj_set_width(sumLabel, 424);
    rowY += 26;

    if (dayEntries[d].empty()) {
      lv_obj_t *emptyLabel = createLabel(listCard, "Brak wpisow", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 0, rowY);
      lv_obj_set_width(emptyLabel, 424);
      rowY += 24;
    } else {
      // Budujemy tekst wpisu na . display w locie (oszczednosc RAM na dlugiej historii).
      for (const ChartEntry &ce : dayEntries[d]) {
        String display;
        switch (ce.kind) {
          case 0: {
            display = ce.time + "  KARMIENIE";
            if (ce.piersLeft > 0 || ce.piersRight > 0)
              display += String("  L") + ce.piersLeft + "/P" + ce.piersRight;
            break;
          }
          case 1: {
            const char *mkind = (ce.milkKind == 0) ? "MLEKO MATKI" : (ce.milkKind == 1 ? "MLEKO MODYFIKOWANE" : "MLEKO");
            display = ce.time + "  " + mkind + "  " + String(ce.ml) + " ml";
            break;
          }
          case 2: display = ce.time + "  PIELUCHA " + String(ce.ml ? "MOKRA" : "BRUDNA"); break;
          case 3: display = ce.time + "  ODCIAGANIE  " + String(ce.ml) + " ml"; break;
          case 4: display = ce.time + "  WITAMINA D"; break;
        }
        // W trybie usuwania kazdy wpis jest klikalnym przyciskiem; w normalnym — etykieta.
        if (deleteModeActive || pendingDeleteIndex >= 0) {
          const bool isPending = (ce.lineIndex == pendingDeleteIndex);
          String btnText = isPending ? (display + "  [POTWIERDZ]") : display;
          lv_obj_t *btn = createButton(listCard, btnText.c_str(), 0, rowY, 424, 26, COLOR_CARD);
          lv_obj_set_style_bg_color(btn, isPending ? COLOR_RED : COLOR_CARD, 0);
          lv_obj_set_style_bg_opa(btn, isPending ? LV_OPA_20 : LV_OPA_10, 0);
          lv_obj_set_style_text_color(btn, isPending ? COLOR_RED : COLOR_TEXT, 0);
          lv_obj_set_style_border_width(btn, 1, 0);
          lv_obj_set_style_border_color(btn, COLOR_RED, 0);
          lv_obj_set_style_radius(btn, 6, 0);
          lv_obj_set_style_shadow_width(btn, 0, 0);
          lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(ce.lineIndex)));
          lv_obj_add_event_cb(btn, deleteEntryEvent, LV_EVENT_CLICKED, nullptr);
        } else {
          lv_obj_t *rowLabel = createLabel(listCard, display.c_str(), COLOR_TEXT, LV_ALIGN_TOP_LEFT, 0, rowY);
          lv_obj_set_width(rowLabel, 424);
        }
        rowY += 26;
      }
    }
    rowY += 12;
  }

  // Lazy historia: kazdy klik doklada kolejne 7 dni wstecz.
  lv_obj_t *moreButton = createButton(listCard, "+ WCZYTAJ STARSZE DNI", 84, rowY + 4, 260, 40, COLOR_MUTED);
  lv_obj_add_event_cb(moreButton, moreDaysEvent, LV_EVENT_CLICKED, nullptr);

  loadReusableScreen(chartScreen);
}

void createPumpingScreen() {
  resetReusableScreen(pumpingScreen);
  pumpingValueLabel = nullptr;

  createLabel(pumpingScreen, "ODCIAGANIE MLEKA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(pumpingScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backToOtherEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *card = createCard(pumpingScreen, 14, 110, 452, 140);
  pumpingValueLabel = createLabel(card, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 12);
  lv_label_set_text_fmt(pumpingValueLabel, "%d ml", selectedPumpingMl);
  lv_obj_t *slider = lv_slider_create(card);
  lv_obj_set_pos(slider, 34, 74);
  lv_obj_set_size(slider, 384, 10);
  lv_slider_set_range(slider, ML_MIN, ML_MAX);
  lv_slider_set_value(slider, selectedPumpingMl, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, COLOR_BORDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, COLOR_BLUE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, COLOR_BLUE, LV_PART_KNOB);
  lv_obj_add_event_cb(slider, pumpingSliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);
  createLabel(card, "10 ml", COLOR_MUTED, LV_ALIGN_BOTTOM_LEFT, 12, -6);
  createLabel(card, "120 ml", COLOR_MUTED, LV_ALIGN_BOTTOM_RIGHT, -12, -6);

  lv_obj_t *saveButton = createButton(pumpingScreen, "ZAPISZ ODCIAG", 14, 270, 290, 50, COLOR_BLUE);
  lv_obj_add_event_cb(saveButton, pumpingSaveEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelButton = createButton(pumpingScreen, "ANULUJ", 316, 270, 150, 50, COLOR_MUTED);
  lv_obj_add_event_cb(cancelButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  loadReusableScreen(pumpingScreen);
}

void createDiaperScreen() {
  resetReusableScreen(diaperScreen);

  createLabel(diaperScreen, "PIELUCHA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(diaperScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backToOtherEvent, LV_EVENT_CLICKED, nullptr);

  static char TYPE_WET[] = "PIELUCHA_MOKRA";
  static char TYPE_DIRTY[] = "PIELUCHA_BRUDNA";
  lv_obj_t *wetButton = createButton(diaperScreen, "MOKRA", 14, 120, 452, 90, COLOR_BLUE);
  lv_obj_add_event_cb(wetButton, diaperQuickEvent, LV_EVENT_CLICKED, TYPE_WET);
  lv_obj_t *dirtyButton = createButton(diaperScreen, "BRUDNA", 14, 226, 452, 90, COLOR_ORANGE);
  lv_obj_add_event_cb(dirtyButton, diaperQuickEvent, LV_EVENT_CLICKED, TYPE_DIRTY);
  createLabel(diaperScreen, "Zapisuje biezacy czas jednym dotyknieciem.", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 330);

  loadReusableScreen(diaperScreen);
}

// Powrot z ekranow PIELUCHA/ODCIAGANIE do wspolnego ekranu INNE.
void backToOtherEvent(lv_event_t *event) {
  createOtherScreen();
}

// Ekran INNE: grupuje szybkie akcje PIELUCHA i ODCIAG POKARMU pod jednym miejscem.
// (Sen ma dedykowany przycisk SEN na ekranie glownym i wlasny ekran.)
void createOtherScreen() {
  resetReusableScreen(otherScreen);

  createLabel(otherScreen, "INNE", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(otherScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *diaperBtn = createButton(otherScreen, "PIELUCHA", 14, 90, 452, 58, COLOR_BLUE);
  lv_obj_add_event_cb(diaperBtn, diaperOpenEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *pumpingBtn = createButton(otherScreen, "ODCIAG POKARMU", 14, 156, 452, 58, COLOR_ORANGE);
  lv_obj_add_event_cb(pumpingBtn, pumpingOpenEvent, LV_EVENT_CLICKED, nullptr);
  // Sen ma wlasny przycisk SEN na ekranie glownym — tutaj juz go nie dublujemy.
  lv_obj_t *diagBtn = createButton(otherScreen, "DIAGNOSTYKA", 14, 222, 452, 58, COLOR_MUTED);
  lv_obj_add_event_cb(diagBtn, diagnosticsOpenEvent, LV_EVENT_CLICKED, nullptr);
  createLabel(otherScreen, "Pielucha, odciaganie, diagnostyka.",
              COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 300);

  loadReusableScreen(otherScreen);
}

// ------------------------------- Ekran SEN (Napper) -----------------------------
// Format "Xh Ymin" / "Ymin" dla czasu trwania (min).
String formatDurationShort(long minutes) {
  if (minutes < 0) return String("-");
  const long h = minutes / 60, m = minutes % 60;
  if (h == 0) return String(m) + " min";
  return String(h) + "h " + m + " min";
}

void sleepOpenEvent(lv_event_t *event) {
  createSleepScreen();
}

// Przelacznik ZASNIJ/OBUDZ na ekranie SEN — po zapisie odswieza ekran SEN (nie home).
void sleepScreenToggleEvent(lv_event_t *event) {
  if (!timeIsValid || !storageReady) return;
  appendEntry(sleepInProgress ? "SEN_STOP" : "SEN_START", time(nullptr), 0);
  loadLatestEntries(); // odswiez globale snu (sleepInProgress/sleepStartedTime/lastWakeTime)
  createSleepScreen();
}

void createSleepScreen() {
  resetReusableScreen(sleepScreen);

  createLabel(sleepScreen, "SEN", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(sleepScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  const long ageDays = calculateAgeDays();
  const WakeWindow ww = wakeWindowMinutes(ageDays);
  const time_t nowT = time(nullptr);

  // --- Karta STANU (spi / czuwa + okno drzemki) ---
  lv_obj_t *stateCard = createCard(sleepScreen, 14, 88, 452, 120);
  lv_color_t stateColor = COLOR_MUTED;
  String bigText, subText;
  if (sleepInProgress && sleepStartedTime) {
    const long sinceMin = static_cast<long>(difftime(nowT, sleepStartedTime) / 60);
    stateColor = lv_color_hex(0x6E5FA6);
    // formatDateTime => "DD.MM.YYYY␠␠HH:MM": godzina od indeksu 12 (substring(12,17)).
    bigText = "Spi od " + formatDateTime(sleepStartedTime).substring(12, 17);
    subText = "Czas snu: " + formatDurationShort(sinceMin);
  } else if (lastWakeTime > 0) {
    const long wakeMin = static_cast<long>(difftime(nowT, lastWakeTime) / 60);
    const time_t napStart = lastWakeTime + static_cast<time_t>(ww.minMin) * 60;
    const time_t napEnd = lastWakeTime + static_cast<time_t>(ww.maxMin) * 60;
    bigText = "Czuwa od " + formatDateTime(lastWakeTime).substring(12, 17) +
              "  (" + formatDurationShort(wakeMin) + ")";
    if (nowT < napStart) { stateColor = COLOR_GREEN; subText = "Za wczesnie. Okno drzemki ~" + formatDateTime(napStart).substring(12, 17) + "-" + formatDateTime(napEnd).substring(12, 17); }
    else if (nowT <= napEnd) { stateColor = COLOR_YELLOW; subText = "OKNO DRZEMKI TERAZ (do ~" + formatDateTime(napEnd).substring(12, 17) + ")"; }
    else { stateColor = COLOR_RED; subText = "Przekroczone okno (ryzyko przemeczenia)"; }
  } else {
    bigText = "Brak danych snu";
    subText = "Gdy dziecko zasnie — dotknij ZASNAL.";
  }
  lv_obj_t *bigLabel = createLabel(stateCard, bigText.c_str(), COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_width(bigLabel, 416);
  lv_obj_set_style_text_align(bigLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(bigLabel, &lv_font_montserrat_16, 0);
  // Pasek stanu okna czuwania (kolorowy). Szer. 416 wysrodkowana w obszarze 428.
  lv_obj_t *stateBar = lv_obj_create(stateCard);
  lv_obj_remove_style_all(stateBar);
  lv_obj_set_pos(stateBar, 6, 38);
  lv_obj_set_size(stateBar, 416, 30);
  lv_obj_set_style_radius(stateBar, 15, 0);
  lv_obj_set_style_bg_color(stateBar, stateColor, 0);
  lv_obj_set_style_bg_opa(stateBar, LV_OPA_COVER, 0);
  lv_obj_t *subLabel = createLabel(stateBar, subText.c_str(), lv_color_white(), LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(subLabel, 404);
  lv_obj_set_style_text_align(subLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(subLabel, &lv_font_montserrat_12, 0);
  // Okno czuwania wg wieku (informacyjnie).
  createLabel(stateCard,
              (String("Okno czuwania wg wieku: ") + formatDurationShort(ww.minMin) + " - " + formatDurationShort(ww.maxMin)).c_str(),
              COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 76);

  // --- Duzy przycisk (opisuje fakt: co dziecko wlasnie zrobilo) ---
  // Gdy czuwa -> klik = "ZASNAL"; gdy spi -> klik = "OBUDZIL SIE".
  lv_obj_t *toggleBtn = createButton(sleepScreen,
                                     sleepInProgress ? "OBUDZIL SIE" : "ZASNAL",
                                     14, 220, 452, 66,
                                     sleepInProgress ? COLOR_YELLOW : lv_color_hex(0x6E5FA6));
  lv_obj_add_event_cb(toggleBtn, sleepScreenToggleEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *toggleLbl = lv_obj_get_child(toggleBtn, 0);
  if (toggleLbl) lv_obj_set_style_text_font(toggleLbl, &lv_font_montserrat_16, 0);

  // --- Karta BILANSU DNIA (drzemki + sen dzien/noc vs cel) ---
  DaySummary today; dayStats(dayOffsetFromToday(0), today);
  int needNight = 0, needDay = 0; sleepNeedMinutes(ageDays, needNight, needDay);
  const int napTgt = napTargetCount(ageDays);
  // Wysokosc 140 (wnetrze 116): 4 linie montserrat_14 od y=26 mieszcza sie z zapasem.
  lv_obj_t *balCard = createCard(sleepScreen, 14, 298, 452, 140);
  lv_obj_set_style_bg_color(balCard, lv_color_mix(COLOR_CARD, lv_color_hex(0x6E5FA6), 12), 0);
  createLabel(balCard, "BILANS DNIA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  String balText;
  balText  = "Drzemki: " + String(today.napCount) + " (cel ~" + String(napTgt) + ")\n";
  balText += "Sen dzien: " + formatDurationShort(today.sleepDayMin) + " / cel " + formatDurationShort(needDay) + "\n";
  balText += "Sen noc: " + formatDurationShort(today.sleepNightMin) + " / cel " + formatDurationShort(needNight) + "\n";
  balText += "Razem: " + formatDurationShort(today.sleepDayMin + today.sleepNightMin);
  // Szerokosc 416 przy offsecie x=6 => prawy brzeg 422 < 428 (obszar wewn. karty).
  lv_obj_t *balLabel = createLabel(balCard, balText.c_str(), COLOR_TEXT, LV_ALIGN_TOP_LEFT, 6, 26);
  lv_obj_set_width(balLabel, 416);
  lv_obj_set_style_text_align(balLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(balLabel, &lv_font_montserrat_14, 0);

  loadReusableScreen(sleepScreen);
}

// ---------------------------- Ekran DIAGNOSTYKA ----------------------------
void diagnosticsOpenEvent(lv_event_t *event) {
  createDiagnosticsScreen();
}

// Czytelny opis powodu ostatniego resetu.
const char *resetReasonText(int reason) {
  switch (reason) {
    case ESP_RST_POWERON:  return "wlaczenie zasilania";
    case ESP_RST_SW:       return "restart programowy";
    case ESP_RST_PANIC:    return "panic (blad)";
    case ESP_RST_INT_WDT:  return "watchdog (przerwania)";
    case ESP_RST_TASK_WDT: return "watchdog (zadanie)";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "spadek napiecia";
    case ESP_RST_DEEPSLEEP:return "wybudzenie ze snu";
    case ESP_RST_EXT:      return "reset zewnetrzny";
    default:               return "nieznany";
  }
}

void createDiagnosticsScreen() {
  resetReusableScreen(diagnosticsScreen);

  createLabel(diagnosticsScreen, "DIAGNOSTYKA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(diagnosticsScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, otherOpenEvent, LV_EVENT_CLICKED, nullptr);

  // Karta ze statusami (przewijalna, gdyby tekst byl dlugi).
  lv_obj_t *card = createCard(diagnosticsScreen, 14, 90, 452, 344);
  lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);

  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const uint32_t up = millis() / 1000;
  const uint32_t upD = up / 86400, upH = (up % 86400) / 3600, upM = (up % 3600) / 60;
  const uint32_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024;
  const uint32_t minInt = (minFreeHeapEver == 0xFFFFFFFFUL) ? 0 : minFreeHeapEver / 1024;
  const uint32_t httpAgo = lastHttpMillis ? (millis() - lastHttpMillis) / 1000 : 0;

  String s;
  s  = String("Wi-Fi: ") + (wifiOk ? "polaczono" : "ROZLACZONO") + "\n";
  if (wifiOk) {
    s += "IP: " + WiFi.localIP().toString() + "\n";
    s += "Sygnal: " + String(WiFi.RSSI()) + " dBm\n";
  }
  s += "Serwer HTTP: " + String(webServerStarted ? "aktywny" : "zatrzymany") + "\n";
  s += "Obsluzonych zadan HTTP: " + String(httpRequestCount) + "\n";
  s += "Ostatnie zadanie HTTP: " + (lastHttpMillis ? (String(httpAgo) + " s temu") : String("-")) + "\n";
  s += "Czas (NTP): " + String(timeIsValid ? "OK" : "brak") + "\n";
  s += "Pamiec danych: " + String(storageReady ? "OK" : "BLAD") + "\n";
  if (dataFileHuge) s += "Uwaga: plik historii duzy (rozwaz eksport)\n";
  s += "Pogoda: " + String(weatherValidNow() ? "OK" : "brak danych") + "\n";
  s += "---\n";
  s += "RAM wewn. wolny: " + String(freeInt) + " KB\n";
  s += "RAM wewn. min: " + String(minInt) + " KB\n";
  s += "PSRAM wolny: " + String(ESP.getFreePsram() / 1024) + " KB\n";
  s += "CPU: " + String(cpuLoadPct) + " %\n";
  s += "Praca: " + String(upD) + "d " + String(upH) + "h " + String(upM) + "min\n";
  s += "---\n";
  s += "Watchdog: " + String(watchdogReady ? "aktywny" : "wylaczony") + "\n";
  s += "Uruchomien urzadzenia: " + String(bootCount) + "\n";
  s += "Restartow watchdoga: " + String(watchdogResetCount) + "\n";
  s += "Ostatni reset: " + String(resetReasonText(lastResetReason));

  lv_obj_t *info = createLabel(card, s.c_str(), COLOR_TEXT, LV_ALIGN_TOP_LEFT, 4, 2);
  lv_obj_set_width(info, 416);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

  // Przelacznik powiadomien Telegram o oknie snu (trwaly — zapis do ustawien).
  lv_obj_t *sleepTgLabel = createLabel(diagnosticsScreen, "Sen: powiadomienia Telegram",
                                       COLOR_TEXT, LV_ALIGN_TOP_LEFT, 18, 444);
  lv_obj_set_style_text_font(sleepTgLabel, &lv_font_montserrat_14, 0);
  lv_obj_t *sleepTgSwitch = lv_switch_create(diagnosticsScreen);
  lv_obj_set_pos(sleepTgSwitch, 390, 438);
  lv_obj_set_size(sleepTgSwitch, 70, 34);
  if (sleepTelegramEnabled) lv_obj_add_state(sleepTgSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sleepTgSwitch, sleepTelegramSwitchEvent, LV_EVENT_VALUE_CHANGED, nullptr);

  loadReusableScreen(diagnosticsScreen);
}

// Zmiana przelacznika powiadomien snu — aktualizuje flage i zapisuje ustawienia.
void sleepTelegramSwitchEvent(lv_event_t *event) {
  lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(event));
  sleepTelegramEnabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
  saveSettings();
}

// Ekran WAGA: wybor wagi dziecka w gramach i zapis wpisu typu WAGA.
void weightStepEvent(lv_event_t *event) {
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  selectedWeightG = constrain(selectedWeightG + delta, WEIGHT_MIN_G, WEIGHT_MAX_G);
  if (weightValueLabel) lv_label_set_text_fmt(weightValueLabel, "%d g", selectedWeightG);
}

void weightSliderEvent(lv_event_t *event) {
  lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
  const int raw = lv_slider_get_value(slider);
  // Skok co 10 g.
  int snapped = ((raw + 5) / 10) * 10;
  snapped = constrain(snapped, WEIGHT_MIN_G, WEIGHT_MAX_G);
  lv_slider_set_value(slider, snapped, LV_ANIM_OFF);
  selectedWeightG = snapped;
  if (weightValueLabel) lv_label_set_text_fmt(weightValueLabel, "%d g", selectedWeightG);
}

void weightSaveEvent(lv_event_t *event) {
  if (!timeIsValid || !storageReady) return;
  appendEntry("WAGA", time(nullptr), selectedWeightG);
  deleteModeActive = false;
  createHomeScreen();
}

void createWeightScreen() {
  resetReusableScreen(weightScreen);
  weightValueLabel = nullptr;

  createLabel(weightScreen, "WAGA DZIECKA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(weightScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *card = createCard(weightScreen, 14, 96, 452, 168);
  weightValueLabel = createLabel(card, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_font(weightValueLabel, &lv_font_montserrat_36, 0);
  lv_label_set_text_fmt(weightValueLabel, "%d g", selectedWeightG);

  // Precyzyjne kroki -10/+10 g.
  lv_obj_t *minus10 = createButton(card, "-10", 8, 66, 84, 44, COLOR_MUTED);
  lv_obj_add_event_cb(minus10, weightStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-10)));
  lv_obj_t *plus10 = createButton(card, "+10", 336, 66, 84, 44, COLOR_GREEN);
  lv_obj_add_event_cb(plus10, weightStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(10)));

  lv_obj_t *slider = lv_slider_create(card);
  lv_obj_set_pos(slider, 100, 80);
  lv_obj_set_size(slider, 228, 12);
  lv_slider_set_range(slider, WEIGHT_MIN_G, WEIGHT_MAX_G);
  lv_slider_set_value(slider, selectedWeightG, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, COLOR_BORDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, COLOR_GREEN, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, COLOR_GREEN, LV_PART_KNOB);
  lv_obj_add_event_cb(slider, weightSliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);
  createLabel(card, "2000 g", COLOR_MUTED, LV_ALIGN_BOTTOM_LEFT, 12, -6);
  createLabel(card, "15000 g", COLOR_MUTED, LV_ALIGN_BOTTOM_RIGHT, -12, -6);

  lv_obj_t *saveButton = createButton(weightScreen, "ZAPISZ WAGE", 14, 280, 290, 50, COLOR_GREEN);
  lv_obj_add_event_cb(saveButton, weightSaveEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelButton = createButton(weightScreen, "ANULUJ", 316, 280, 150, 50, COLOR_MUTED);
  lv_obj_add_event_cb(cancelButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  loadReusableScreen(weightScreen);
}

void otherOpenEvent(lv_event_t *event) {
  createOtherScreen();
}

void weightOpenEvent(lv_event_t *event) {
  // Zacznij od ostatniej znanej wagi (jesli jest), inaczej wartosc domyslna.
  if (lastWeightG > 0) selectedWeightG = constrain(lastWeightG, WEIGHT_MIN_G, WEIGHT_MAX_G);
  else selectedWeightG = DEFAULT_WEIGHT_G;
  createWeightScreen();
}

void createCalendarScreen() {
  resetReusableScreen(calendarScreen);

  createLabel(calendarScreen, "LESNY KALENDARZ - 3 DNI", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t *backButton = createButton(calendarScreen, "POWROT", 14, 42, 124, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  const time_t now = time(nullptr);
  const int cardY[3] = {92, 212, 332};
  for (uint8_t i = 0; i < 3; ++i) {
    struct tm dayInfo;
    localtime_r(&now, &dayInfo);
    dayInfo.tm_mday -= i;
    dayInfo.tm_hour = 12;
    dayInfo.tm_min = 0;
    dayInfo.tm_sec = 0;
    dayInfo.tm_isdst = -1;
    calendarDays[i] = beginningOfDay(mktime(&dayInfo));
    lv_obj_t *card = createCard(calendarScreen, 14, cardY[i], 452, 108);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, calendarDayTitle(calendarDays[i], i).c_str());
    lv_obj_set_style_text_color(title, i == 0 ? COLOR_GREEN : COLOR_TEXT, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(title, 8, 4);
    lv_obj_set_width(title, 298);

    lv_obj_t *editButton = createButton(card, "SZCZEGOLY", 316, 4, 116, 34, COLOR_BLUE);
    lv_obj_add_event_cb(editButton, calendarEditEvent, LV_EVENT_CLICKED, &calendarDays[i]);

    lv_obj_t *summary = lv_label_create(card);
    lv_label_set_text(summary, entriesForDay(calendarDays[i], true).c_str());
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(summary, COLOR_MUTED, 0);
    lv_obj_set_style_text_align(summary, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(summary, 8, 43);
    lv_obj_set_size(summary, 422, 56);
  }

  loadReusableScreen(calendarScreen);
}

void createDayDetailScreen(time_t day) {
  selectedCalendarDay = beginningOfDay(day);
  resetReusableScreen(dayDetailScreen);

  createLabel(dayDetailScreen, "DZIENNIK LESNEGO DNIA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 9);
  String dateText = formatDateTime(selectedCalendarDay).substring(0, 10);
  createLabel(dayDetailScreen, dateText.c_str(), COLOR_GREEN, LV_ALIGN_TOP_MID, 0, 31);

  lv_obj_t *backButton = createButton(dayDetailScreen, "KALENDARZ", 14, 58, 154, 36, COLOR_MUTED);
  lv_obj_add_event_cb(backButton, calendarButtonEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *entriesCard = createCard(dayDetailScreen, 14, 106, 452, 196);
  lv_obj_set_style_bg_color(entriesCard, lv_color_mix(COLOR_CARD, COLOR_TONAL_GREEN, 12), 0);
  lv_obj_add_flag(entriesCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(entriesCard, LV_DIR_VER);
  lv_obj_set_style_pad_all(entriesCard, 12, 0);
  populateDayEntries(entriesCard, selectedCalendarDay);

  createLabel(dayDetailScreen, "DODAJ KARMIENIE DO TEGO DNIA", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 314);
  lv_obj_t *feedingButton = createButton(dayDetailScreen, "+ KARMIENIE", 14, 342, 452, 54, COLOR_ORANGE);
  lv_obj_add_event_cb(feedingButton, addFeedingForDayEvent, LV_EVENT_CLICKED, nullptr);

  loadReusableScreen(dayDetailScreen);
}

void createHomeScreen() {
  resetReusableScreen(homeScreen);

  // Gorny pasek: tytul + skroty statusu z diodami (W/P/C) + zegar.
  lv_obj_t *titleLabel = createLabel(homeScreen, "LESNY DZIENNIK ALEKSANDRA", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 14, 14);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);

  const char *ledLetters[3] = {"W", "P", "C"};
  lv_obj_t **ledTargets[3] = {&homeLedWifi, &homeLedMemory, &homeLedTime};
  for (uint8_t i = 0; i < 3; ++i) {
    const int baseX = 266 + i * 28;
    lv_obj_t *cap = createLabel(homeScreen, ledLetters[i], COLOR_MUTED, LV_ALIGN_TOP_LEFT, baseX, 15);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_t *led = lv_obj_create(homeScreen);
    lv_obj_remove_style_all(led);
    lv_obj_set_size(led, 8, 8);
    lv_obj_align(led, LV_ALIGN_TOP_LEFT, baseX + 11, 18);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(led, COLOR_RED, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    *ledTargets[i] = led;
  }

  homeClockLabel = createLabel(homeScreen, "", COLOR_MUTED, LV_ALIGN_TOP_RIGHT, -14, 14);
  lv_obj_set_width(homeClockLabel, 122);
  lv_obj_set_style_text_font(homeClockLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(homeClockLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(homeClockLabel, LV_ALIGN_TOP_RIGHT, -14, 14);

  // Gora ekranu: po lewej OSTATNIE KARMIENIE (czytelniej, zamiast codziennych tipsow —
  // wskazowka rozwojowa pozostaje dostepna w panelu WWW), po prawej wiek Aleksandra.
  feedingCard = createCard(homeScreen, 14, 44, 268, 100);
  lv_obj_set_style_bg_color(feedingCard, lv_color_mix(COLOR_CARD, COLOR_ORANGE, 16), 0);
  lv_obj_set_style_shadow_width(feedingCard, 4, 0);
  createLabel(feedingCard, "OSTATNIE KARMIENIE", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  homeFeedingLabel = createLabel(feedingCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_width(homeFeedingLabel, 244);
  lv_obj_set_style_text_align(homeFeedingLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(homeFeedingLabel, LV_LABEL_LONG_WRAP);

  lv_obj_t *ageCard = createCard(homeScreen, 290, 44, 176, 100);
  lv_obj_set_style_bg_color(ageCard, COLOR_TONAL_GREEN, 0);
  lv_obj_set_style_border_width(ageCard, 0, 0);
  lv_obj_set_style_shadow_width(ageCard, 0, 0);
  homeAgeLabel = createLabel(ageCard, "", COLOR_GREEN, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_width(homeAgeLabel, 160);
  lv_obj_set_style_text_align(homeAgeLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(homeAgeLabel, LV_LABEL_LONG_WRAP);

  // Belka licznika: w ostatnich 30 minutach przed terminem miga bialo/czerwono.
  homeCounterBar = lv_obj_create(homeScreen);
  lv_obj_remove_style_all(homeCounterBar);
  lv_obj_set_pos(homeCounterBar, 14, 148);
  lv_obj_set_size(homeCounterBar, 452, 30);
  lv_obj_set_style_radius(homeCounterBar, 15, 0);
  lv_obj_set_style_bg_color(homeCounterBar, COLOR_TONAL_GREEN, 0);
  lv_obj_set_style_bg_opa(homeCounterBar, LV_OPA_COVER, 0);
  homeCounterLabel = createLabel(homeCounterBar, "", COLOR_MUTED, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(homeCounterLabel, 440);
  lv_obj_set_style_text_align(homeCounterLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(homeCounterLabel, &lv_font_montserrat_16, 0);

  // Ostatnia butelka na calej szerokosci (KARMIENIE przeniesione na gore ekranu).
  milkCard = createCard(homeScreen, 14, 190, 452, 70);
  lv_obj_set_style_bg_color(milkCard, lv_color_mix(COLOR_CARD, COLOR_BLUE, 16), 0);
  lv_obj_set_style_shadow_width(milkCard, 4, 0);
  createLabel(milkCard, "OSTATNIA BUTELKA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  homeMilkLabel = createLabel(milkCard, "", COLOR_TEXT, LV_ALIGN_CENTER, 0, 10);

  feedFormButton = createButton(homeScreen, "KARMIENIE", 14, 268, 452, 48, COLOR_ORANGE);
  lv_obj_add_event_cb(feedFormButton, feedingButtonEvent, LV_EVENT_CLICKED, nullptr);

  // Dolna nawigacja: 3 kolumny x 2 rzedy (6 slotow). Szerokosc kolumny 145 px,
  // odstep 8: x = 14, 167, 320. SEN wyrozniony fioletem; gdy dziecko spi -> zolty.
  const int navW = 145;
  const int navX[3] = {14, 167, 320};
  // Rzad 1 (y=324): SEN, INNE, WAGA
  sleepHomeButton = createButton(homeScreen, sleepInProgress ? "SEN (spi)" : "SEN",
                                 navX[0], 324, navW, 40,
                                 sleepInProgress ? COLOR_YELLOW : lv_color_hex(0x6E5FA6));
  lv_obj_add_event_cb(sleepHomeButton, sleepOpenEvent, LV_EVENT_CLICKED, nullptr);
  otherHomeButton = createButton(homeScreen, "INNE", navX[1], 324, navW, 40, COLOR_BLUE);
  lv_obj_add_event_cb(otherHomeButton, otherOpenEvent, LV_EVENT_CLICKED, nullptr);
  weightHomeButton = createButton(homeScreen, "WAGA", navX[2], 324, navW, 40, COLOR_BLUE);
  lv_obj_add_event_cb(weightHomeButton, weightOpenEvent, LV_EVENT_CLICKED, nullptr);
  // Rzad 2 (y=372): KALENDARZ, PODSUMOWANIE (2 kolumny szersze dla czytelnosci etykiet)
  calendarButton = createButton(homeScreen, "KALENDARZ", navX[0], 372, 220, 40, COLOR_BLUE);
  lv_obj_add_event_cb(calendarButton, calendarButtonEvent, LV_EVENT_CLICKED, nullptr);
  chartButton = createButton(homeScreen, "PODSUMOWANIE", 246, 372, 220, 40, COLOR_GREEN);
  lv_obj_add_event_cb(chartButton, chartButtonEvent, LV_EVENT_CLICKED, nullptr);

  // ===================== WYGASZACZ =====================
  // Uklad (gora ekranu tipCard/ageCard 44..144 pozostaja widoczne):
  //   Karta zegara      150..250  (zegar + data + ostatnie/nastepne karmienie)
  //   Pasmo doby        256..300  (karmienia/pieluchy na osi 0-24h)
  //   Karta pogody      306..470  (ikona + temp + opis + min/max + 3h + ubior)

  // ===================== WYGASZACZ (nowy uklad) =====================
  //   Karta zegara      150..238  (88; zegar + data u gory, linia karmienia nizej)
  //   Karta statystyk   244..292  (48; chudszy pasek 3 liczb)
  //   Karta pogody      298..474  (176; ikona+temp+opis+minmax, godziny, ubior)

  // --- Karta zegara ---
  ssClockCard = createCard(homeScreen, 14, 150, 452, 88);
  lv_obj_set_style_pad_all(ssClockCard, 10, 0);

  // Lewa kolumna: zegar (font 36 — dostepny w lv_conf) + pod nim dzien tygodnia i data.
  ssClockLabel = createLabel(ssClockCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 4, 0);
  lv_obj_set_style_text_font(ssClockLabel, &lv_font_montserrat_36, 0);
  lv_obj_set_style_text_align(ssClockLabel, LV_TEXT_ALIGN_LEFT, 0);
  // ssClockShadowLabel nieuzywany (plaski zegar) — zostaje ukryty.
  ssClockShadowLabel = createLabel(ssClockCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 4, 0);
  lv_obj_set_style_text_font(ssClockShadowLabel, &lv_font_montserrat_36, 0);
  lv_obj_add_flag(ssClockShadowLabel, LV_OBJ_FLAG_HIDDEN);

  ssDateLabel = createLabel(ssClockCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 4, 46);
  lv_obj_set_width(ssDateLabel, 130);
  lv_obj_set_style_text_font(ssDateLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(ssDateLabel, LV_TEXT_ALIGN_LEFT, 0);

  // Prawa czesc (od x=146): linia 1 = ostatnie karmienie + (nast. ~HH:MM),
  // linia 2 = informacja o drzemce. Osobne etykiety, bez zawijania na zegar.
  // Font 12 (nie 14): dluzsza linia karmienia zawija sie max na 2 wiersze i miesci
  // z zapasem nad wierszem drzemki (bez nachodzenia w pionie w karcie 68 px wnetrza).
  ssLastFeedingLabel = createLabel(ssClockCard, "", COLOR_GREEN, LV_ALIGN_TOP_LEFT, 146, 4);
  lv_obj_set_width(ssLastFeedingLabel, 286);
  lv_obj_set_style_text_font(ssLastFeedingLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(ssLastFeedingLabel, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(ssLastFeedingLabel, LV_LABEL_LONG_WRAP);
  ssSleepLabel = createLabel(ssClockCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 146, 46);
  lv_obj_set_width(ssSleepLabel, 286);
  lv_obj_set_style_text_font(ssSleepLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(ssSleepLabel, LV_TEXT_ALIGN_LEFT, 0);
  // ssNextFeedLabel nieuzywany osobno (scalony w linii karmienia) — ukryty.
  ssNextFeedLabel = createLabel(ssClockCard, "", COLOR_BLUE, LV_ALIGN_BOTTOM_RIGHT, -4, 0);
  lv_obj_add_flag(ssNextFeedLabel, LV_OBJ_FLAG_HIDDEN);

  // --- Karta statystyk dnia (chudsza): 3 kafelki liczbowe ---
  ssDayBandCard = createCard(homeScreen, 14, 244, 452, 48);
  lv_obj_set_style_pad_all(ssDayBandCard, 6, 0);
  // ssDayBandTrack sluzy teraz jako niewidoczny kontener 3 kafelkow statystyk.
  ssDayBandTrack = ssDayBandCard;
  ssDayBandStamp = -1;
  const char *statCaps[3] = {"KARMIENIA", "NAJDL. PRZERWA", "SR. PRZERWA"};
  const int statX[3] = {6, 156, 306};
  for (uint8_t i = 0; i < 3; ++i) {
    ssStatValue[i] = createLabel(ssDayBandCard, "-", COLOR_GREEN, LV_ALIGN_TOP_LEFT, statX[i], 0);
    lv_obj_set_width(ssStatValue[i], 134);
    lv_obj_set_style_text_align(ssStatValue[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ssStatValue[i], &lv_font_montserrat_16, 0);
    lv_obj_t *cap = createLabel(ssDayBandCard, statCaps[i], COLOR_MUTED, LV_ALIGN_TOP_LEFT, statX[i], 22);
    lv_obj_set_width(cap, 134);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_10, 0);
    if (i > 0) {
      lv_obj_t *sep = lv_obj_create(ssDayBandCard);
      lv_obj_remove_style_all(sep);
      lv_obj_set_size(sep, 1, 32);
      lv_obj_set_pos(sep, statX[i] - 8, 2);
      lv_obj_set_style_bg_color(sep, COLOR_BORDER, 0);
      lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    }
  }

  // --- Karta pogody (wyzsza dzieki chudszej karcie statystyk) ---
  ssWeatherCard = createCard(homeScreen, 14, 298, 452, 176);

  ssIconBox = lv_obj_create(ssWeatherCard);
  lv_obj_remove_style_all(ssIconBox);
  lv_obj_set_size(ssIconBox, 92, 92);
  lv_obj_set_pos(ssIconBox, 4, 6);
  ssLastIconCode = -999;

  // Gorna czesc: ikona (lewa) + temperatura/opis/minmax (prawa).
  ssTempLabel = createLabel(ssWeatherCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 112, 2);
  lv_obj_set_style_text_font(ssTempLabel, &lv_font_montserrat_36, 0);

  ssDescLabel = createLabel(ssWeatherCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 114, 44);
  lv_obj_set_width(ssDescLabel, 320);
  lv_obj_set_style_text_font(ssDescLabel, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(ssDescLabel, LV_LABEL_LONG_DOT);

  ssMinMaxLabel = createLabel(ssWeatherCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 114, 66);
  lv_obj_set_style_text_font(ssMinMaxLabel, &lv_font_montserrat_14, 0);

  // Srodek: 3 kolejne godziny (pod ikona/temp, w jednym rzedzie).
  for (uint8_t i = 0; i < 3; ++i) {
    ssHourLabels[i] = createLabel(ssWeatherCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID,
                                  static_cast<int>(-140 + i * 140), 96);
    lv_obj_set_width(ssHourLabels[i], 128);
    lv_obj_set_style_text_align(ssHourLabels[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ssHourLabels[i], &lv_font_montserrat_12, 0);
  }

  // Dol: porada ubioru na tonalnym pasku (jedna linia z przycieciem — nie zaslania).
  lv_obj_t *dressBar = lv_obj_create(ssWeatherCard);
  lv_obj_remove_style_all(dressBar);
  lv_obj_set_size(dressBar, 428, 26);
  lv_obj_align(dressBar, LV_ALIGN_BOTTOM_MID, 0, 2);
  lv_obj_set_style_radius(dressBar, 9, 0);
  lv_obj_set_style_bg_color(dressBar, COLOR_TONAL_GREEN, 0);
  lv_obj_set_style_bg_opa(dressBar, LV_OPA_COVER, 0);
  lv_obj_clear_flag(dressBar, LV_OBJ_FLAG_SCROLLABLE);
  ssDressLabel = createLabel(dressBar, "", COLOR_MUTED, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(ssDressLabel, 414);
  lv_obj_set_style_text_align(ssDressLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ssDressLabel, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(ssDressLabel, LV_LABEL_LONG_DOT);

  ssRenderedClock = "";
  ssRenderedDate = "";
  ssRenderedTemp = "";
  ssRenderedDescription = "";
  ssRenderedMinMax = "";
  ssRenderedDress = "";
  ssRenderedLastFeeding = "";
  ssRenderedSleep = "";
  ssRenderedNextFeed = "";
  for (uint8_t i = 0; i < 3; ++i) ssRenderedHours[i] = "";
  for (uint8_t i = 0; i < 3; ++i) ssRenderedStat[i] = "";

  applyScreensaverVisibility();

  renderedClock = "";
  renderedAge = "";
  renderedFeeding = "";
  renderedMilk = "";
  renderedCounter = "";
  updateHomeInformation();
  loadReusableScreen(homeScreen);
}

void updateHomeInformation() {
  if (!homeClockLabel) return;

  struct tm nowInfo;
  String clockText;
  if (currentLocalTime(nowInfo)) {
    clockText = formatDateTime(time(nullptr));
    timeIsValid = true;
  } else {
    clockText = "Brak potwierdzonego czasu";
    timeIsValid = false;
  }

  const bool wifiOk = WiFi.status() == WL_CONNECTED;

  const String ageText = calculateAgeText();

  // Dioda przy kazdej sekcji statusu: zielona = OK, czerwona = blad.
  if (homeLedWifi) lv_obj_set_style_bg_color(homeLedWifi, wifiOk ? COLOR_GREEN : COLOR_RED, 0);
  if (homeLedMemory) lv_obj_set_style_bg_color(homeLedMemory, storageReady ? COLOR_GREEN : COLOR_RED, 0);
  if (homeLedTime) lv_obj_set_style_bg_color(homeLedTime, timeIsValid ? COLOR_GREEN : COLOR_RED, 0);

  // Widoczny pasek licznika czasu od ostatniego karmienia (nad kartami).
  String counterText;
  if (lastFeedingTime) {
    const long elapsedMin = static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60);
    if (elapsedMin >= 0 && elapsedMin < COUNTER_BLINK_MIN) {
      counterText = "OSTATNIE KARMIENIE: " + formatAgoText(lastFeedingTime);
    } else {
      counterText = "CZAS NA KARMIENIE: " + formatAgoText(lastFeedingTime);
    }
    counterRemainMin = (elapsedMin >= 0)
                           ? static_cast<int>(COUNTER_BLINK_MIN - elapsedMin)
                           : -1;
  } else {
    counterText = "OSTATNIE KARMIENIE: brak wpisu";
    counterRemainMin = -1;
  }
  setLabelTextIfChanged(homeCounterLabel, renderedCounter, counterText);
  updateCounterAlarmVisuals();

  setLabelTextIfChanged(homeClockLabel, renderedClock, clockText);
  setLabelTextIfChanged(homeAgeLabel, renderedAge, ageText);
  setLabelTextIfChanged(homeFeedingLabel, renderedFeeding, compactHomeEntry(lastFeeding, false));
  setLabelTextIfChanged(homeMilkLabel, renderedMilk, compactHomeEntry(lastMilk, true));
}

// Belka licznika wg czasu od ostatniego karmienia:
//   < 3 h        — zielona pastelowa
//   3 h .. 4 h   — zolta (zbliza sie pora)
//   >= 4 h       — miga biel/czerwień (sygnalizator: czas na karmienie)
void updateCounterAlarmVisuals() {
  if (!homeCounterBar || !homeCounterLabel) return;

  if (!lastFeedingTime) {
    lv_obj_set_style_bg_color(homeCounterBar, COLOR_TONAL_GREEN, 0);
    lv_obj_set_style_text_color(homeCounterLabel, COLOR_MUTED, 0);
    return;
  }

  long elapsedMin = static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60);
  if (elapsedMin < 0) elapsedMin = 0;

  if (elapsedMin >= COUNTER_BLINK_MIN) {
    // Przekroczenie 4 h: naprzemienne biale/czerwone tlo.
    const bool redPhase = counterAlarmPhase;
    lv_obj_set_style_bg_color(homeCounterBar, redPhase ? COLOR_RED : lv_color_white(), 0);
    lv_obj_set_style_text_color(homeCounterLabel, redPhase ? lv_color_white() : COLOR_RED, 0);
    return;
  }

  counterAlarmPhase = false;
  if (elapsedMin >= COUNTER_WARN_MIN) {
    lv_obj_set_style_bg_color(homeCounterBar, COLOR_YELLOW, 0);
    lv_obj_set_style_text_color(homeCounterLabel, COLOR_TEXT, 0);
    return;
  }

  lv_obj_set_style_bg_color(homeCounterBar, COLOR_TONAL_GREEN, 0);
  lv_obj_set_style_text_color(homeCounterLabel, COLOR_MUTED, 0);
}

// Timer 500 ms: steruje mignieciem belki i odswieza teksty tylko na home.
void counterAlarmTickCb(lv_timer_t *timer) {
  (void)timer;
  if (otaInProgress || !homeScreen || lv_screen_active() != homeScreen) return;

  static uint8_t divider = 0;
  ++divider;
  if (divider >= 2) { // co ~1 s odswiez teksty licznika
    divider = 0;
    updateHomeInformation();
  }
  updateCounterAlarmVisuals();
}

// ---------------------------------- Formularz -----------------------------------
void updateMilkTypeButtons() {
  if (!formMilkMatkiButton || !formMilkModifiedButton) return;
  lv_obj_set_style_bg_color(formMilkMatkiButton, extraMilkModified ? COLOR_MUTED : COLOR_GREEN, 0);
  lv_obj_set_style_bg_color(formMilkModifiedButton, extraMilkModified ? COLOR_ORANGE : COLOR_MUTED, 0);
}

void updateExtraMilkVisibility() {
  if (!formMilkCard) return;
  if (extraMilkEnabled) lv_obj_clear_flag(formMilkCard, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(formMilkCard, LV_OBJ_FLAG_HIDDEN);

  // Butelka rozwija własną kartę poniżej przycisku. Pozostałe akcje przesuwają się dopiero pod nią.
  // Karta mleka: y=330, wys.=144 => dol na 474; przyciski akcji tuz pod nia (482).
  const int actionY = extraMilkEnabled ? 482 : 330;
  if (formSaveButton) lv_obj_set_pos(formSaveButton, 14, actionY);
  if (formCancelButton) lv_obj_set_pos(formCancelButton, 316, actionY);
  if (formStatusLabel) lv_obj_set_pos(formStatusLabel, 25, actionY + 54);
  if (formBottleToggleLabel) lv_label_set_text(formBottleToggleLabel, extraMilkEnabled ? "SZCZEGOLY - UKRYJ" : "SZCZEGOLY");
  updateMilkTypeButtons();
}

void toggleBottleEvent(lv_event_t *event) {
  extraMilkEnabled = !extraMilkEnabled;
  updateExtraMilkVisibility();
}

void updateFormValues() {
  if (!formDateTimeLabel) return;
  lv_label_set_text(formDateTimeLabel, formatDateTime(selectedEntryTime).c_str());
  if (formMlLabel) lv_label_set_text(formMlLabel, "Karmienie bez ilosci ml");
  if (formMilkMlLabel) lv_label_set_text(formMilkMlLabel, (String(selectedMilkMl) + " ml").c_str());
  if (piersLeftCtl.label) lv_label_set_text_fmt(piersLeftCtl.label, "%d min", piersLeftCtl.value);
  if (piersRightCtl.label) lv_label_set_text_fmt(piersRightCtl.label, "%d min", piersRightCtl.value);
}

void minus5Event(lv_event_t *event) {
  selectedEntryTime -= 5 * 60;
  updateFormValues();
}

void plus5Event(lv_event_t *event) {
  selectedEntryTime += 5 * 60;
  updateFormValues();
}

void milkSliderEvent(lv_event_t *event) {
  lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(event));
  // Skok co 5 ml (5, 10, 15, ... 120).
  const int raw = lv_slider_get_value(slider);
  int snapped = ((raw + 2) / 5) * 5;
  snapped = constrain(snapped, ML_MIN, ML_MAX);
  lv_slider_set_value(slider, snapped, LV_ANIM_OFF);
  selectedMilkMl = snapped;
  updateFormValues();
}

void milkMatkiEvent(lv_event_t *event) {
  extraMilkModified = false;
  updateMilkTypeButtons();
}

void piersStepEvent(lv_event_t *event) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
  PiersControl *ctl = static_cast<PiersControl *>(lv_obj_get_user_data(target));
  if (!ctl || !ctl->label) return;
  const int delta = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  ctl->value = constrain(ctl->value + delta, 0, 90);
  lv_label_set_text_fmt(ctl->label, "%d min", ctl->value);
}

void milkModifiedEvent(lv_event_t *event) {
  extraMilkModified = true;
  updateMilkTypeButtons();
}

void cancelFormEvent(lv_event_t *event) {
  if (formReturnToCalendar) createDayDetailScreen(selectedCalendarDay);
  else createHomeScreen();
}

void hideFormConfirm() {
  if (formConfirmOverlay) {
    lv_obj_del(formConfirmOverlay);
    formConfirmOverlay = nullptr;
  }
}

void confirmFormCancel(lv_event_t *event) {
  hideFormConfirm();
}

// Faktyczny zapis — wykonuje sie po potwierdzeniu w oknie dialogowym.
void performSaveForm() {
  if (!appendEntry("KARMIENIE", selectedEntryTime, 0, piersLeftCtl.value, piersRightCtl.value)) {
    lv_label_set_text(formStatusLabel, "Blad zapisu karmienia do pliku CSV.");
    lv_obj_set_style_text_color(formStatusLabel, COLOR_RED, 0);
    return;
  }
  if (extraMilkEnabled) {
    const char *milkType = extraMilkModified ? "MLEKO_MODYFIKOWANE" : "MLEKO_MATKI";
    if (!appendEntry(milkType, selectedEntryTime, selectedMilkMl)) {
      lv_label_set_text(formStatusLabel, "Karmienie zapisane, ale blad zapisu mleka.");
      lv_obj_set_style_text_color(formStatusLabel, COLOR_RED, 0);
      return;
    }
  }

  if (formReturnToCalendar) createDayDetailScreen(selectedCalendarDay);
  else createHomeScreen();
}

void confirmFormSave(lv_event_t *event) {
  hideFormConfirm();
  performSaveForm();
}

void showFormConfirm() {
  if (formConfirmOverlay) return;

  // Przymglenie tla
  lv_obj_t *overlay = lv_obj_create(formScreen);
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, 480, 480);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  formConfirmOverlay = overlay;

  // Boks dialogowy
  lv_obj_t *box = createCard(overlay, 55, 175, 370, 130);
  createLabel(box, "ZAPISAC KARMIENIE?", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 16);
  lv_obj_t *cancelBtn = createButton(box, "ANULUJ", 30, 62, 140, 44, COLOR_MUTED);
  lv_obj_add_event_cb(cancelBtn, confirmFormCancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *okBtn = createButton(box, "ZAPISZ", 200, 62, 140, 44, COLOR_ORANGE);
  lv_obj_add_event_cb(okBtn, confirmFormSave, LV_EVENT_CLICKED, nullptr);
}

void saveFormEvent(lv_event_t *event) {
  if (!timeIsValid || selectedEntryTime < 1735689600) {
    lv_label_set_text(formStatusLabel, "Nieprawidlowy czas. Sprawdz Wi-Fi i NTP.");
    lv_obj_set_style_text_color(formStatusLabel, COLOR_RED, 0);
    return;
  }
  if (!storageReady) {
    lv_label_set_text(formStatusLabel, "Nie mozna zapisac: pamiec wewnetrzna niedostepna.");
    lv_obj_set_style_text_color(formStatusLabel, COLOR_RED, 0);
    return;
  }

  showFormConfirm();
}

void createFormScreen() {
  formDateTimeLabel = nullptr;
  formMlLabel = nullptr;
  formMilkMlLabel = nullptr;
  formMilkCard = nullptr;
  formMilkMatkiButton = nullptr;
  formMilkModifiedButton = nullptr;
  formBottleToggleButton = nullptr;
  formBottleToggleLabel = nullptr;
  formSaveButton = nullptr;
  formCancelButton = nullptr;
  formStatusLabel = nullptr;
  formConfirmOverlay = nullptr;
  piersLeftCtl.label = nullptr;
  piersRightCtl.label = nullptr;

  resetReusableScreen(formScreen);
  lv_obj_add_flag(formScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(formScreen, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(formScreen, LV_SCROLLBAR_MODE_AUTO);

  createLabel(formScreen, formReturnToCalendar ? "DODAJ KARMIENIE" : "NOWE KARMIENIE", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 6);

  // CZAS w jednej linii: data i godzina razem, bez osobnego podpisu sekcji.
  // Wysokosc 104: etykieta daty + przyciski ±5 MIN mieszcza sie w obramowaniu.
  lv_obj_t *timeCard = createCard(formScreen, 14, 34, 452, 104);
  formDateTimeLabel = createLabel(timeCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_t *minusButton = createButton(timeCard, "-5 MIN", 18, 38, 198, 40, COLOR_MUTED);
  lv_obj_add_event_cb(minusButton, minus5Event, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *plusButton = createButton(timeCard, "+5 MIN", 236, 38, 198, 40, COLOR_MUTED);
  lv_obj_add_event_cb(plusButton, plus5Event, LV_EVENT_CLICKED, nullptr);

  // PIERS: minuty karmienia osobno dla lewej i prawej strony.
  lv_obj_t *piersCard = createCard(formScreen, 14, 150, 452, 118);
  createLabel(piersCard, "PIERS - CZAS KARMIENIA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_t *leftName = createLabel(piersCard, "LEWA", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 14, 26);
  lv_obj_set_width(leftName, 204);
  lv_obj_set_style_text_align(leftName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t *rightName = createLabel(piersCard, "PRAWA", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 234, 26);
  lv_obj_set_width(rightName, 204);
  lv_obj_set_style_text_align(rightName, LV_TEXT_ALIGN_CENTER, 0);

  piersLeftCtl.label = createLabel(piersCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 66, 62);
  lv_obj_set_width(piersLeftCtl.label, 80);
  lv_obj_set_style_text_align(piersLeftCtl.label, LV_TEXT_ALIGN_CENTER, 0);
  piersRightCtl.label = createLabel(piersCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 286, 62);
  lv_obj_set_width(piersRightCtl.label, 80);
  lv_obj_set_style_text_align(piersRightCtl.label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *minusLeftBtn = createButton(piersCard, "-5", 14, 52, 48, 40, COLOR_MUTED);
  lv_obj_set_user_data(minusLeftBtn, &piersLeftCtl);
  lv_obj_add_event_cb(minusLeftBtn, piersStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-5)));
  lv_obj_t *plusLeftBtn = createButton(piersCard, "+5", 150, 52, 48, 40, COLOR_GREEN);
  lv_obj_set_user_data(plusLeftBtn, &piersLeftCtl);
  lv_obj_add_event_cb(plusLeftBtn, piersStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(5)));
  lv_obj_t *minusRightBtn = createButton(piersCard, "-5", 234, 52, 48, 40, COLOR_MUTED);
  lv_obj_set_user_data(minusRightBtn, &piersRightCtl);
  lv_obj_add_event_cb(minusRightBtn, piersStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(-5)));
  lv_obj_t *plusRightBtn = createButton(piersCard, "+5", 370, 52, 48, 40, COLOR_GREEN);
  lv_obj_set_user_data(plusRightBtn, &piersRightCtl);
  lv_obj_add_event_cb(plusRightBtn, piersStepEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(5)));

  formBottleToggleButton = createButton(formScreen, "SZCZEGOLY", 14, 278, 452, 44, COLOR_BLUE);
  formBottleToggleLabel = lv_obj_get_child(formBottleToggleButton, 0);
  lv_obj_add_event_cb(formBottleToggleButton, toggleBottleEvent, LV_EVENT_CLICKED, nullptr);

  // Wysokosc 144: obszar wewnetrzny (144 - 2*12 padding = 120) miesci suwak, podpisy
  // i rzad przyciskow MATKI/MODYFIKOWANE (dol na y=114) bez wychodzenia poza karte.
  formMilkCard = createCard(formScreen, 14, 330, 452, 144);
  lv_obj_set_style_bg_color(formMilkCard, lv_color_mix(COLOR_CARD, COLOR_BLUE, 12), 0);
  createLabel(formMilkCard, "BUTELKA - ILOSC I RODZAJ", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  formMilkMlLabel = createLabel(formMilkCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_t *milkSlider = lv_slider_create(formMilkCard);
  lv_obj_set_pos(milkSlider, 8, 46);
  lv_obj_set_size(milkSlider, 412, 10);
  lv_slider_set_range(milkSlider, ML_MIN, ML_MAX);
  lv_slider_set_value(milkSlider, selectedMilkMl, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(milkSlider, COLOR_BORDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(milkSlider, COLOR_ORANGE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(milkSlider, COLOR_ORANGE, LV_PART_KNOB);
  lv_obj_add_event_cb(milkSlider, milkSliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);
  createLabel(formMilkCard, "10 ML", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 8, 54);
  createLabel(formMilkCard, "120 ML", COLOR_MUTED, LV_ALIGN_TOP_RIGHT, -8, 54);
  // Rzad przyciskow w obszarze wewn. karty (428 px). Szer. 196, odstep 20, marginesy 8:
  // MATKI x=8..204, MODYFIKOWANE x=224..420 — z zapasem ~8 px do brzegu (na cien przyciskow).
  formMilkMatkiButton = createButton(formMilkCard, "MATKI", 8, 80, 196, 34, COLOR_GREEN);
  lv_obj_add_event_cb(formMilkMatkiButton, milkMatkiEvent, LV_EVENT_CLICKED, nullptr);
  formMilkModifiedButton = createButton(formMilkCard, "MODYFIKOWANE", 224, 80, 196, 34, COLOR_ORANGE);
  lv_obj_add_event_cb(formMilkModifiedButton, milkModifiedEvent, LV_EVENT_CLICKED, nullptr);

  formStatusLabel = createLabel(formScreen, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 25, 384);
  lv_obj_set_width(formStatusLabel, 430);
  lv_label_set_long_mode(formStatusLabel, LV_LABEL_LONG_WRAP);
  formSaveButton = createButton(formScreen, "ZAPISZ KARMIENIE", 14, 330, 290, 50, COLOR_ORANGE);
  lv_obj_add_event_cb(formSaveButton, saveFormEvent, LV_EVENT_CLICKED, nullptr);
  formCancelButton = createButton(formScreen, "ANULUJ", 316, 330, 150, 50, COLOR_MUTED);
  lv_obj_add_event_cb(formCancelButton, cancelFormEvent, LV_EVENT_CLICKED, nullptr);

  updateFormValues();
  updateExtraMilkVisibility();
  loadReusableScreen(formScreen);
}

void openEntryForm() {
  formReturnToCalendar = false;
  selectedMilkMl = DEFAULT_ML;
  extraMilkEnabled = false;
  extraMilkModified = false;
  piersLeftCtl.value = 0;
  piersRightCtl.value = 0;
  selectedEntryTime = time(nullptr);
  createFormScreen();
}

void openEntryFormForDay(time_t day) {
  formReturnToCalendar = true;
  selectedCalendarDay = beginningOfDay(day);
  selectedMilkMl = DEFAULT_ML;
  extraMilkEnabled = false;
  extraMilkModified = false;
  piersLeftCtl.value = 0;
  piersRightCtl.value = 0;

  struct tm dayInfo;
  localtime_r(&selectedCalendarDay, &dayInfo);
  dayInfo.tm_hour = 12;
  dayInfo.tm_min = 0;
  dayInfo.tm_sec = 0;
  dayInfo.tm_isdst = -1;
  selectedEntryTime = mktime(&dayInfo);
  createFormScreen();
}

// ----------------------- Usługi okresowe i opcjonalne ---------------------------
// Dzienna kopia danych: pierwsze wywołanie po północy przepisuje plik do BACKUP_FILE_PATH.
bool appendBackupIfDue() {
  if (!storageReady) return false;
  struct tm nowInfo;
  if (!currentLocalTime(nowInfo)) return false;
  const long stamp = static_cast<long>((nowInfo.tm_year + 1900) * 10000L + (nowInfo.tm_mon + 1) * 100L + nowInfo.tm_mday);
  if (stamp == lastBackupDayStamp) return false;

  if (!copyLittleFsFile(DATA_FILE_PATH, BACKUP_FILE_PATH)) return false;
  lastBackupDayStamp = stamp;
  Serial.println("Backup: utworzono kopie dzienna.");
  // Automatyczna wysylka backupu na Telegram, o ile nie czeka juz w kolejce.
  // Stan backupu czyta telegramTask (inny rdzen) — zapis pod mutexem.
  bool scheduled = false;
  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (backupState == B_IDLE) {
    backupFileName = buildBackupFileName();
    backupState = B_WANTED;
    scheduled = true;
  }
  if (telegramMutex) xSemaphoreGive(telegramMutex);
  if (scheduled) {
    Serial.println("Backup: zaplanowano wysylke na Telegram.");
    wakeTelegramTask();
  }
  return true;
}

// Co 6 h odświeżamy synchronizację NTP; getLocalTime z zerowym timeoutem nie blokuje.
void resyncNtpIfDue() {
  if (WiFi.status() != WL_CONNECTED || !timeIsValid) return;
  if (millis() - lastNtpSyncMs < NTP_RESYNC_INTERVAL_MS) return;
  lastNtpSyncMs = millis();
  struct tm probe;
  if (getLocalTime(&probe, 0)) return; // zegar działa — nic nie rób
  Serial.println("NTP: ponawiam synchronizacje zegara.");
  configTzTime(TIMEZONE_RULE, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

// Motyw nocny: paleta + jaśniejsze podświetlenie w godzinach NIGHT_*. Po zmianie
// przebudowujemy aktywny ekran, bo kolory są wpisywane do stylów przy budowie.
// Sprawdzanie pory dnia co 30 s — getLocalTime potrafi blokować do 10 ms.
void updateNightMode() {
  static uint32_t lastNightCheck = 0;
  if (millis() - lastNightCheck < 30000) return;
  lastNightCheck = millis();

  struct tm nowInfo;
  bool night = false;
  if (currentLocalTime(nowInfo)) {
    night = nowInfo.tm_hour >= NIGHT_START_HOUR || nowInfo.tm_hour < NIGHT_END_HOUR;
  }
  if (night == nightModeActive) return;
  nightModeActive = night;
  applyTheme(night);
  ledcWrite(BACKLIGHT_PIN, night ? BACKLIGHT_NIGHT_DUTY : BACKLIGHT_FULL_DUTY);
  screenDimmed = false;
  lastUserActivityMillis = millis();
  Serial.printf("Motyw: %s\n", night ? "nocny" : "dzienny");

  lv_obj_t *active = lv_screen_active();
  if (active == homeScreen) createHomeScreen();
  else if (active == formScreen) createFormScreen();
  else if (active == calendarScreen) createCalendarScreen();
  else if (active == dayDetailScreen) createDayDetailScreen(selectedCalendarDay);
  else if (active == chartScreen) createFeedingChartScreen();
  else if (active == pumpingScreen) createPumpingScreen();
  else if (active == diaperScreen) createDiaperScreen();
  else if (active == otherScreen) createOtherScreen();
  else if (active == weightScreen) createWeightScreen();
  else if (active == diagnosticsScreen) createDiagnosticsScreen();
}

// ------------------------------- Telegram ---------------------------------------
String telegramUrlEncode(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) out += c;
    else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

String telegramTextFor(const String &type, int ml, int piersLeft, int piersRight, time_t when) {
  struct tm t;
  localtime_r(&when, &t);
  char hhmm[6];
  strftime(hhmm, sizeof(hhmm), "%H:%M", &t);
  if (type == "KARMIENIE") {
    String s = String("Karmienie ") + hhmm;
    if (piersLeft > 0 || piersRight > 0) s += String(" (L") + piersLeft + "/P" + piersRight + " min)";
    return s;
  }
  if (isMilkType(type)) return milkTypeLabel(type) + String(" ") + hhmm + " - " + ml + " ml";
  if (type == "PIELUCHA_MOKRA") return String("Pielucha mokra ") + hhmm;
  if (type == "PIELUCHA_BRUDNA") return String("Pielucha brudna ") + hhmm;
  if (type == "ODCIAGANIE") return String("Odciaganie ") + hhmm + " - " + ml + " ml";
  if (type == "WITAMINA_D") return String("Witamina D podana ") + hhmm;
  if (type == "WAGA") return String("Waga ") + hhmm + " - " + ml + " g";
  if (type == "SEN_START") return String("Zasnal ") + hhmm;
  if (type == "SEN_STOP") return String("Obudzil sie ") + hhmm;
  return type + " " + hhmm;
}

// Kolejka 1-elementowa: nowszy wpis zastępuje starszy, wysyłka nie blokuje UI
// (realizuje ja telegramTask). Zapis pod mutexem, bo czyta go inny rdzen.
void queueTelegram(const String &text) {
  if (text.length() == 0) return;
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) return;
  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  pendingTelegramText = text;
  if (telegramNextAttemptMs == 0) telegramNextAttemptMs = millis();
  if (telegramMutex) xSemaphoreGive(telegramMutex);
  wakeTelegramTask();
}

// Nazwa pliku backupu: karmienia_YYYY-MM-DD.csv
String buildBackupFileName() {
  struct tm nowInfo;
  if (!currentLocalTime(nowInfo)) return "karmienia_backup.csv";
  char name[32];
  strftime(name, sizeof(name), "karmienia_%Y-%m-%d.csv", &nowInfo);
  return String(name);
}

// Wysyla /karmienia_backup.csv jako dokument przez sendDocument.
// Multipart skladany w locie; tresc pliku streamowana z LittleFS (bez alokacji duzych buforow).
// fileName przekazywany przez wartosc (snapshot zrobiony pod mutexem w wolajacym),
// aby NIE czytac globalnego backupFileName z innego rdzenia bez synchronizacji.
bool sendBackupViaTelegram(const String &fileName) {
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) {
    Serial.println("Telegram: backup pominieto — brak tokenu lub chat_id.");
    return false;
  }
  File file = LittleFS.open(BACKUP_FILE_PATH, FILE_READ);
  if (!file) {
    Serial.println("Telegram: brak pliku backupu do wysylki.");
    return false;
  }
  const size_t fileSize = file.size();

  feedWatchdog(); // wysylka dokumentu przez TLS trwa dluzej — chronimy watchdog
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(8000);
  client.setTimeout(8000);
  Serial.printf("Telegram backup: freeHeap=%u maxAlloc=%u przed TLS\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (!client.connect("api.telegram.org", 443)) {
    char lastErrBuf[128];
    const int errCode = client.lastError(lastErrBuf, sizeof(lastErrBuf));
    Serial.printf("Telegram: backup — blad TLS (kod=%d msg=%s)\n", errCode, lastErrBuf);
    file.close();
    return false;
  }
  Serial.println("Telegram: TLS handshake do telegram OK.");

  const String boundary = String("----karmienia") + String(millis());
  String head = String("--") + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
                TELEGRAM_CHAT_ID + "\r\n";
  head += String("--") + boundary + "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" +
          fileName + "\"\r\nContent-Type: text/csv\r\n\r\n";
  const String tail = String("\r\n--") + boundary + "--\r\n";
  const size_t bodyLen = head.length() + fileSize + tail.length();

  client.printf("POST /bot%s/sendDocument HTTP/1.1\r\nHost: api.telegram.org\r\n"
                "User-Agent: ESP32\r\nContent-Type: multipart/form-data; boundary=%s\r\n"
                "Content-Length: %u\r\nConnection: close\r\n\r\n",
                TELEGRAM_BOT_TOKEN, boundary.c_str(), static_cast<unsigned>(bodyLen));
  client.print(head);

  uint8_t chunk[512];
  while (file.available()) {
    const size_t n = file.read(chunk, sizeof(chunk));
    if (n == 0) break;
    client.write(chunk, n);
  }
  client.print(tail);
  client.flush();

  // Czytamy kod odpowiedzi HTTP.
  unsigned long waitUntil = millis() + 6000;
  String statusLine;
  while (!client.available() && millis() < waitUntil) delay(10);
  if (client.available()) statusLine = client.readStringUntil('\n');
  int code = 0;
  if (statusLine.startsWith("HTTP/1.") && statusLine.length() >= 12) {
    code = statusLine.substring(9, 12).toInt();
  }
  while (client.available()) client.read();
  client.stop();
  file.close();
  feedWatchdog();

  if (code == 200) {
    Serial.println("Telegram: backup wyslany.");
    return true;
  }
  Serial.printf("Telegram: backup wysylka nieudana (kod %d).\n", code);
  return false;
}

// Jedna proba obslugi kolejki Telegrama. Wywolywana WYLACZNIE z telegramTask
// (rdzen 0) — blokujace operacje TLS nie dotykaja loop()/LVGL/dotyku. Dostep do
// wspoldzielonego stanu (pendingTelegramText/backupState/backupFileName) jest
// pod telegramMutex; sama wysylka TLS biegnie bez trzymania mutexa.
void pumpTelegramQueue() {
#if !FEATURE_HTTPCLIENT
  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (backupState != B_IDLE) backupState = B_IDLE;
  if (pendingTelegramText.length() > 0) {
    Serial.println("Telegram: HTTPClient niedostepny w tym rdzeniu — powiadomienie pominiete.");
    pendingTelegramText = "";
  }
  if (telegramMutex) xSemaphoreGive(telegramMutex);
  return;
#else
  if (static_cast<int32_t>(millis() - telegramNextAttemptMs) < 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    telegramNextAttemptMs = millis() + 15000;
    return;
  }

  // --- Migawka stanu pod mutexem: decydujemy, co wyslac, bez trzymania go w TLS ---
  bool doBackup = false;
  String textToSend;
  String backupNameSnapshot; // kopia nazwy pliku pod mutexem (nie czytamy globalu w TLS)
  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (backupState == B_WANTED || backupState == B_SENDING) {
    backupState = B_SENDING;
    doBackup = true;
    backupNameSnapshot = backupFileName;
  } else if (pendingTelegramText.length() > 0) {
    textToSend = pendingTelegramText; // kopia lokalna do wyslania
  }
  if (telegramMutex) xSemaphoreGive(telegramMutex);

  // Priorytet: backup (dokument) przed zwykla wiadomoscia.
  if (doBackup) {
    const bool ok = sendBackupViaTelegram(backupNameSnapshot);
    if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
    if (ok) {
      // Tylko jesli w miedzyczasie nie zaplanowano nowego backupu.
      if (backupState == B_SENDING) backupState = B_IDLE;
      telegramNextAttemptMs = millis() + 10000;
    } else {
      backupState = B_WANTED; // ponawiamy za 30 s
      telegramNextAttemptMs = millis() + 30000;
    }
    if (telegramMutex) xSemaphoreGive(telegramMutex);
    return;
  }

  if (textToSend.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure(); // autoryzacją jest sam token bota
  client.setTimeout(8000);
  HTTPClient http;
  const String url = String("/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  if (!http.begin(client, "api.telegram.org", 443, url)) {
    Serial.println("Telegram: http.begin() nieudany.");
    telegramNextAttemptMs = millis() + 30000;
    return;
  }
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body = "chat_id=" + String(TELEGRAM_CHAT_ID) +
                      "&text=" + telegramUrlEncode(textToSend);
  const int code = http.POST(body);
  http.end();

  if (telegramMutex) xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (code == 200) {
    // Usuwamy z kolejki tylko jesli w miedzyczasie nie doszla NOWSZA wiadomosc.
    if (pendingTelegramText == textToSend) pendingTelegramText = "";
    telegramFailCount = 0;
    Serial.println("Telegram: wiadomosc wyslana.");
  } else {
    // Niepowodzenie — ponawiamy za 30 s, ale po TELEGRAM_MAX_FAILS z rzedu
    // porzucamy wpis, zeby nie blokowac kolejki w nieskonczonosc.
    ++telegramFailCount;
    if (telegramFailCount >= TELEGRAM_MAX_FAILS) {
      Serial.printf("Telegram: %u nieudanych prob — porzucam wpis.\n", (unsigned)telegramFailCount);
      if (pendingTelegramText == textToSend) pendingTelegramText = "";
      telegramFailCount = 0;
      telegramNextAttemptMs = millis() + 60000;
    } else {
      Serial.printf("Telegram: wysylka nieudana (kod %d). Ponowka za 30 s (%u/%u).\n",
                    code, (unsigned)telegramFailCount, (unsigned)TELEGRAM_MAX_FAILS);
      telegramNextAttemptMs = millis() + 30000;
    }
  }
  if (telegramMutex) xSemaphoreGive(telegramMutex);
#endif
}

// Zadanie FreeRTOS obslugujace wysylke Telegrama poza loop(). Budzi sie na
// notyfikacje (nowy wpis w kolejce) lub co 5 s (obsluga retry/backupu), po czym
// wykonuje jedna probe wysylki. Blokujacy TLS zyje tu, nie w watku UI.
void telegramTask(void *parameter) {
  (void)parameter;
  for (;;) {
    // Timeout 5 s: nawet bez notyfikacji obsluzymy zaplanowane ponowienia/backup.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    pumpTelegramQueue();
  }
}

// Budzi zadanie Telegrama (np. po dodaniu wpisu do kolejki). Bezpieczne gdy task
// jeszcze nie istnieje — wtedy obsluzy sie przy najblizszym cyklu (timeout).
void wakeTelegramTask() {
  if (telegramTaskHandle) xTaskNotifyGive(telegramTaskHandle);
}

// Wiadomosc startowa z pelnym statusem urzadzenia (kolejka, wysylka nieblokujaca).
void queueTelegramStartup() {
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) return;
  const WeatherState w = snapshotWeather();
  String msg = String("Leśny Dziennik uruchomiony\n") +
               String("Czas: ") + (timeIsValid ? String(formatDateTime(time(nullptr))) : "brak NTP") + "\n" +
               String("IP: ") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "brak Wi-Fi") + "\n" +
               String("RAM: ") + String(ESP.getFreeHeap() / 1024) + " KB / " + String(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024) + " KB\n" +
               String("PSRAM: ") + String(ESP.getFreePsram() / 1024) + " KB / " + String(ESP.getPsramSize() / 1024) + " KB\n" +
               String("Pamiec: ") + (storageReady ? "gotowa" : "BLAD") + "\n" +
               String("Pogoda: ") + (w.valid ? String(w.tempNow) + " C" : "brak danych");
  queueTelegram(msg);
  Serial.println("Telegram: wiadomosc startowa w kolejce.");
}

// mDNS + OTA (wariant a: nadpisywanie partycji factory).
void initOptionalServices() {
#if FEATURE_MDNS
  if (MDNS.begin("karmienie")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://karmienie.local gotowe.");
  } else {
    Serial.println("mDNS: start nieudany.");
  }
#else
  Serial.println("mDNS: niedostepne w tym rdzeniu (brak ESPmDNS.h).");
#endif

#if FEATURE_OTA
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
#else
  Serial.println("OTA: niedostepne w tym rdzeniu (brak ArduinoOTA.h).");
#endif

  // Telegram: log o starcie oraz wiadomosc startowa z pelnym statusem.
  if (strlen(TELEGRAM_BOT_TOKEN) > 0 && strlen(TELEGRAM_CHAT_ID) > 0) {
    Serial.println("Telegram: uruchomiony (token i chat_id skonfigurowane).");
    queueTelegramStartup();
  } else {
    Serial.println("Telegram: wylaczone (brak tokenu lub chat_id w config.h).");
  }
}

// Odswieza licznik "temu" i zegar na ekranie glownym co 30 s (bez pelnego odrysu).
void agingTickCb(lv_timer_t *timer) {
  (void)timer;
  if (otaInProgress) return;
  if (homeScreen && lv_screen_active() == homeScreen) updateHomeInformation();
}

// --------------------------- Wygaszacz ekranu i pogoda --------------------------
// (enum WeatherKind zadeklarowany na górze pliku — wymóg prototypów Arduino)

WeatherKind weatherKindOf(int wmoCode) {
  if (wmoCode == 0) return W_SUN;
  if (wmoCode == 1 || wmoCode == 2) return W_PARTLY;
  if (wmoCode == 3) return W_CLOUD;
  if (wmoCode == 45 || wmoCode == 48) return W_FOG;
  if ((wmoCode >= 51 && wmoCode <= 67) || (wmoCode >= 80 && wmoCode <= 82)) return W_RAIN;
  if (wmoCode >= 71 && wmoCode <= 77) return W_SNOW;
  if (wmoCode >= 95) return W_STORM;
  return W_CLOUD;
}

const char *weatherDescPL(WeatherKind kind) {
  switch (kind) {
    case W_SUN: return "Slonce";
    case W_PARTLY: return "Czesciowe zachmurzenie";
    case W_CLOUD: return "Pochmurno";
    case W_FOG: return "Mgla";
    case W_RAIN: return "Deszcz";
    case W_SNOW: return "Snieg";
    case W_STORM: return "Burza";
  }
  return "";
}

// Ikona pogody skladana z prymitywow LVGL — ulepszone ksztalty, wiecej detali.
// Wszystkie wspolrzedne wzgledem boxa 100x100.
void drawWeatherIcon(lv_obj_t *box, int wmoCode) {
  if (!box) return;
  lv_obj_clean(box);

  auto dot = [&](int cx, int cy, int r, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(box);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  };
  auto pill = [&](int cx, int cy, int len, int th, int angleDeg, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(box);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, len, th);
    lv_obj_set_pos(o, cx - len / 2, cy - th / 2);
    lv_obj_set_style_radius(o, th / 2, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    if (angleDeg) lv_obj_set_style_transform_angle(o, angleDeg * 10, 0);
  };

  // Zaokraglony prostokat (do plaskiej podstawy chmury i korpusu).
  auto roundRect = [&](int x, int y, int w, int h, int r, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(box);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  };

  const bool night = nightModeActive;
  const lv_color_t cSun = lv_color_hex(night ? 0xE8CF55 : 0xF3A72E);
  const lv_color_t cSunGlow = lv_color_hex(night ? 0x5A5326 : 0xFBE0A6);
  const lv_color_t cCloud = night ? lv_color_hex(0xB9C6D6) : lv_color_hex(0xE4ECF5);
  const lv_color_t cCloudEdge = night ? lv_color_hex(0x8FA0B4) : lv_color_hex(0xC3D2E2);
  const lv_color_t cRain = lv_color_hex(night ? 0x7C9BD1 : 0x4E7FC4);
  const lv_color_t cYellow = lv_color_hex(0xF2C438);
  const lv_color_t cFog = COLOR_MUTED;
  const lv_color_t cWhite = night ? lv_color_hex(0xE8EFF7) : lv_color_white();

  // Puszysta chmura z plaska podstawa: cieniowany obrys + jasniejsze wypelnienie.
  auto cloud = [&](int baseY) {
    // cien/obrys
    dot(32, baseY - 6, 15, cCloudEdge);
    dot(54, baseY - 16, 19, cCloudEdge);
    dot(72, baseY - 8, 16, cCloudEdge);
    roundRect(18, baseY - 2, 60, 18, 9, cCloudEdge);
    // wypelnienie (lekko wyzej, tworzy delikatny gradient warstwowy)
    dot(33, baseY - 8, 12, cCloud);
    dot(54, baseY - 18, 16, cCloud);
    dot(71, baseY - 10, 13, cCloud);
    roundRect(20, baseY - 3, 56, 14, 7, cCloud);
  };

  switch (weatherKindOf(wmoCode)) {
    case W_SUN: {
      // 12 promieni (naprzemiennie dlugie/krotkie) wokol tarczy
      for (int i = 0; i < 12; ++i) {
        const float rad = i * 30.0f * 3.14159f / 180.0f;
        const int len = (i % 2 == 0) ? 16 : 10;
        const int cx = 48 + static_cast<int>(cosf(rad) * 34.0f);
        const int cy = 48 + static_cast<int>(sinf(rad) * 34.0f);
        pill(cx, cy, len, 5, static_cast<int>(i * 30.0f), cSun);
      }
      // Miekka poswiata + tarcza
      dot(48, 48, 26, cSunGlow);
      dot(48, 48, 20, cSun);
      break;
    }
    case W_PARTLY: {
      // Slonce w gornym-lewym rogu z krotkimi promieniami
      for (int i = 0; i < 8; ++i) {
        const float rad = i * 45.0f * 3.14159f / 180.0f;
        const int cx = 34 + static_cast<int>(cosf(rad) * 22.0f);
        const int cy = 32 + static_cast<int>(sinf(rad) * 22.0f);
        pill(cx, cy, 9, 4, static_cast<int>(i * 45.0f), cSun);
      }
      dot(34, 32, 15, cSunGlow);
      dot(34, 32, 11, cSun);
      // Chmura zaslaniajaca dolna-prawa czesc
      cloud(64);
      break;
    }
    case W_CLOUD: {
      // Dwie chmury (mniejsza z tylu ciemniejsza, wieksza z przodu jasna)
      dot(64, 40, 13, cCloudEdge);
      dot(78, 46, 11, cCloudEdge);
      roundRect(54, 40, 34, 14, 7, cCloudEdge);
      cloud(58);
      break;
    }
    case W_FOG: {
      // Chmura + poziome smugi mgly
      cloud(46);
      for (int i = 0; i < 3; ++i) {
        const int y = 62 + i * 11;
        const int len = (i == 1) ? 64 : 52;
        pill(48 + (i == 2 ? -6 : 4), y, len, 5, 0, cFog);
      }
      break;
    }
    case W_RAIN: {
      cloud(44);
      // 4 krople: kropla = kula + ostry czubek (pill pochylony)
      static const int dx[4] = {30, 46, 62, 76};
      for (int i = 0; i < 4; ++i) {
        const int x = dx[i], y = 66 + (i % 2) * 8;
        pill(x, y, 5, 12, 18, cRain);
        dot(x + 1, y + 5, 3, cRain);
      }
      break;
    }
    case W_SNOW: {
      cloud(44);
      // 3 platki: 3 skrzyzowane belki + jasny srodek
      static const int fx[3] = {34, 54, 74};
      static const int fy[3] = {70, 78, 70};
      for (int i = 0; i < 3; ++i) {
        pill(fx[i], fy[i], 12, 3, 0, cWhite);
        pill(fx[i], fy[i], 12, 3, 60, cWhite);
        pill(fx[i], fy[i], 12, 3, 120, cWhite);
        dot(fx[i], fy[i], 2, cWhite);
      }
      break;
    }
    case W_STORM: {
      cloud(42);
      // Blyskawica: gruby zygzak z kilku nakladajacych sie belek
      pill(50, 60, 20, 8, 115, cYellow);
      pill(44, 74, 16, 8, 60, cYellow);
      pill(54, 82, 14, 8, 118, cYellow);
      dot(48, 71, 4, cYellow);
      break;
    }
  }
}

void updateScreensaverContent() {
  if (!screensaverActive || !ssClockLabel) return;

  const WeatherState currentWeather = snapshotWeather();

  struct tm nowInfo;
  if (currentLocalTime(nowInfo)) {
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &nowInfo);
    setLabelTextIfChanged(ssClockLabel, ssRenderedClock, String(buf));
    // Krotki dzien tygodnia + data w JEDNEJ linii (pod zegarem), np. "PON 05.09".
    static const char *DAYS_SHORT[] = {"NIEDZ", "PON", "WT", "SR", "CZW", "PT", "SOB"};
    char dbuf[8];
    strftime(dbuf, sizeof(dbuf), "%d.%m", &nowInfo);
    setLabelTextIfChanged(ssDateLabel, ssRenderedDate,
                          String(DAYS_SHORT[nowInfo.tm_wday]) + " " + dbuf);
    // Jedna linia karmienia: stan + godzina nastepnego (ostatnie + 4h) w nawiasie.
    String feedingText;
    lv_color_t feedingColor = COLOR_MUTED;
    if (lastFeedingTime) {
      const long elapsedMin = static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60);
      const String nextClk = nextFeedingClock();
      const String suffix = nextClk.length() ? (" (nast. ~" + nextClk + ")") : String();
      if (elapsedMin >= 0 && elapsedMin < COUNTER_BLINK_MIN) {
        feedingText = "Ostatnie: " + formatAgoText(lastFeedingTime) + suffix;
      } else {
        feedingText = "Czas na karmienie!" + suffix;
      }
      feedingColor = feedingAgeColor(lastFeedingTime);
    } else {
      feedingText = "Brak wpisu karmienia";
    }
    setLabelTextIfChanged(ssLastFeedingLabel, ssRenderedLastFeeding, feedingText);
    if (ssLastFeedingLabel) lv_obj_set_style_text_color(ssLastFeedingLabel, feedingColor, 0);
    // Drugi wiersz: informacja o drzemce (osobna etykieta pod linia karmienia).
    String sleepLine;
    lv_color_t sleepColor = COLOR_MUTED;
    if (sleepInProgress && sleepStartedTime) {
      const long minS = static_cast<long>(difftime(time(nullptr), sleepStartedTime) / 60);
      sleepLine = "Spi: " + formatDurationShort(minS);
    } else if (lastWakeTime > 0) {
      const WakeWindow ww = wakeWindowMinutes(calculateAgeDays());
      const time_t napStart = lastWakeTime + static_cast<time_t>(ww.minMin) * 60;
      const time_t napEnd = lastWakeTime + static_cast<time_t>(ww.maxMin) * 60;
      const time_t nowS = time(nullptr);
      if (nowS < napStart) sleepLine = "Drzemka ~" + formatDateTime(napStart).substring(12, 17);
      else if (nowS <= napEnd) { sleepLine = "Czas na drzemke"; sleepColor = COLOR_YELLOW; }
      else { sleepLine = "Przekroczone okno czuwania"; sleepColor = COLOR_RED; }
    }
    setLabelTextIfChanged(ssSleepLabel, ssRenderedSleep, sleepLine);
    if (ssSleepLabel) lv_obj_set_style_text_color(ssSleepLabel, sleepColor, 0);
    // Statystyki dnia: liczba karmien, najdluzsza i srednia przerwa.
    // Odswiezamy je NA BIEZACO — przeliczamy rytm co ~30 s (nie tylko przy zapisie),
    // aby "srednia" i "najdluzsza przerwa" uwzglednialy uplyw czasu od ostatniego
    // karmienia. Liczba karmien z dayStats (jeden wspolny licznik z kalendarzem/WWW).
    static uint32_t ssStatsLastRecalc = 0;
    if (ssStatsLastRecalc == 0 || millis() - ssStatsLastRecalc >= 30000) {
      recomputeFeedingRhythm();
      ssStatsLastRecalc = millis();
    }
    DaySummary ssToday;
    dayStats(dayOffsetFromToday(0), ssToday);
    // Biezaca (otwarta) przerwa od ostatniego karmienia — pokazujemy najwieksza
    // z dotychczasowych i tej trwajacej, aby wartosc rosla w czasie na oczach.
    int shownLongest = longestFeedingGapMin;
    int shownAvg = avgFeedingGapMin;
    if (lastFeedingTime) {
      const int openGap = static_cast<int>(difftime(time(nullptr), lastFeedingTime) / 60);
      if (openGap > shownLongest) shownLongest = openGap;
    }
    setLabelTextIfChanged(ssStatValue[0], ssRenderedStat[0], String(ssToday.feedingCount));
    setLabelTextIfChanged(ssStatValue[1], ssRenderedStat[1], formatGapShort(shownLongest));
    setLabelTextIfChanged(ssStatValue[2], ssRenderedStat[2], formatGapShort(shownAvg));
  } else {
    setLabelTextIfChanged(ssClockLabel, ssRenderedClock, "--:--");
    setLabelTextIfChanged(ssDateLabel, ssRenderedDate, "");
  }

  if (!currentWeather.valid) {
    setLabelTextIfChanged(ssTempLabel, ssRenderedTemp, "--°");
    setLabelTextIfChanged(ssDescLabel, ssRenderedDescription,
                          WiFi.status() == WL_CONNECTED ? "Czekam na dane pogody..." : "Brak polaczenia Wi-Fi");
    setLabelTextIfChanged(ssMinMaxLabel, ssRenderedMinMax, "");
    setLabelTextIfChanged(ssDressLabel, ssRenderedDress, "");
    for (uint8_t i = 0; i < 3; ++i) setLabelTextIfChanged(ssHourLabels[i], ssRenderedHours[i], "--h\n--°");
    if (ssLastIconCode != -999) {
      lv_obj_clean(ssIconBox);
      ssLastIconCode = -999;
    }
    return;
  }

  const WeatherKind kind = weatherKindOf(currentWeather.codeNow);
  setLabelTextIfChanged(ssTempLabel, ssRenderedTemp,
    String(currentWeather.tempNow >= 0 ? "+" : "") + currentWeather.tempNow +
    "° (odcz. " + (currentWeather.feelsLike >= 0 ? "+" : "") + currentWeather.feelsLike + "°)");
  setLabelTextIfChanged(ssDescLabel, ssRenderedDescription, weatherDescPL(kind));
  setLabelTextIfChanged(ssMinMaxLabel, ssRenderedMinMax,
                        String("MIN ") + currentWeather.tempMinDay + "°   MAX " + currentWeather.tempMaxDay + "°");
  setLabelTextIfChanged(ssDressLabel, ssRenderedDress, dressingAdviceFor(currentWeather.tempNow));
  for (uint8_t i = 0; i < 3; ++i) {
    const auto &h = currentWeather.next[i];
    String hourText = "--:--\n--°";
    if (h.has) {
      char hhmm[7];
      snprintf(hhmm, sizeof(hhmm), "%02u:%02u", h.hour, h.minute);
      hourText = String(hhmm) + "\n" + (h.temp >= 0 ? "+" : "") + h.temp + "°";
    }
    setLabelTextIfChanged(ssHourLabels[i], ssRenderedHours[i], hourText);
  }
  if (ssLastIconCode != currentWeather.codeNow) {
    ssLastIconCode = currentWeather.codeNow;
    drawWeatherIcon(ssIconBox, currentWeather.codeNow);
  }
}

void applyScreensaverVisibility() {
  lv_obj_t *controls[] = {feedingCard, milkCard, feedFormButton,
                          sleepHomeButton, otherHomeButton, weightHomeButton, calendarButton, chartButton,
                          homeCounterBar};
  for (lv_obj_t *o : controls) {
    if (!o) continue;
    if (screensaverActive) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t *widgets[] = {ssClockCard, ssWeatherCard, ssDayBandCard,
                         ssClockLabel, ssDateLabel, ssIconBox,
                         ssTempLabel, ssDescLabel, ssMinMaxLabel, ssDressLabel,
                         ssLastFeedingLabel, ssSleepLabel,
                         ssStatValue[0], ssStatValue[1], ssStatValue[2],
                         ssHourLabels[0], ssHourLabels[1], ssHourLabels[2]};
  for (lv_obj_t *o : widgets) {
    if (!o) continue;
    if (screensaverActive) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  // ssClockShadowLabel pozostaje ukryty w obu trybach (plaski zegar w nowym ukladzie).
  if (ssClockShadowLabel) lv_obj_add_flag(ssClockShadowLabel, LV_OBJ_FLAG_HIDDEN);
}

void enterScreensaver() {
  // Idempotentne: wywolanie przy starcie (przed dotknieciem) rowniez tworzy timer.
  screensaverActive = true;
  applyScreensaverVisibility();
  updateScreensaverContent();
  if (!weatherValidNow() || millis() - weatherLastTryMs > WEATHER_STALE_MS) {
    weatherFetchPending = true;
  }
  if (!ssClockTimer) {
    // 5 s: zegar, licznik "temu" i statystyki odswiezaja sie plynnie na wygaszaczu.
    ssClockTimer = lv_timer_create([](lv_timer_t *) { updateScreensaverContent(); }, 5000, nullptr);
  }
}

void exitScreensaver() {
  if (!screensaverActive) return;
  screensaverActive = false;
  if (ssClockTimer) {
    lv_timer_del(ssClockTimer);
    ssClockTimer = nullptr;
  }
  applyScreensaverVisibility();
}

// Minimalny parser JSON bez bibliotek: liczba po kluczu oraz n-ty element
// tablicy liczbowej (wartosci proste/null) w sekcji od searchFrom.
float jsonNumberAfter(const String &body, const char *key, int searchFrom) {
  int k = body.indexOf(key, searchFrom);
  if (k < 0) return NAN;
  k += static_cast<int>(strlen(key));
  while (k < static_cast<int>(body.length()) && (body[k] == ' ' || body[k] == ':')) ++k;
  int end = body.indexOf(',', k);
  const int endBrace = body.indexOf('}', k);
  if (end < 0 || (endBrace >= 0 && endBrace < end)) end = endBrace;
  if (end < 0) end = static_cast<int>(body.length());
  return body.substring(k, end).toFloat();
}

bool jsonArrayNumberAt(const String &body, const char *key, int index, int searchFrom, float &out) {
  int pos = body.indexOf(key, searchFrom);
  if (pos < 0) return false;
  pos = body.indexOf('[', pos);
  if (pos < 0) return false;
  ++pos;
  for (int i = 0; i < index; ++i) {
    const int comma = body.indexOf(',', pos);
    const int close = body.indexOf(']', pos);
    if (comma < 0 || (close >= 0 && close < comma)) return false;
    pos = comma + 1;
  }
  int end = body.indexOf(',', pos);
  const int close = body.indexOf(']', pos);
  if (end < 0 || (close >= 0 && close < end)) end = close;
  if (end < 0) return false;
  String token = body.substring(pos, end);
  token.trim();
  if (token.length() == 0 || token == "null") return false;
  out = token.toFloat();
  return true;
}

bool saveWeatherCache() {
  if (!storageReady) return false;
  // Snapshot pod mutexem: zapisujemy spojna kopie (weatherState modyfikuje rdzen 0),
  // a plikowe I/O robimy bez trzymania mutexa.
  const WeatherState snap = snapshotWeather();
  if (!snap.valid) return false;
  File file = LittleFS.open(WEATHER_CACHE_FILE, FILE_WRITE);
  if (!file) return false;
  const uint32_t magic = 0x57454131UL; // "WEA1"
  const bool saved = file.write(reinterpret_cast<const uint8_t *>(&magic), sizeof(magic)) == sizeof(magic) &&
                     file.write(reinterpret_cast<const uint8_t *>(&snap), sizeof(snap)) == sizeof(snap);
  file.close();
  return saved;
}

bool loadWeatherCache() {
  if (!storageReady) return false;
  File file = LittleFS.open(WEATHER_CACHE_FILE, FILE_READ);
  if (!file || file.size() != sizeof(uint32_t) + sizeof(WeatherState)) {
    if (file) file.close();
    return false;
  }
  uint32_t magic = 0;
  WeatherState cached = {};
  const bool loaded = file.read(reinterpret_cast<uint8_t *>(&magic), sizeof(magic)) == sizeof(magic) &&
                      file.read(reinterpret_cast<uint8_t *>(&cached), sizeof(cached)) == sizeof(cached) &&
                      magic == 0x57454131UL && cached.valid;
  file.close();
  if (!loaded) return false;
  weatherState = cached;
  return true;
}

// ------------------------------- Ustawienia trwale ------------------------------
// Prosty plik "klucz=wartosc" na LittleFS. Na razie jeden klucz: sleepTelegram.
// Latwo rozszerzalny o kolejne ustawienia w przyszlosci.
bool saveSettings() {
  if (!storageReady) return false;
  File file = LittleFS.open(SETTINGS_FILE, FILE_WRITE);
  if (!file) return false;
  file.printf("sleepTelegram=%d\n", sleepTelegramEnabled ? 1 : 0);
  file.close();
  return true;
}

void loadSettings() {
  if (!storageReady) return;
  File file = LittleFS.open(SETTINGS_FILE, FILE_READ);
  if (!file) return; // brak pliku = wartosci domyslne
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    const int eq = line.indexOf('=');
    if (eq <= 0) continue;
    const String key = line.substring(0, eq);
    const String val = line.substring(eq + 1);
    if (key == "sleepTelegram") sleepTelegramEnabled = (val.toInt() != 0);
  }
  file.close();
}

// ----------------------------- Pogoda Open-Meteo (HTTP, port 80) -----------------
// Darmowe, bez klucza API, dane ECMWF, temp odczuwalna.
void fetchWeatherNow() {
#if !FEATURE_HTTPCLIENT
  return;
#else
  if (weatherBusyFlag || WiFi.status() != WL_CONNECTED) return;
  if (weatherNextTryMs != 0 && static_cast<int32_t>(millis() - weatherNextTryMs) < 0) return;
  weatherBusyFlag = true;
  weatherLastTryMs = millis();
  weatherNextTryMs = weatherLastTryMs + WEATHER_RETRY_DELAY_MS;

  char url[384];
  const int urlLength = snprintf(url, sizeof(url),
           "/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,weather_code"
           "&hourly=temperature_2m,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min&forecast_days=2&timezone=Europe%%2FWarsaw",
           WEATHER_LAT, WEATHER_LON);
  if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    Serial.println("Pogoda: adres zapytania zostal uciety.");
    weatherBusyFlag = false;
    return;
  }

  bool ok = false;
  String body;
  WiFiClient client;
  client.setTimeout(8000);
  HTTPClient http;
  const String fullUrl = String("http://api.open-meteo.com") + url;
  int httpCode = -1;
  if (http.begin(client, fullUrl)) {
    http.setTimeout(8000);
    http.setConnectTimeout(8000);
    http.useHTTP10(true);
    httpCode = http.GET();
    if (httpCode == 200) {
      // Odczyt strumieniowy do jednego, z gory zarezerwowanego bufora.
      // Rezerwacja eliminuje serie realloc-ow (kazda fragmentowalaby RAM wewnetrzny),
      // a twardy limit chroni przed OOM, gdyby API zwrocilo nietypowo duza odpowiedz.
      constexpr size_t WEATHER_BODY_LIMIT = 12288; // 12 KB — zapytanie na 2 dni miesci sie z zapasem
      const int declaredLen = http.getSize();
      size_t reserveLen = (declaredLen > 0)
                              ? min(static_cast<size_t>(declaredLen) + 1, WEATHER_BODY_LIMIT)
                              : 4096;
      body.reserve(reserveLen);
      WiFiClient *stream = http.getStreamPtr();
      uint8_t chunk[512];
      while (http.connected() && body.length() < WEATHER_BODY_LIMIT) {
        const size_t avail = stream->available();
        if (avail == 0) {
          if (!stream->connected()) break;
          delay(5);
          continue;
        }
        const size_t toRead = min(avail, sizeof(chunk));
        const int n = stream->readBytes(chunk, toRead);
        if (n <= 0) break;
        body.concat(reinterpret_cast<const char *>(chunk), static_cast<size_t>(n));
      }
      Serial.printf("Pogoda: odpowiedz %u B (limit %u B).\n",
                    static_cast<unsigned>(body.length()),
                    static_cast<unsigned>(WEATHER_BODY_LIMIT));
    } else {
      Serial.printf("Pogoda: HTTP %d. Kolejna proba za 60 s.\n", httpCode);
    }
    http.end();
  } else {
    Serial.println("Pogoda: http.begin() nieudany.");
  }

  if (body.length() > 0) {
      int curIdx = body.indexOf("\"current\"");
      if (curIdx < 0) curIdx = 0;

      WeatherState w{};
      float v = jsonNumberAfter(body, "\"temperature_2m\"", curIdx);
      float f = jsonNumberAfter(body, "\"apparent_temperature\"", curIdx);
      const float cVal = jsonNumberAfter(body, "\"weather_code\"", curIdx);

      if (!isnan(v) && !isnan(cVal)) {
        w.tempNow = static_cast<int>(lroundf(v));
        w.feelsLike = isnan(f) ? w.tempNow : static_cast<int>(lroundf(f));
        w.codeNow = static_cast<int>(cVal);

        const int hIdx = body.indexOf("\"hourly\"");
        struct tm n;
        if (hIdx >= 0 && currentLocalTime(n)) {
          for (uint8_t i = 0; i < 3; ++i) {
            float t = NAN, c = NAN;
            const int idx = n.tm_hour + 1 + static_cast<int>(i);
            if (jsonArrayNumberAt(body, "temperature_2m", idx, hIdx, t) &&
                jsonArrayNumberAt(body, "weather_code", idx, hIdx, c)) {
              w.next[i].hour = static_cast<uint8_t>(idx % 24);
              w.next[i].temp = static_cast<int>(lroundf(t));
              w.next[i].has = true;
            }
          }
        }

        const int dIdx = body.indexOf("\"daily\"");
        if (dIdx >= 0 && jsonArrayNumberAt(body, "temperature_2m_min", 0, dIdx, v) && !isnan(v))
          w.tempMinDay = static_cast<int>(lroundf(v));
        else w.tempMinDay = w.tempNow;
        if (dIdx >= 0 && jsonArrayNumberAt(body, "temperature_2m_max", 0, dIdx, v) && !isnan(v))
          w.tempMaxDay = static_cast<int>(lroundf(v));
        else w.tempMaxDay = w.tempNow;

        w.valid = true;
        if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          weatherState = w;
          xSemaphoreGive(weatherMutex);
        } else if (!weatherMutex) {
          weatherState = w;
        } else {
          weatherBusyFlag = false;
          return;
        }
        weatherNextTryMs = millis() + WEATHER_REFRESH_MS;
        weatherDataReady = true;
        Serial.printf("Pogoda OK: %+d C (odcz. %+d C), kod %d.\n",
                      w.tempNow, w.feelsLike, w.codeNow);
        ok = true;
      } else {
        Serial.println("Pogoda: brak danych biezacych w odpowiedzi API.");
      }
  }
  if (!ok) Serial.println("Pogoda: dane nieodswiezone.");
  weatherBusyFlag = false;
#endif
}

// Wszystkie blokujące operacje sieciowe pogody wykonują się poza loop().
// Główna pętla dostaje tylko gotowy wynik przez weatherDataReady.
void weatherTask(void *parameter) {
  (void)parameter;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    weatherFetchPending = false;
    fetchWeatherNow();
  }
}

void requestWeatherFetch() {
  if (weatherTaskHandle) {
    xTaskNotifyGive(weatherTaskHandle);
  } else {
    weatherFetchPending = true;
  }
}

// ------------------------------- Ekran startowy (boot) --------------------------
// Prosty, ladny ekran powitalny z lista krokow inicjalizacji. Pokazywany od razu
// po uruchomieniu LVGL, aktualizowany przy kazdym etapie startu.
constexpr uint8_t BOOT_STEP_COUNT = 5;
lv_obj_t *bootScreen = nullptr;
lv_obj_t *bootStepLabel[BOOT_STEP_COUNT] = {nullptr};
lv_obj_t *bootStatusLabel = nullptr;
const char *BOOT_STEP_NAMES[BOOT_STEP_COUNT] = {
    "Wi-Fi", "Zegar (NTP)", "Pamiec danych", "Pogoda", "Serwer WWW"};

// Wymusza jedno odswiezenie LVGL, aby zmiany byly widoczne mimo blokujacych krokow.
void bootPumpLvgl() {
  const uint32_t nowMs = millis();
  lv_tick_inc(nowMs - lastLvglTickMs);
  lastLvglTickMs = nowMs;
  lv_timer_handler();
}

// Rysuje uroczą buzię niemowlaka z prymitywow LVGL wewnatrz podanego kontenera
// (~104x104). Glowka, kosmyk wlosow, oczy, rumiane policzki, usmiech.
void drawBabyFace(lv_obj_t *box) {
  if (!box) return;
  auto circle = [&](int cx, int cy, int r, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(box);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
  };
  const lv_color_t skin   = lv_color_hex(0xF6C9A8);
  const lv_color_t skinSh = lv_color_hex(0xE8B291);
  const lv_color_t cheek  = lv_color_hex(0xF3A9A0);
  const lv_color_t hair   = lv_color_hex(0x6B4A2B);
  const lv_color_t eye    = lv_color_hex(0x3B2A1E);
  const lv_color_t white  = lv_color_white();

  // Twarz (delikatny cien u dolu + wlasciwa buzia).
  circle(52, 56, 34, skinSh);
  circle(52, 53, 33, skin);
  // Uszy.
  circle(20, 55, 7, skin);
  circle(84, 55, 7, skin);
  // Kosmyk wlosow (mała czuprynka u gory).
  circle(52, 24, 11, hair);
  circle(44, 27, 7, hair);
  circle(60, 27, 7, hair);
  lv_obj_t *curl = circle(52, 20, 4, hair); (void)curl;
  // Oczy (bialko + zrenica) + brwi jako male kreski (kropki).
  circle(40, 50, 6, white);  circle(40, 51, 3, eye);
  circle(64, 50, 6, white);  circle(64, 51, 3, eye);
  // Rumiane policzki.
  circle(33, 63, 5, cheek);
  circle(71, 63, 5, cheek);
  // Nosek.
  circle(52, 60, 3, skinSh);
  // Usmiech: luk z trzech kropek.
  circle(45, 70, 2, eye);
  circle(52, 73, 2, eye);
  circle(59, 70, 2, eye);
}

void showBootScreen() {
  bootScreen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(bootScreen, lv_color_hex(0x27492E), 0); // ciepla zielen lesna
  lv_obj_set_style_bg_opa(bootScreen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(bootScreen, LV_OBJ_FLAG_SCROLLABLE);

  // Logo: okragla "odznaka" z rysowanym bobasem (prymitywy LVGL, wzgledem boxa).
  lv_obj_t *badge = lv_obj_create(bootScreen);
  lv_obj_remove_style_all(badge);
  lv_obj_set_size(badge, 104, 104);
  lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(0xEAF4E4), 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(badge, lv_color_hex(0x7FB88A), 0);
  lv_obj_set_style_border_width(badge, 3, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  drawBabyFace(badge);

  lv_obj_t *title = lv_label_create(bootScreen);
  lv_label_set_text(title, "LESNY DZIENNIK");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 162);

  lv_obj_t *name = lv_label_create(bootScreen);
  lv_label_set_text(name, "ALEKSANDER");
  lv_obj_set_style_text_color(name, lv_color_hex(0xBFE0C4), 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_36, 0);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 182);

  // Karta z lista krokow.
  lv_obj_t *card = lv_obj_create(bootScreen);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 360, 190);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 250);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x2F5638), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  for (uint8_t i = 0; i < BOOT_STEP_COUNT; ++i) {
    bootStepLabel[i] = lv_label_create(card);
    lv_label_set_text_fmt(bootStepLabel[i], "%s  %s", ".", BOOT_STEP_NAMES[i]);
    lv_obj_set_style_text_color(bootStepLabel[i], lv_color_hex(0x9DB3A0), 0);
    lv_obj_set_style_text_font(bootStepLabel[i], &lv_font_montserrat_16, 0);
    lv_obj_align(bootStepLabel[i], LV_ALIGN_TOP_LEFT, 4, 4 + i * 32);
  }

  bootStatusLabel = lv_label_create(bootScreen);
  lv_label_set_text(bootStatusLabel, "Uruchamianie...");
  lv_obj_set_style_text_color(bootStatusLabel, lv_color_hex(0xBFE0C4), 0);
  lv_obj_set_style_text_font(bootStatusLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(bootStatusLabel, LV_ALIGN_BOTTOM_MID, 0, -14);

  lv_screen_load(bootScreen);
  bootPumpLvgl();
}

// state: 0 = w toku (żółty ">"), 1 = OK (zielony "OK"), 2 = pominiete/blad (szary "-").
void bootStep(uint8_t idx, uint8_t state) {
  if (idx >= BOOT_STEP_COUNT || !bootStepLabel[idx]) return;
  const char *mark = state == 1 ? "OK" : (state == 2 ? "--" : ">>");
  lv_color_t col = state == 1 ? lv_color_hex(0x9FE6AC)
                              : (state == 2 ? lv_color_hex(0x7C8C80) : lv_color_hex(0xE8D468));
  lv_label_set_text_fmt(bootStepLabel[idx], "%s  %s", mark, BOOT_STEP_NAMES[idx]);
  lv_obj_set_style_text_color(bootStepLabel[idx], col, 0);
  if (bootStatusLabel && state != 0) {
    // Po ostatnim kroku pokaz "Gotowe".
    if (idx == BOOT_STEP_COUNT - 1) lv_label_set_text(bootStatusLabel, "Gotowe!");
  }
  bootPumpLvgl();
}

// --------------------------------- Uruchomienie ---------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // --- Diagnostyka: powod resetu + liczniki w pamieci RTC (przetrwaja restart) ---
  lastResetReason = static_cast<int>(esp_reset_reason());
  if (rtcMagic != RTC_MAGIC_VALUE) {
    // Pierwsze uruchomienie po utracie zasilania — inicjalizacja licznikow.
    rtcMagic = RTC_MAGIC_VALUE;
    bootCount = 0;
    watchdogResetCount = 0;
  }
  ++bootCount;
  if (lastResetReason == ESP_RST_TASK_WDT || lastResetReason == ESP_RST_INT_WDT ||
      lastResetReason == ESP_RST_WDT) {
    ++watchdogResetCount;
    Serial.println("DIAG: poprzedni restart spowodowany przez WATCHDOG.");
  }
  Serial.printf("DIAG: boot #%u, powod resetu=%d, restartow WDT=%u\n",
                (unsigned)bootCount, lastResetReason, (unsigned)watchdogResetCount);

  Wire.begin(I2C_SDA, I2C_SCL);
  // Fast Mode I2C (400 kHz) dla CALEJ magistrali. Domyslne 100 kHz sprawia, ze kazdy
  // odczyt punktu dotyku (GT911.getPoint) trwa ~4x dluzej — podniesienie zegara skraca
  // pojedyncze odpytanie i wyraznie poprawia responsywnosc dotyku. Uwaga: zegar jest
  // wspolny — dotyczy tez ekspandera XCA9554 (0x20) uzywanego przy starcie panelu ST7701;
  // oba uklady obsluguja Fast Mode. Init panelu wykonuje sie raz przy starcie.
  Wire.setClock(400000);
  Wire.setTimeOut(100);

  // Inwentaryzacja partycji i wolnego miejsca na starcie.
  const esp_partition_t *runningPart = esp_ota_get_running_partition();
  if (runningPart) {
    Serial.printf("Partycja aplikacji: %s @ 0x%06X (%u KB)\n",
                  runningPart->label,
                  static_cast<unsigned>(runningPart->address),
                  static_cast<unsigned>(runningPart->size / 1024));
  }
  const esp_partition_t *fsPart = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (fsPart) {
    Serial.printf("Partycja danych:   %s @ 0x%06X (%u KB)\n",
                  fsPart->label,
                  static_cast<unsigned>(fsPart->address),
                  static_cast<unsigned>(fsPart->size / 1024));
  }
  Serial.printf("Flash: %u MB | Heap wolny: %u B (max blok %u B) | PSRAM wolny: %u B\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / (1024U * 1024U)),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(ESP.getFreePsram()));

  // Panel RGB 480 × 480 potrzebuje PSRAM na bufory obrazu.
  const uint32_t psramSize = ESP.getPsramSize();
  Serial.println(String("PSRAM detected: ") + String(psramSize) + " bytes");
  if (psramSize == 0) {
    Serial.println("ERROR: ustaw Narzedzia > PSRAM > OPI PSRAM i wgraj program ponownie.");
    while (true) delay(1000);
  }

  // Oficjalne demo przed inicjalizacją LCD steruje liniami ekspandera TCA9554.
  initialiseDisplayPanel();
  if (!initialiseNativeRgbPanel()) {
    Serial.println("Blad uruchomienia natywnego panelu RGB");
    while (true) delay(1000);
  }
  initialiseBacklight();

  // Dokładna inicjalizacja dotyku GT911 z oficjalnego przykładu Waveshare LVGL.
  Serial.println("INIT: start GT911");
  GT911.setPins(-1, -1);
  touchReady = GT911.begin(Wire, GT911_SLAVE_ADDRESS_L, I2C_SDA, I2C_SCL);
  Serial.printf("INIT: GT911.begin zakonczone, wynik=%s\n", touchReady ? "OK" : "BLAD");
  if (touchReady) {
    GT911.setMaxTouchPoint(1);
    Serial.println("GT911: dotyk gotowy.");
  } else {
    Serial.println("GT911: nie wykryto kontrolera dotyku.");
  }

  // Początkowa biała klatka natywnego panelu; dalszy ekran startowy i aplikację
  // rysuje już LVGL, aby nie mieszać dwóch właścicieli framebufferu.
  const size_t panelFrameBytes = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color16_t);
  memset(rgbFrameBuffer0, 0xFF, panelFrameBytes);
  esp_lcd_panel_draw_bitmap(rgbPanel, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, rgbFrameBuffer0);
  if (rgbColorTransferDoneSemaphore) {
    xSemaphoreTake(rgbColorTransferDoneSemaphore, pdMS_TO_TICKS(100));
  }

  lv_init();
  Serial.println("INIT: start LVGL");
  // Nie używamy lv_tick_set_cb: w tej konfiguracji globalnego LVGL powodował
  // StoreProhibited zaraz po lv_init. Tick jest inkrementowany ręcznie w loop().
  lastLvglTickMs = millis();
  Serial.println("INIT: tick LVGL ustawiony");

  // Tryb DIRECT: LVGL renderuje wprost do dwoch framebufferow panelu w PSRAM
  // (pobranych z esp_lcd). Brak osobnego bufora czesciowego = mniej zuzycia RAM
  // wewnetrznego i brak kopiowania/tearingu przy krawedzi w displayFlush().
  const size_t fbBytes = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color16_t);
  Serial.printf("INIT: LVGL DIRECT na 2 FB panelu (%p, %p), po %u B, PSRAM wolny %u B\n",
                rgbFrameBuffer0, rgbFrameBuffer1,
                static_cast<unsigned>(fbBytes),
                static_cast<unsigned>(ESP.getFreePsram()));

  displayDriver = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  Serial.println("INIT: display LVGL utworzony");
  lv_display_set_flush_cb(displayDriver, displayFlush);
  lv_display_set_buffers(displayDriver, rgbFrameBuffer0, rgbFrameBuffer1, fbBytes, LV_DISPLAY_RENDER_MODE_DIRECT);

  touchDriver = lv_indev_create();
  Serial.println("INIT: input LVGL utworzony");
  lv_indev_set_type(touchDriver, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touchDriver, touchRead);
  // Tryb EVENT: LVGL NIE tworzy wlasnego timera odczytu dotyku — odpytujemy go
  // wylacznie recznie przez lv_indev_read() w loop() (geste probkowanie miedzy
  // renderami). Bez tego (domyslny MODE_TIMER) lv_timer_handler czytalby dotyk
  // po swojemu, a nasze reczne odczyty dokladalyby sie, mieszajac stan press/click.
  lv_indev_set_mode(touchDriver, LV_INDEV_MODE_EVENT);

  createReusableScreenRoots();
  Serial.println("INIT: korzenie ekranow utworzone");

  // Ekran startowy z logo i lista krokow — pokazywany podczas inicjalizacji.
  showBootScreen();

  // Krok 1: Wi-Fi (blokujace do ~15 s).
  bootStep(0, 0);
  connectWiFi();
  bootStep(0, WiFi.status() == WL_CONNECTED ? 1 : 2);

  // Krok 2: Zegar NTP — start nieblokujacy; na wazny czas czekamy nizej
  // (bootscreen przechodzi dalej dopiero gdy czas jest gotowy lub minie limit).
  bootStep(1, 0);
  beginNtp();
  timeIsValid = waitForNtp(2500); // krotka pierwsza proba; reszta dokonczy sie w tle
  lastNtpSyncMs = millis();
  bootStep(1, timeIsValid ? 1 : 0); // gdy jeszcze brak — zostaw "w toku", dokonczymy po init

  // Krok 3: Pamiec danych (LittleFS).
  bootStep(2, 0);
  storageReady = initialiseStorage();
  if (storageReady) {
    Serial.printf("LittleFS: uzywane %u KB z %u KB.\n",
                  static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                  static_cast<unsigned>(LittleFS.totalBytes() / 1024));
  }
  bootStep(2, storageReady ? 1 : 2);
  loadSettings(); // trwale ustawienia (m.in. powiadomienia snu) — przed uzyciem
  loadLatestEntries();

  // Krok 4: Pogoda (z cache lub do pobrania w tle).
  bootStep(3, 0);
  weatherMutex = xSemaphoreCreateMutex();
  if (!weatherMutex) {
    Serial.println("Pogoda: mutex nieutworzony — tryb bezpieczny bez wspoldzielenia.");
  }
  telegramMutex = xSemaphoreCreateMutex();
  if (!telegramMutex) {
    Serial.println("Telegram: mutex nieutworzony — tryb bezpieczny bez wspoldzielenia.");
  }
  if (loadWeatherCache()) {
    weatherLastTryMs = millis();
    Serial.println("Pogoda: pokazuje ostatnie zapisane dane do czasu odswiezenia.");
    bootStep(3, 1);
  } else {
    weatherFetchPending = true;
    bootStep(3, 2); // brak cache — pobierze sie w tle po starcie
  }
  weatherNextTryMs = millis() + WEATHER_START_DELAY_MS;

  // Krok 5: Serwer WWW + uslugi (mDNS/OTA/Telegram).
  bootStep(4, 0);
  startWebServer();
  initOptionalServices();
  bootStep(4, webServerStarted ? 1 : 2);
  if (xTaskCreatePinnedToCore(weatherTask, "weather", 4096, nullptr, 1,
                              &weatherTaskHandle, 0) != pdPASS) {
    Serial.println("Pogoda: nie mozna uruchomic zadania FreeRTOS.");
    weatherTaskHandle = nullptr;
  }
  // Zadanie wysylki Telegrama (TLS) na rdzeniu 0 — wieksza stertowa ramka na TLS.
  if (xTaskCreatePinnedToCore(telegramTask, "telegram", 8192, nullptr, 1,
                              &telegramTaskHandle, 0) != pdPASS) {
    Serial.println("Telegram: nie mozna uruchomic zadania FreeRTOS — wysylka wylaczona.");
    telegramTaskHandle = nullptr;
  }
  appendBackupIfDue();
  updateNightMode();
  lv_timer_create(agingTickCb, 30000, nullptr);
  counterAlarmTimer = lv_timer_create(counterAlarmTickCb, 500, nullptr);
  lastUiWifiConnected = WiFi.status() == WL_CONNECTED;

  // --- Czekamy az wszystko bedzie gotowe zanim wejdziemy do aplikacji ---
  // Gdy jest Wi-Fi, ale zegar jeszcze niepewny — dajemy NTP czas na synchronizacje
  // (do BOOT_TIME_WAIT_MS). Bez Wi-Fi nie ma na co czekac. Watchdog nieaktywny —
  // ta petla jest bezpieczna, a bootPumpLvgl utrzymuje animacje/odswiezanie ekranu.
  if (WiFi.status() == WL_CONNECTED && !timeIsValid) {
    if (bootStatusLabel) lv_label_set_text(bootStatusLabel, "Synchronizacja zegara...");
    constexpr uint32_t BOOT_TIME_WAIT_MS = 12000;
    const uint32_t waitStart = millis();
    while (!timeIsValid && millis() - waitStart < BOOT_TIME_WAIT_MS) {
      struct tm probe;
      if (getLocalTime(&probe, 100) && probe.tm_year + 1900 >= 2025) {
        timeIsValid = true;
        break;
      }
      bootPumpLvgl();
      delay(150);
    }
    lastNtpSyncMs = millis();
  }
  bootStep(1, timeIsValid ? 1 : 2); // ostateczny status kroku zegara

  // "Gotowe!" tylko gdy krytyczne elementy sa OK (pamiec + czas). Inaczej informacja.
  if (bootStatusLabel) {
    if (storageReady && timeIsValid) lv_label_set_text(bootStatusLabel, "Gotowe!");
    else if (!timeIsValid) lv_label_set_text(bootStatusLabel, "Brak czasu — start mimo to");
    else lv_label_set_text(bootStatusLabel, "Start...");
  }
  bootPumpLvgl();
  delay(700);

  createHomeScreen();
  // Zwolnij ekran startowy (nie jest juz potrzebny) — home jest juz zaladowany.
  if (bootScreen) {
    lv_obj_del(bootScreen);
    bootScreen = nullptr;
    for (uint8_t i = 0; i < BOOT_STEP_COUNT; ++i) bootStepLabel[i] = nullptr;
    bootStatusLabel = nullptr;
  }
  // Start w trybie zegara: czekamy na pierwsze dotkniecie ekranu.
  enterScreensaver();

  // --- Watchdog zadaniowy: pilnuje petli loop() (restart przy zawieszeniu) ---
  // Uruchamiany na koncu setup(), gdy blokujace kroki startowe (WiFi/NTP) sa juz za nami.
  initWatchdog();
}

// Inicjalizacja Task WDT. Rdzen ESP-IDF 5.x (Arduino-ESP32 3.x) uzywa API ze
// struktura konfiguracyjna; starsze rdzenie — sygnatury (timeout_s, panic).
void initWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  // W Arduino-ESP32 3.x WDT bywa juz zainicjalizowany przez rdzen — probujemy
  // najpierw rekonfiguracji, a gdy nieaktywny, pelnej inicjalizacji.
  esp_task_wdt_config_t wdtConfig = {};
  wdtConfig.timeout_ms = WATCHDOG_TIMEOUT_S * 1000;
  wdtConfig.idle_core_mask = 0;     // nie pilnujemy zadan IDLE
  wdtConfig.trigger_panic = true;   // przekroczenie -> panic -> restart
  esp_err_t err = esp_task_wdt_reconfigure(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_init(&wdtConfig);
  }
  watchdogReady = (err == ESP_OK);
#else
  // Starszy rdzen: init przyjmuje (timeout_s, panic).
  watchdogReady = (esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true) == ESP_OK);
#endif
  if (watchdogReady) {
    esp_task_wdt_add(nullptr); // pilnuj biezacego zadania (loopTask)
    esp_task_wdt_reset();
    Serial.printf("DIAG: watchdog aktywny (timeout %us).\n", (unsigned)WATCHDOG_TIMEOUT_S);
  } else {
    Serial.println("DIAG: nie udalo sie uruchomic watchdoga.");
  }
}

// Karmienie watchdoga + aktualizacja lekkiej telemetrii. Wolane co iteracje loop()
// oraz recznie przed/po dlugich operacjach sieciowych (Telegram/pogoda).
void feedWatchdog() {
  if (watchdogReady) esp_task_wdt_reset();
  const uint32_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (freeInt < minFreeHeapEver) minFreeHeapEver = freeInt;
}

void loop() {
#if FEATURE_OTA
  // Podczas transferu OTA wstrzymujemy UI i resztę pętli.
  if (otaInProgress) {
    ArduinoOTA.handle();
    delay(10);
    return;
  }
  ArduinoOTA.handle();
#endif
  const uint32_t loopStart = micros(); // poczatek "pracy" iteracji (do pomiaru CPU load)
  feedWatchdog(); // reset watchdoga + telemetria min. heap w kazdej iteracji
  resyncRgbPanelIfDue(); // profilaktyka dryfu obrazu (restart DMA panelu przy VSYNC)
  updateNightMode();
  // Wysylka Telegrama biegnie w telegramTask (rdzen 0) — nie blokuje juz tej petli.
  resyncNtpIfDue();
  checkSleepNotifications(); // powiadomienia o oknie snu (jesli wlaczone)

  // Powrot na ekran glowny po 30 s bezczynnosci na dowolnym ekranie.
  if (!screensaverActive && !otaInProgress && homeScreen &&
      lv_screen_active() != homeScreen &&
      millis() - lastUserActivityMillis > HOME_RETURN_TIMEOUT_MS) {
    createHomeScreen();
  }

  // Wygaszacz ekranu glownego po bezczynnosci.
  if (!screensaverActive && !otaInProgress && homeScreen &&
      lv_screen_active() == homeScreen &&
      millis() - lastUserActivityMillis > SCREENSAVER_TIMEOUT_MS) {
    enterScreensaver();
  }

  // Pogoda: odswiezanie okresowe lub na zadanie wygaszacza.
  const bool weatherRetryDue = weatherLastTryMs != 0 && millis() - weatherLastTryMs >= WEATHER_RETRY_DELAY_MS;
  const bool weatherRefreshDue = weatherLastTryMs == 0 || millis() - weatherLastTryMs >= WEATHER_REFRESH_MS;
  if ((weatherRefreshDue || weatherRetryDue || weatherFetchPending) &&
      !weatherBusyFlag &&
      (weatherNextTryMs == 0 || static_cast<int32_t>(millis() - weatherNextTryMs) >= 0)) {
    requestWeatherFetch();
  }

  // Wynik zadania trafia do cache i LVGL wyłącznie w głównej pętli.
  if (weatherDataReady) {
    weatherDataReady = false;
    saveWeatherCache();
    if (screensaverActive) updateScreensaverContent();
  }

  // Kontrola jasności nie zmienia drzewka LVGL ani nie wymusza odrysowania RGB.
  updateScreenDimming();

  // Serwer musi być zatrzymany po utracie Wi-Fi i uruchomiony na nowo po uzyskaniu aktualnego IP.
  // Dzięki temu nie pozostaje związany ze starym gniazdem po ponownym połączeniu z routerem.
  if (WiFi.status() != WL_CONNECTED && webServerStarted) {
    webServer.stop();
    webServerStarted = false;
    Serial.println("HTTP: serwer zatrzymany — brak Wi-Fi.");
  }
  if (WiFi.status() == WL_CONNECTED && !webServerStarted) startWebServer();
  if (webServerStarted) {
    const bool hadClient = webServer.client() && webServer.client().connected();
    webServer.handleClient();
    if (hadClient) { ++httpRequestCount; lastHttpMillis = millis(); }
  }

  uint32_t lvglNowMs = millis();
  lv_tick_inc(lvglNowMs - lastLvglTickMs);
  lastLvglTickMs = lvglNowMs;
  lv_timer_handler(); // pelny cykl: render + odczyt dotyku

  // --- Szybkie, geste probkowanie SAMEGO dotyku (T1+T2) ---------------------------
  // Pelny render (powyzej) jest ciezki i rzadki, przez co krotkie tapniecia bywaly
  // gubione. Tu, zamiast jednego sztywnego delay(8), robimy kilka KROTKICH odczytow
  // wylacznie urzadzenia wejsciowego: lv_indev_read() wola touchRead + przetwarza
  // zdarzenie (press/click) BEZ pelnego odrysu ekranu. Dzieki temu GT911 jest
  // odpytywany znacznie czesciej niz render => reakcja na dotyk jest natychmiastowa.
  // Sumaryczna przerwa (~8 ms) zblizona do poprzedniej, ale rozbita na 4 probki.
  const uint32_t busyBeforeDelayUs = micros() - loopStart; // czas pracy przed przerwami
  uint32_t samplingWorkUs = 0; // czas PRACY w petli probkowania (bez delay-ow)
  for (uint8_t s = 0; s < 4; ++s) {
    delay(2);
    const uint32_t workStart = micros();
    // Tick z realnego czasu (bez sztucznego +), zeby nie rozjechal sie zegar LVGL.
    lvglNowMs = millis();
    lv_tick_inc(lvglNowMs - lastLvglTickMs);
    lastLvglTickMs = lvglNowMs;
    if (touchDriver) lv_indev_read(touchDriver); // sam dotyk, bez pelnego renderu
    samplingWorkUs += micros() - workStart;
  }
  const uint32_t afterDelayStartUs = micros();

  // Nie ma okresowego odrysowywania ekranu. Dane widoku zmieniają się tylko po zapisie,
  // wejściu na ekran albo rzeczywistej zmianie stanu Wi-Fi.
  const bool wifiConnectedNow = WiFi.status() == WL_CONNECTED;
  if (wifiConnectedNow != lastUiWifiConnected) {
    lastUiWifiConnected = wifiConnectedNow;
    if (wifiConnectedNow && !timeIsValid) timeIsValid = syncTimeFromNTP();
    if (homeScreen && lv_screen_active() == homeScreen) updateHomeInformation();
  }

  // Co 30 s restartujemy próbę przez reconnect(), bez ponownego przekazywania konfiguracji SSID.
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > WIFI_RECONNECT_INTERVAL_MS) {
    lastReconnectAttempt = millis();
    retryWiFiConnection();
  }

  // CPU load: sumujemy realny czas PRACY iteracji (z wykluczeniem samych delay-ow
  // w petli probkowania), odniesiony do rzeczywistego okna czasu. Im wiecej czasu
  // pracy, tym wyzsze obciazenie. Mierzymy w oknie 5 s.
  const uint32_t busyAfterDelayUs = micros() - afterDelayStartUs;
  cpuBusyUs += busyBeforeDelayUs + samplingWorkUs + busyAfterDelayUs;

  const uint32_t now = millis();
  if (now - cpuCalcLastMs >= 5000) {
    const uint64_t elapsedUs = cpuCalcLastMs == 0 ? 5000000ULL : (static_cast<uint64_t>(now - cpuCalcLastMs) * 1000);
    cpuLoadPct = static_cast<int>((cpuBusyUs * 100) / elapsedUs);
    if (cpuLoadPct > 100) cpuLoadPct = 100;
    cpuBusyUs = 0;
    cpuCalcLastMs = now;
  }
}
