// ui.h — cienka warstwa nad ui_modern.h (ekrany: start, tablica, panel, diagnostyka).
#pragma once
#include "ui_model.h"

enum class UiScreen { Boot, Saver, Dash, Diag };

void uiBootBegin();
void uiBootSetStep(int idx, int state);   // 0 w toku, 1 OK, 2 pominiete/blad
void uiBootSetText(const char *t);
void uiBootSetProgress(int pct);
void uiBootEnd();

void uiShowSaver(const UiModel &m);
void uiShowDash(const UiModel &m);
void uiShowDiag(const UiModel &m, const char *text);
// Odswiezenie biezacego ekranu. chartsChanged => przebudowa biezacej karty panelu.
void uiRefresh(const UiModel &m, bool chartsChanged, const char *diagText);
UiScreen uiScreen();

// Zadania zgloszone przez UI (wykonywane w petli, poza callbackami LVGL).
enum UiRequest : uint8_t { UI_REQ_NONE = 0, UI_REQ_TO_SAVER = 1, UI_REQ_TO_DIAG = 2, UI_REQ_TO_DASH = 4 };
uint8_t uiTakeRequests();
void uiRequest(uint8_t r);
