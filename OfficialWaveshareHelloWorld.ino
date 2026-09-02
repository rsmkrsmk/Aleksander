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
lv_obj_t *homeCounterBar = nullptr;
lv_obj_t *ssClockCard = nullptr;
lv_obj_t *ssWeatherCard = nullptr;
lv_obj_t *ssHourLabels[3] = {nullptr, nullptr, nullptr};
int ssLastIconCode = -999;
lv_timer_t *ssClockTimer = nullptr;
String ssRenderedClock;
String ssRenderedDate;
String ssRenderedTemp;
String ssRenderedDescription;
String ssRenderedMinMax;
String ssRenderedDress;
String ssRenderedLastFeeding;
String ssRenderedHours[3];

// Dolna czesci home ukrywane w trybie wygaszacza.
lv_obj_t *feedingCard = nullptr;
lv_obj_t *milkCard = nullptr;
lv_obj_t *feedFormButton = nullptr;
lv_obj_t *diaperButton = nullptr;
lv_obj_t *pumpingHomeButton = nullptr;
lv_obj_t *calendarButton = nullptr;
lv_obj_t *chartButton = nullptr;

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
String pendingTelegramText;         // max 1 wiadomosc w kolejce
uint32_t telegramNextAttemptMs = 0;
// Automatyczny backup przez Telegram: B_IDLE (wolny), B_WANTED (czeka), B_SENDING.
enum BackupState { B_IDLE, B_WANTED, B_SENDING };
BackupState backupState = B_IDLE;
String backupFileName;
bool otaInProgress = false;         // podczas OTA wstrzymujemy odswiezanie LVGL
time_t lastFeedingTime = 0;         // czas ostatniego KARMIENIE (do licznika "temu")
time_t lastMilkTime = 0;
bool deleteModeActive = false;         // tryb wyboru wpisu do usuniecia
int pendingDeleteIndex = -1;            // indeks oczekujacy na potwierdzenie (-1 = brak)

String lastFeeding = "Brak zapisanego wpisu";
String lastMilk = "Brak zapisanego wpisu";

// CPU load estimation: mierzymy czas aktywny vs czas sciany.
static uint64_t cpuBusyUs = 0;
static uint64_t cpuTotalUs = 0;
static uint32_t cpuCalcLastMs = 0;
static int cpuLoadPct = 0;

// Zapamiętane teksty ograniczają odrysowywanie ekranu RGB tylko do faktycznie zmienionych danych.
String renderedClock;
String renderedAge;
String renderedFeeding;
String renderedMilk;
String renderedDevelopment;

lv_obj_t *homeScreen = nullptr;
lv_obj_t *formScreen = nullptr;
lv_obj_t *calendarScreen = nullptr;
lv_obj_t *dayDetailScreen = nullptr;
lv_obj_t *chartScreen = nullptr;
lv_obj_t *pumpingScreen = nullptr;
lv_obj_t *diaperScreen = nullptr;
lv_obj_t *homeLedWifi = nullptr;
lv_obj_t *homeLedMemory = nullptr;
lv_obj_t *homeLedTime = nullptr;
lv_obj_t *homeClockLabel = nullptr;
lv_obj_t *homeAgeLabel = nullptr;
lv_obj_t *homeFeedingLabel = nullptr;
lv_obj_t *homeMilkLabel = nullptr;
lv_obj_t *homeDevelopmentLabel = nullptr;
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
void initialiseDisplayPanel();
void initialiseBacklight();
void setScreenDimmed(bool dimmed);
void registerUserActivity();
void updateScreenDimming();

void connectWiFi();
void retryWiFiConnection();
bool syncTimeFromNTP();
bool initialiseStorage();
void loadLatestEntries();
bool appendEntry(const char *entryType, time_t when, int ml, int piersLeft = -1, int piersRight = -1);
bool isMilkType(const String &entryType);
String milkTypeLabel(const String &entryType);
String entriesForDay(time_t day, bool compact);
void populateDayEntries(lv_obj_t *container, time_t day);
void invalidateDayStats();
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

void startWebServer();
void handleWebRoot();
void handleApiStatus();
void handleApiEntries();
void handleApiEntry();
void handleApiDeleteEntry();
void handleApiSendBackup();
void handleApiEvent();
void handleExportCsv();
void handleApiImport();
void handleWebNotFound();

bool appendBackupIfDue();
String buildBackupFileName();
bool sendBackupViaTelegram();
void resyncNtpIfDue();
void queueTelegram(const String &text);
String telegramTextFor(const String &type, int ml, int piersLeft, int piersRight, time_t when);
void pumpTelegramQueue();
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
                          &pumpingScreen, &diaperScreen};
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
  // 30 linii × 480 = 14400 pikseli; 230400 / 14400 = 16 (calkowite).
  // DMA uzywa 2 buforow bounce: 2 × 30 × 480 × 2 = 57.6 KB.
  config.bounce_buffer_size_px = SCREEN_WIDTH * 30;
  config.sram_trans_align = 8;
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

void touchRead(lv_indev_t *indev, lv_indev_data_t *data) {
  static int16_t lastX = 0;
  static int16_t lastY = 0;

  if (!touchReady) {
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

bool syncTimeFromNTP() {
  if (WiFi.status() != WL_CONNECTED) return false;

  configTzTime(TIMEZONE_RULE, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  struct tm timeInfo;
  for (uint8_t attempt = 0; attempt < 20; ++attempt) {
    if (getLocalTime(&timeInfo, 250)) return true;
    delay(250);
  }
  return false;
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
  if (!storageReady) return;

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) return;

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
    }
    if (isMilkType(entry.type)) {
      lastMilk = formatEntryForUi(line);
      lastMilkTime = stamp;
    }
  }
  file.close();
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

  bool saved;
  if (piersLeft >= 0 || piersRight >= 0) {
    // Nowy format z minutami piersi (wartość -1 oznacza: użyj zera).
    saved = file.printf("%s,%s,%s,%d,%d,%d\n", datePart, timePart, entryType, ml,
                        max(piersLeft, 0), max(piersRight, 0)) > 0;
  } else {
    // Wywołania bez minut (mleko, starsze ścieżki) zostawiają 4 kolumny.
    saved = file.printf("%s,%s,%s,%d\n", datePart, timePart, entryType, ml) > 0;
  }
  file.flush();
  file.close();

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

  File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
  if (!file) return "Nie mozna otworzyc historii";

  // Podsumowanie pochodzi z cache statystyk (jeden przebieg pliku dla wszystkich widoków).
  DaySummary s;
  dayStats(day, s);

  int shown = 0;
  String details;
  const String targetDate = dateIso(day);

  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (!line.startsWith(targetDate + ",")) continue;
    CsvEntry entry;
    if (!parseCsvLine(line, entry)) continue;
    if (!compact || shown < 2) {
      String rowText;
      if (isMilkType(entry.type)) {
        rowText = milkTypeLabel(entry.type) + "  " + String(entry.ml) + " ml";
      } else if (entry.type == "KARMIENIE" && (entry.piersLeft > 0 || entry.piersRight > 0)) {
        rowText = "KARMIENIE  L" + String(entry.piersLeft) + "/P" + String(entry.piersRight);
      } else {
        rowText = entry.type;
      }
      details += entry.time + "  " + rowText + "\n";
      ++shown;
    }
  }
  file.close();

  const String summary = formatDaySummaryLine(s) + "\n" + formatDayExtraLine(s);
  if (s.feedingCount == 0 && s.milkCount == 0) return summary + "\nBrak wpisow";
  if (compact) return summary;
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
  }
  if (storageReady) {
    File file = LittleFS.open(DATA_FILE_PATH, FILE_READ);
    if (file) {
      file.readStringUntil('\n');
      while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        CsvEntry entry;
        if (!parseCsvLine(line, entry)) continue;
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
          }
          break;
        }
      }
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
  payload.reserve(2560);
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
  payload += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  payload += "\"storage\":" + String(storageReady ? "true" : "false") + ",";
  payload += "\"timeValid\":" + String(timeIsValid ? "true" : "false") + ",";
  payload += "\"minMl\":" + String(ML_MIN) + ",";
  payload += "\"maxMl\":" + String(ML_MAX) + ",";
  payload += "\"defaultMl\":" + String(DEFAULT_ML) + ",";
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
  payload += "\"cpuLoad\":" + String(cpuLoadPct) + "}";
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
  String payload = "{\"date\":\"" + targetDate + "\",\"entries\":[";
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
  if (backupState != B_IDLE) {
    sendJson(200, "{\"message\":\"Wysylka backupu juz trwa.\"}");
    return;
  }
  backupFileName = buildBackupFileName();
  backupState = B_WANTED;
  if (telegramNextAttemptMs == 0) telegramNextAttemptMs = millis();
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
                         type == "WITAMINA_D" || type == "ODCIAGANIE";
  if (!validType) {
    sendJson(400, "{\"message\":\"Nieznany typ zdarzenia.\"}");
    return;
  }

  int ml = constrain(webServer.arg("ml").toInt(), 0, ML_MAX);
  if (type == "ODCIAGANIE" && ml < ML_MIN) {
    sendJson(400, "{\"message\":\"Podaj ilosc odciagnietego mleka.\"}");
    return;
  }

  time_t when = time(nullptr);
  if (webServer.hasArg("when")) parseWebDateTime(webServer.arg("when"), when);

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
    webServer.on("/api/entry", HTTP_POST, handleApiEntry);
    webServer.on("/api/delete-entry", HTTP_POST, handleApiDeleteEntry);
    webServer.on("/api/send-backup", HTTP_POST, handleApiSendBackup);
    webServer.on("/api/event", HTTP_POST, handleApiEvent);
    webServer.on("/export.csv", HTTP_GET, handleExportCsv);
    webServer.on("/api/import", HTTP_POST, handleApiImport);
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
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

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
  lv_obj_add_event_cb(backButton, backHomeEvent, LV_EVENT_CLICKED, nullptr);

  static char TYPE_WET[] = "PIELUCHA_MOKRA";
  static char TYPE_DIRTY[] = "PIELUCHA_BRUDNA";
  lv_obj_t *wetButton = createButton(diaperScreen, "MOKRA", 14, 120, 452, 90, COLOR_BLUE);
  lv_obj_add_event_cb(wetButton, diaperQuickEvent, LV_EVENT_CLICKED, TYPE_WET);
  lv_obj_t *dirtyButton = createButton(diaperScreen, "BRUDNA", 14, 226, 452, 90, COLOR_ORANGE);
  lv_obj_add_event_cb(dirtyButton, diaperQuickEvent, LV_EVENT_CLICKED, TYPE_DIRTY);
  createLabel(diaperScreen, "Zapisuje biezacy czas jednym dotyknieciem.", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 330);

  loadReusableScreen(diaperScreen);
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

  // Gora ekranu: po lewej codzienne tipsy, po prawej wiek Aleksandra.
  lv_obj_t *tipCard = createCard(homeScreen, 14, 44, 268, 100);
  homeDevelopmentLabel = createLabel(tipCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_set_width(homeDevelopmentLabel, 244);
  lv_obj_set_style_text_font(homeDevelopmentLabel, &lv_font_montserrat_12, 0);
  lv_label_set_long_mode(homeDevelopmentLabel, LV_LABEL_LONG_WRAP);

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

  feedingCard = createCard(homeScreen, 14, 190, 220, 70);
  lv_obj_set_style_bg_color(feedingCard, lv_color_mix(COLOR_CARD, COLOR_ORANGE, 16), 0);
  lv_obj_set_style_shadow_width(feedingCard, 4, 0);
  createLabel(feedingCard, "KARMIENIE", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  homeFeedingLabel = createLabel(feedingCard, "", COLOR_TEXT, LV_ALIGN_CENTER, 0, 10);

  milkCard = createCard(homeScreen, 246, 190, 220, 70);
  lv_obj_set_style_bg_color(milkCard, lv_color_mix(COLOR_CARD, COLOR_BLUE, 16), 0);
  lv_obj_set_style_shadow_width(milkCard, 4, 0);
  createLabel(milkCard, "BUTELKA", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  homeMilkLabel = createLabel(milkCard, "", COLOR_TEXT, LV_ALIGN_CENTER, 0, 10);

  feedFormButton = createButton(homeScreen, "KARMIENIE", 14, 268, 452, 48, COLOR_ORANGE);
  lv_obj_add_event_cb(feedFormButton, feedingButtonEvent, LV_EVENT_CLICKED, nullptr);

  diaperButton = createButton(homeScreen, "PIELUCHA", 14, 324, 220, 40, COLOR_BLUE);
  lv_obj_add_event_cb(diaperButton, diaperOpenEvent, LV_EVENT_CLICKED, nullptr);
  pumpingHomeButton = createButton(homeScreen, "ODCIAG POKARMU", 246, 324, 220, 40, COLOR_BLUE);
  lv_obj_add_event_cb(pumpingHomeButton, pumpingOpenEvent, LV_EVENT_CLICKED, nullptr);

  calendarButton = createButton(homeScreen, "KALENDARZ", 14, 372, 220, 40, COLOR_BLUE);
  lv_obj_add_event_cb(calendarButton, calendarButtonEvent, LV_EVENT_CLICKED, nullptr);

  chartButton = createButton(homeScreen, "PODSUMOWANIE", 246, 372, 220, 40, COLOR_GREEN);
  lv_obj_add_event_cb(chartButton, chartButtonEvent, LV_EVENT_CLICKED, nullptr);

  // ===================== WYGASZACZ =====================
  // Karta zegara: zegar + data + ostatnie karmienie
  ssClockCard = createCard(homeScreen, 14, 152, 452, 140);

  ssClockLabel = createLabel(ssClockCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_font(ssClockLabel, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_align(ssClockLabel, LV_TEXT_ALIGN_CENTER, 0);
  ssClockShadowLabel = createLabel(ssClockCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 1, 9);
  lv_obj_set_style_text_font(ssClockShadowLabel, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_align(ssClockShadowLabel, LV_TEXT_ALIGN_CENTER, 0);

  ssDateLabel = createLabel(ssClockCard, "", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 65);
  lv_obj_set_style_text_font(ssDateLabel, &lv_font_montserrat_14, 0);

  ssLastFeedingLabel = createLabel(ssClockCard, "", COLOR_GREEN, LV_ALIGN_TOP_MID, 0, 95);
  lv_obj_set_style_text_font(ssLastFeedingLabel, &lv_font_montserrat_14, 0);

  // Karta pogody: ikona, temperatura, opis, min/max, 3 godziny, porada ubioru
  ssWeatherCard = createCard(homeScreen, 14, 290, 452, 180);

  ssIconBox = lv_obj_create(ssWeatherCard);
  lv_obj_remove_style_all(ssIconBox);
  lv_obj_set_size(ssIconBox, 100, 100);
  lv_obj_set_pos(ssIconBox, 8, 8);
  ssLastIconCode = -999;

  ssTempLabel = createLabel(ssWeatherCard, "", COLOR_TEXT, LV_ALIGN_TOP_LEFT, 120, 4);
  lv_obj_set_style_text_font(ssTempLabel, &lv_font_montserrat_36, 0);

  ssDescLabel = createLabel(ssWeatherCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 122, 54);
  lv_obj_set_width(ssDescLabel, 310);
  lv_label_set_long_mode(ssDescLabel, LV_LABEL_LONG_WRAP);

  ssMinMaxLabel = createLabel(ssWeatherCard, "", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 122, 82);

  for (uint8_t i = 0; i < 3; ++i) {
    ssHourLabels[i] = createLabel(ssWeatherCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID,
                                  static_cast<int>(-148 + i * 148), 116);
    lv_obj_set_width(ssHourLabels[i], 136);
    lv_obj_set_style_text_align(ssHourLabels[i], LV_TEXT_ALIGN_CENTER, 0);
  }

  ssDressLabel = createLabel(ssWeatherCard, "", COLOR_MUTED, LV_ALIGN_TOP_MID, 0, 148);
  lv_obj_set_width(ssDressLabel, 428);
  lv_obj_set_style_text_align(ssDressLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ssDressLabel, &lv_font_montserrat_10, 0);
  lv_label_set_long_mode(ssDressLabel, LV_LABEL_LONG_WRAP);

  ssRenderedClock = "";
  ssRenderedDate = "";
  ssRenderedTemp = "";
  ssRenderedDescription = "";
  ssRenderedMinMax = "";
  ssRenderedDress = "";
  ssRenderedLastFeeding = "";
  for (uint8_t i = 0; i < 3; ++i) ssRenderedHours[i] = "";

  applyScreensaverVisibility();

  renderedClock = "";
  renderedAge = "";
  renderedFeeding = "";
  renderedMilk = "";
  renderedDevelopment = "";
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
  const String developmentText = developmentTipForToday();

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
  setLabelTextIfChanged(homeDevelopmentLabel, renderedDevelopment, developmentText);
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
  const int actionY = extraMilkEnabled ? 462 : 330;
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

  formMilkCard = createCard(formScreen, 14, 330, 452, 124);
  lv_obj_set_style_bg_color(formMilkCard, lv_color_mix(COLOR_CARD, COLOR_BLUE, 12), 0);
  createLabel(formMilkCard, "BUTELKA - ILOSC I RODZAJ", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 4);
  formMilkMlLabel = createLabel(formMilkCard, "", COLOR_TEXT, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_t *milkSlider = lv_slider_create(formMilkCard);
  lv_obj_set_pos(milkSlider, 24, 46);
  lv_obj_set_size(milkSlider, 384, 10);
  lv_slider_set_range(milkSlider, ML_MIN, ML_MAX);
  lv_slider_set_value(milkSlider, selectedMilkMl, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(milkSlider, COLOR_BORDER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(milkSlider, COLOR_ORANGE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(milkSlider, COLOR_ORANGE, LV_PART_KNOB);
  lv_obj_add_event_cb(milkSlider, milkSliderEvent, LV_EVENT_VALUE_CHANGED, nullptr);
  createLabel(formMilkCard, "10 ML", COLOR_MUTED, LV_ALIGN_TOP_LEFT, 12, 54);
  createLabel(formMilkCard, "120 ML", COLOR_MUTED, LV_ALIGN_TOP_RIGHT, -12, 54);
  formMilkMatkiButton = createButton(formMilkCard, "MATKI", 16, 80, 196, 34, COLOR_GREEN);
  lv_obj_add_event_cb(formMilkMatkiButton, milkMatkiEvent, LV_EVENT_CLICKED, nullptr);
  formMilkModifiedButton = createButton(formMilkCard, "MODYFIKOWANE", 240, 80, 196, 34, COLOR_ORANGE);
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
  if (backupState == B_IDLE) {
    backupFileName = buildBackupFileName();
    backupState = B_WANTED;
    Serial.println("Backup: zaplanowano wysylke na Telegram.");
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
  if (type == "ODCIAGANIE") return String("Odcaganie ") + hhmm + " - " + ml + " ml";
  if (type == "WITAMINA_D") return String("Witamina D podana ") + hhmm;
  return type + " " + hhmm;
}

// Kolejka 1-elementowa: nowszy wpis zastępuje starszy, wysyłka nie blokuje UI.
void queueTelegram(const String &text) {
  if (text.length() == 0) return;
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) return;
  pendingTelegramText = text;
  if (telegramNextAttemptMs == 0) telegramNextAttemptMs = millis();
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
bool sendBackupViaTelegram() {
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
          backupFileName + "\"\r\nContent-Type: text/csv\r\n\r\n";
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

  if (code == 200) {
    Serial.println("Telegram: backup wyslany.");
    return true;
  }
  Serial.printf("Telegram: backup wysylka nieudana (kod %d).\n", code);
  return false;
}

void pumpTelegramQueue() {
#if !FEATURE_HTTPCLIENT
  if (backupState != B_IDLE) backupState = B_IDLE;
  if (pendingTelegramText.length() > 0) {
    Serial.println("Telegram: HTTPClient niedostepny w tym rdzeniu — powiadomienie pominiete.");
    pendingTelegramText = "";
  }
  return;
#else
  if (static_cast<int32_t>(millis() - telegramNextAttemptMs) < 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    telegramNextAttemptMs = millis() + 15000;
    return;
  }

  // Priorytet: backup (dokument) przed zwykla wiadomoscia.
  if (backupState == B_WANTED || backupState == B_SENDING) {
    backupState = B_SENDING;
    const bool ok = sendBackupViaTelegram();
    if (ok) {
      backupState = B_IDLE;
      telegramNextAttemptMs = millis() + 10000;
      return;
    }
    // Nieudane — zostajemy w WANTED i ponawiamy za 30 s.
    backupState = B_WANTED;
    telegramNextAttemptMs = millis() + 30000;
    return;
  }

  if (pendingTelegramText.length() == 0) return;

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
                      "&text=" + telegramUrlEncode(pendingTelegramText);
  const int code = http.POST(body);
  http.end();
  if (code == 200) {
    pendingTelegramText = ""; // sukces — usuwamy z kolejki
    Serial.println("Telegram: wiadomosc wyslana.");
  } else {
    // Niepowodzenie — zostawiamy w kolejce, ponawiamy za 30 s.
    Serial.printf("Telegram: wysylka nieudana (kod %d). Ponowka za 30 s.\n", code);
    telegramNextAttemptMs = millis() + 30000;
  }
#endif
}

// Wiadomosc startowa z pelnym statusem urzadzenia (kolejka, wysylka nieblokujaca).
void queueTelegramStartup() {
  if (strlen(TELEGRAM_BOT_TOKEN) == 0 || strlen(TELEGRAM_CHAT_ID) == 0) return;
  String msg = String("Leśny Dziennik uruchomiony\n") +
               String("Czas: ") + (timeIsValid ? String(formatDateTime(time(nullptr))) : "brak NTP") + "\n" +
               String("IP: ") + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "brak Wi-Fi") + "\n" +
               String("RAM: ") + String(ESP.getFreeHeap() / 1024) + " KB / " + String(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024) + " KB\n" +
               String("PSRAM: ") + String(ESP.getFreePsram() / 1024) + " KB / " + String(ESP.getPsramSize() / 1024) + " KB\n" +
               String("Pamiec: ") + (storageReady ? "gotowa" : "BLAD") + "\n" +
               String("Pogoda: ") + (weatherState.valid ? String(weatherState.tempNow) + " C" : "brak danych");
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

  const bool night = nightModeActive;
  const lv_color_t cSun = lv_color_hex(night ? 0xD9C24A : 0xE8A13A);
  const lv_color_t cSunLight = lv_color_hex(night ? 0xE8D468 : 0xF5C060);
  const lv_color_t cCloud = lv_color_mix(COLOR_CARD, COLOR_BLUE, LV_OPA_40);
  const lv_color_t cCloudDark = lv_color_mix(COLOR_BLUE, COLOR_MUTED, LV_OPA_30);
  const lv_color_t cRain = COLOR_BLUE;
  const lv_color_t cYellow = COLOR_YELLOW;
  const lv_color_t cFog = COLOR_MUTED;
  const lv_color_t cWhite = lv_color_white();

  // Wspólna chmura: trzy kule + dolne wypelnienie
  auto cloud = [&]() {
    dot(30, 48, 14, cCloud);
    dot(52, 38, 18, cCloud);
    dot(72, 44, 16, cCloud);
    dot(44, 56, 10, cCloud);
    pill(50, 52, 60, 20, 0, cCloud);
  };

  switch (weatherKindOf(wmoCode)) {
    case W_SUN: {
      // Główna tarcza slonca
      dot(50, 50, 22, cSunLight);
      dot(50, 50, 18, cSun);
      // 12 promieni co 30 stopni
      for (int i = 0; i < 12; ++i) {
        const float deg = i * 30.0f;
        const float rad = deg * 3.14159f / 180.0f;
        const int len = (i % 3 == 0) ? 18 : 14; // dluższe co trzeci
        const int cx = 50 + static_cast<int>(cosf(rad) * 30.0f);
        const int cy = 50 + static_cast<int>(sinf(rad) * 30.0f);
        const int angle = (i % 3 == 0) ? -static_cast<int>(deg) : -(static_cast<int>(deg) + 15);
        pill(cx, cy, len, 5, angle, cSun);
      }
      break;
    }
    case W_PARTLY: {
      // Slonce w górnym-lewym rogu
      dot(28, 24, 12, cSun);
      static const int rays[6][3] = {
          {44, 22, 0}, {14, 22, 0}, {22, 12, 90}, {22, 38, 90},
          {38, 14, 45}, {14, 36, 45}};
      for (const auto &r : rays) pill(r[0], r[1], 10, 4, r[2], cSun);
      // Chmura przesłaniająca dolna-prawa czesc
      dot(48, 44, 14, cCloudDark);
      dot(66, 36, 18, cCloud);
      dot(84, 44, 15, cCloud);
      dot(62, 52, 12, cCloud);
      pill(66, 48, 58, 18, 0, cCloud);
      break;
    }
    case W_CLOUD: {
      dot(20, 46, 12, cCloudDark);
      dot(38, 34, 18, cCloud);
      dot(58, 38, 20, cCloud);
      dot(76, 44, 14, cCloud);
      dot(48, 52, 16, cCloud);
      dot(68, 52, 12, cCloudDark);
      pill(50, 50, 72, 22, 0, cCloud);
      break;
    }
    case W_FOG: {
      for (int i = 0; i < 6; ++i) {
        const int y = 16 + i * 14;
        const int len = 60 + (i % 2 == 0 ? 0 : 20);
        const int offset = (i % 2 == 0 ? 20 : 10);
        pill(50 + offset, y, len, 6, 0, cFog);
      }
      break;
    }
    case W_RAIN: {
      cloud();
      // 6 kropli deszczu (bardziej strome — 20 stopni zamiast 25)
      static const int drops[6][3] = {
          {28, 74, 12}, {40, 82, 16}, {54, 76, 12},
          {66, 86, 14}, {78, 78, 12}, {90, 88, 10}};
      for (const auto &d : drops) pill(d[0], d[1], d[2], 4, 20, cRain);
      break;
    }
    case W_SNOW: {
      cloud();
      // 6 płatków sniegu: krzyzyk + kropka w srodku
      static const int sx[6] = {28, 42, 56, 70, 82, 50};
      static const int sy[6] = {80, 74, 88, 78, 84, 98};
      for (int i = 0; i < 6; ++i) {
        pill(sx[i], sy[i], 10, 3, 0, cWhite);
        pill(sx[i], sy[i], 10, 3, 90, cWhite);
        dot(sx[i], sy[i], 2, cWhite);
      }
      break;
    }
    case W_STORM: {
      cloud();
      // Błyskawica: trzy odcinki tworzace zygzak
      pill(52, 70, 18, 6, 120, cYellow);
      pill(40, 90, 14, 6, 60, cYellow);
      pill(56, 80, 10, 6, 130, cYellow);
      // 3 krople deszczu
      pill(20, 74, 10, 4, 20, cRain);
      pill(72, 82, 12, 4, 20, cRain);
      pill(86, 72, 8, 4, 20, cRain);
      break;
    }
  }
}

void updateScreensaverContent() {
  if (!screensaverActive || !ssClockLabel) return;

  WeatherState currentWeather = {};
  if (weatherMutex && xSemaphoreTake(weatherMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    currentWeather = weatherState;
    xSemaphoreGive(weatherMutex);
  } else {
    currentWeather = weatherState;
  }

  struct tm nowInfo;
  if (currentLocalTime(nowInfo)) {
    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &nowInfo);
    setLabelTextIfChanged(ssClockShadowLabel, ssRenderedClock, String(buf));
    setLabelTextIfChanged(ssClockLabel, ssRenderedClock, String(buf));
    static const char *DAYS[] = {"NIEDZIELA", "PONIEDZIALEK", "WTOREK", "SRODA",
                                 "CZWARTEK", "PIATEK", "SOBOTA"};
    char dbuf[8];
    strftime(dbuf, sizeof(dbuf), "%d.%m", &nowInfo);
    setLabelTextIfChanged(ssDateLabel, ssRenderedDate,
                          String(DAYS[nowInfo.tm_wday]) + " - " + dbuf);
    // Ostatnie karmienie na wygaszaczu z kolorowym znacznikiem
    String feedingText;
    lv_color_t feedingColor = COLOR_MUTED;
    if (lastFeedingTime) {
      const long elapsedMin = static_cast<long>(difftime(time(nullptr), lastFeedingTime) / 60);
      if (elapsedMin >= 0 && elapsedMin < COUNTER_BLINK_MIN) {
        feedingText = "OSTATNIE KARMIENIE: " + formatAgoText(lastFeedingTime);
      } else {
        feedingText = "CZAS NA KARMIENIE: " + formatAgoText(lastFeedingTime);
      }
      feedingColor = feedingAgeColor(lastFeedingTime);
    } else {
      feedingText = "OSTATNIE KARMIENIE: brak wpisu";
    }
    setLabelTextIfChanged(ssLastFeedingLabel, ssRenderedLastFeeding, feedingText);
    if (ssLastFeedingLabel) lv_obj_set_style_text_color(ssLastFeedingLabel, feedingColor, 0);
  } else {
    setLabelTextIfChanged(ssClockShadowLabel, ssRenderedClock, "--:--");
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
                          diaperButton, pumpingHomeButton, calendarButton, chartButton,
                          homeCounterBar};
  for (lv_obj_t *o : controls) {
    if (!o) continue;
    if (screensaverActive) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_t *widgets[] = {ssClockCard, ssWeatherCard,
                         ssClockShadowLabel, ssClockLabel, ssDateLabel, ssIconBox,
                         ssTempLabel, ssDescLabel, ssMinMaxLabel, ssDressLabel,
                         ssLastFeedingLabel,
                         ssHourLabels[0], ssHourLabels[1], ssHourLabels[2]};
  for (lv_obj_t *o : widgets) {
    if (!o) continue;
    if (screensaverActive) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

void enterScreensaver() {
  // Idempotentne: wywolanie przy starcie (przed dotknieciem) rowniez tworzy timer.
  screensaverActive = true;
  applyScreensaverVisibility();
  updateScreensaverContent();
  if (!weatherState.valid || millis() - weatherLastTryMs > WEATHER_STALE_MS) {
    weatherFetchPending = true;
  }
  if (!ssClockTimer) {
    ssClockTimer = lv_timer_create([](lv_timer_t *) { updateScreensaverContent(); }, 10000, nullptr);
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
  if (!storageReady || !weatherState.valid) return false;
  File file = LittleFS.open(WEATHER_CACHE_FILE, FILE_WRITE);
  if (!file) return false;
  const uint32_t magic = 0x57454131UL; // "WEA1"
  const bool saved = file.write(reinterpret_cast<const uint8_t *>(&magic), sizeof(magic)) == sizeof(magic) &&
                     file.write(reinterpret_cast<const uint8_t *>(&weatherState), sizeof(weatherState)) == sizeof(weatherState);
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

// --------------------------------- Uruchomienie ---------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);
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

  createReusableScreenRoots();
  Serial.println("INIT: korzenie ekranow utworzone");
  connectWiFi();
  timeIsValid = syncTimeFromNTP();
  lastNtpSyncMs = millis();
  storageReady = initialiseStorage();
  if (storageReady) {
    Serial.printf("LittleFS: uzywane %u KB z %u KB.\n",
                  static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                  static_cast<unsigned>(LittleFS.totalBytes() / 1024));
  }
  loadLatestEntries();
  weatherMutex = xSemaphoreCreateMutex();
  if (!weatherMutex) {
    Serial.println("Pogoda: mutex nieutworzony — tryb bezpieczny bez wspoldzielenia.");
  }
  if (loadWeatherCache()) {
    weatherLastTryMs = millis();
    Serial.println("Pogoda: pokazuje ostatnie zapisane dane do czasu odswiezenia.");
  } else {
    weatherFetchPending = true;
  }
  weatherNextTryMs = millis() + WEATHER_START_DELAY_MS;
  startWebServer();
  initOptionalServices();
  if (xTaskCreatePinnedToCore(weatherTask, "weather", 4096, nullptr, 1,
                              &weatherTaskHandle, 0) != pdPASS) {
    Serial.println("Pogoda: nie mozna uruchomic zadania FreeRTOS.");
    weatherTaskHandle = nullptr;
  }
  appendBackupIfDue();
  updateNightMode();
  lv_timer_create(agingTickCb, 30000, nullptr);
  counterAlarmTimer = lv_timer_create(counterAlarmTickCb, 500, nullptr);
  lastUiWifiConnected = WiFi.status() == WL_CONNECTED;
  createHomeScreen();
  // Start w trybie zegara: czekamy na pierwsze dotkniecie ekranu.
  enterScreensaver();
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
  updateNightMode();
  pumpTelegramQueue();
  resyncNtpIfDue();

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
  if (webServerStarted) webServer.handleClient();

  const uint32_t lvglNowMs = millis();
  lv_tick_inc(lvglNowMs - lastLvglTickMs);
  lastLvglTickMs = lvglNowMs;
  lv_timer_handler();

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

  const uint32_t loopStart = micros();

  delay(5);

  // CPU load: im wiecej czasu poza delay(5), tym wyzsze obciazenie.
  // Mierzymy czasy w oknie 5 s.
  const uint32_t now = millis();
  if (now - cpuCalcLastMs >= 5000) {
    const uint64_t elapsedUs = cpuCalcLastMs == 0 ? 5000000ULL : (static_cast<uint64_t>(now - cpuCalcLastMs) * 1000);
    cpuLoadPct = static_cast<int>((cpuBusyUs * 100) / elapsedUs);
    if (cpuLoadPct > 100) cpuLoadPct = 100;
    cpuBusyUs = 0;
    cpuCalcLastMs = now;
  }
  cpuBusyUs += micros() - loopStart;
}
