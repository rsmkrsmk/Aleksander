// hw_touch.h — dotyk GT911 jako wskaznik LVGL (odczyt reczny co 5 ms w petli).
#pragma once
#include <stdint.h>

bool touchInit();                 // po displayInit() (wymaga lv_init) i Wire.begin()
void touchPoll();                 // wolac czesto z petli UI (sam ogranicza sie do 1 odczytu / 5 ms)
uint32_t touchLastActivityMs();   // millis() ostatniego dotkniecia
// Gdy funkcja zwroci true (np. aktywna tablica), dotkniecie NIE trafia do LVGL: wolane jest
// onSwallowedTap(), a dalszy dotyk jest tlumiony do puszczenia palca (bez "przeklikniecia").
void touchSetSwallow(bool (*shouldSwallow)(), void (*onSwallowedTap)());
