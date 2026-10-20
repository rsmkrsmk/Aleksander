// weather.cpp — Open-Meteo: current + hourly (+1..+3 h) + daily min/max.
#include "weather.h"
#include "app_config.h"
#include "net.h"
#include "diag.h"
#include <Arduino.h>
#include <NetworkClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

RTC_NOINIT_ATTR static WeatherData rtcWeather;
RTC_NOINIT_ATTR static uint32_t rtcWeatherMagic;
constexpr uint32_t WEATHER_MAGIC = 0x57454135UL;

static SemaphoreHandle_t mtx = nullptr;
static WeatherData cur;
static uint32_t lastOkMs = 0, lastTryMs = 0;
static bool everOk = false;

void weatherInit() {
  mtx = xSemaphoreCreateMutex();
  if (rtcWeatherMagic == WEATHER_MAGIC && rtcWeather.valid) {
    cur = rtcWeather;   // pokazujemy ostatnie dane do czasu odswiezenia
    Serial.println("Pogoda: dane z pamieci RTC.");
  }
}

WeatherData weatherSnapshot() {
  WeatherData w;
  if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    w = cur;
    xSemaphoreGive(mtx);
  }
  return w;
}

bool weatherDue() {
  const uint32_t now = millis();
  if (now < WEATHER_START_DELAY_MS) return false;
  if (!everOk) return lastTryMs == 0 || now - lastTryMs >= WEATHER_RETRY_MS;
  return now - lastOkMs >= WEATHER_REFRESH_MS && now - lastTryMs >= WEATHER_RETRY_MS;
}

uint32_t weatherAgeMs() { return everOk ? millis() - lastOkMs : 0xFFFFFFFFUL; }

bool weatherFetch() {
  lastTryMs = millis();
  if (!wifiOk()) return false;
  NetworkClient c;
  c.setTimeout(8000);
  if (!c.connect("api.open-meteo.com", 80)) {
    Serial.println("Pogoda: brak polaczenia.");
    return false;
  }
  char req[420];
  const int n = snprintf(req, sizeof req,
                         "GET /v1/forecast?latitude=%.4f&longitude=%.4f"
                         "&current=temperature_2m,apparent_temperature,weather_code"
                         "&hourly=temperature_2m,weather_code"
                         "&daily=temperature_2m_max,temperature_2m_min&forecast_days=2&timezone=Europe%%2FWarsaw HTTP/1.1\r\n"
                         "Host: api.open-meteo.com\r\nUser-Agent: LesnyDziennik/" FW_VERSION "\r\nConnection: close\r\n\r\n",
                         WEATHER_LAT, WEATHER_LON);
  PsBuf body;
  body.limit = 32 * 1024;
  HttpPiece pc = {reinterpret_cast<const uint8_t *>(req), static_cast<size_t>(n)};
  const HttpResult r = httpExchange(c, &pc, 1, &body, 10000);
  c.stop();
  bool ok = false;
  if (r.status == 200 && body.len > 0) {
    const time_t now = time(nullptr);
    struct tm tn;
    localtime_r(&now, &tn);
    WeatherData w;
    if (weatherParse(reinterpret_cast<const char *>(body.p), timeOk() ? tn.tm_hour : -1, w)) {
      if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(200)) == pdTRUE) {
        cur = w;
        xSemaphoreGive(mtx);
      }
      rtcWeather = w;
      rtcWeatherMagic = WEATHER_MAGIC;
      lastOkMs = millis();
      everOk = true;
      ok = true;
      Serial.printf("Pogoda: %+d C (odcz. %+d), kod %d\n", w.tempNow, w.feelsLike, w.codeNow);
    }
  }
  if (!ok) Serial.printf("Pogoda: blad (HTTP %d, %u B).\n", r.status, (unsigned)body.len);
  body.release();
  return ok;
}
