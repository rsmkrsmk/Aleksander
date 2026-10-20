// diag.h — liczniki uruchomien (RTC), watchdog zadan, obciazenie CPU, minimum RAM.
#pragma once
#include <stdint.h>

void diagInit();                 // powod resetu + liczniki w RTC (przetrwaja restart programowy)
void diagWatchdogStart();        // Task WDT dla biezacego zadania (loop)
void diagWatchdogAddCurrentTask();
void diagFeedWatchdog();         // bezpieczne z kazdego zadania (tylko gdy subskrybowane)
void diagCpuAdd(uint32_t busyUs);
int diagCpuPct();
uint32_t diagMinFreeInternal();
uint32_t diagBootCount();
uint32_t diagWdtResets();
const char *diagResetReasonText();
// Dzien ostatniej kopii na Telegram (RTC, przetrwa restart programowy).
int32_t diagLastBackupDay();
void diagSetLastBackupDay(int32_t day);
