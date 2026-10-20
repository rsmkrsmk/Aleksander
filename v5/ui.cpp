// ui.cpp — jedyny plik dolaczajacy ui_modern.h (fonty i grafiki trafiaja do jednej jednostki).
#include "ui.h"
#include <Arduino.h>
#include "ui_modern.h"

static UiScreen cur = UiScreen::Boot;
static volatile uint8_t requests = 0;

void uiRequest(uint8_t r) { requests = requests | r; }
uint8_t uiTakeRequests() {
  const uint8_t r = requests;
  requests = 0;
  return r;
}
UiScreen uiScreen() { return cur; }

void uiBootBegin() {
  uiBootCreate();
  cur = UiScreen::Boot;
}
void uiBootSetStep(int idx, int state) { uiBootStep(static_cast<uint8_t>(idx), static_cast<uint8_t>(state)); }
void uiBootSetText(const char *t) { uiBootSetStatus(t); }
void uiBootSetProgress(int pct) { uiBootProgress(pct); }
void uiBootEnd() { uiBootDestroy(); }

static void hookCallbacks() {
  uiOnBackToSaver = []() { uiRequest(UI_REQ_TO_SAVER); };
  uiOnDiagnostics = []() { uiRequest(UI_REQ_TO_DIAG); };
  uiOnDiagBack = []() { uiRequest(UI_REQ_TO_DASH); };
}

void uiShowSaver(const UiModel &m) {
  hookCallbacks();
  uiSaverCreate(m.night);          // nowy ekran (stary usuwany po zaladowaniu nowego)
  uiSaverUpdate(m);
  lv_screen_load(uiSaverScr);
  uiDashDestroy();                 // panel i diagnostyka nie sa potrzebne na tablicy (pamiec LVGL)
  uiDiagDestroy();
  cur = UiScreen::Saver;
}

void uiShowDash(const UiModel &m) {
  hookCallbacks();
  uiDashCreate(m.night);
  uiDashUpdate(m, false);
  uiDashShow(0);
  uiDashUpdate(m, false);
  uiSaverDestroy();
  uiDiagDestroy();
  cur = UiScreen::Dash;
}

void uiShowDiag(const UiModel &m, const char *text) {
  hookCallbacks();
  uiDiagCreate(m.night, text);
  lv_screen_load(uiDiagScr);
  uiDashDestroy();
  uiSaverDestroy();
  cur = UiScreen::Diag;
}

void uiRefresh(const UiModel &m, bool chartsChanged, const char *diagText) {
  switch (cur) {
    case UiScreen::Saver: uiSaverUpdate(m); break;
    case UiScreen::Dash: uiDashUpdate(m, chartsChanged); break;
    case UiScreen::Diag: if (diagText) uiDiagUpdate(diagText); break;
    default: break;
  }
}
