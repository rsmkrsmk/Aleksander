/*
  Leśny Dziennik Aleksandra — v5 (urządzenie = tylko wyświetlacz)
  Płytka: Waveshare ESP32-S3-Touch-LCD-4B (480x480 RGB, GT911)
  Środowisko: Arduino IDE, esp32 by Espressif >= 3.2, LVGL 9.3+, GFX Library for Arduino >= 1.6, SensorLib.

  Przed kompilacją:
   1. Skopiuj secrets.h.example jako secrets.h i uzupełnij Wi-Fi i Telegram.
   2. lv_conf.h skopiuj do folderu bibliotek Arduino (obok folderu lvgl) — patrz README.
   3. Narzędzia: Płytka „ESP32S3 Dev Module”, PSRAM „OPI PSRAM”, Flash Size „16MB”,
      Partition Scheme — używany jest plik partitions.csv z tego folderu.

  Cała logika jest w modułach (app.cpp, stats.cpp, host_sync.cpp, …) — ten plik tylko je uruchamia.
*/
#include "app.h"

void setup() { appSetup(); }
void loop() { appLoop(); }
