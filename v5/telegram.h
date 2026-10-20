// telegram.h — powiadomienia (start, okno drzemki) i codzienna kopia danych.
#pragma once
#include <stdint.h>
#include "stats.h"

bool telegramConfigured();
void telegramQueue(const char *text);           // z dowolnego zadania; 1 wiadomosc w kolejce (nowsza zastepuje)
bool telegramPending();
bool telegramSendPending();                     // zadanie sieciowe: proba wyslania kolejki
bool telegramSendDocument(const uint8_t *data, size_t len, const char *fileName);   // zadanie sieciowe
// Petla UI, co ~1 s: powiadomienia o oknie drzemki (raz na okno czuwania).
void telegramCheckSleep(const SleepState &s, bool enabled, bool timeValid, time_t now);
