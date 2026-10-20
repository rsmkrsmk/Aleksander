// hw_touch.cpp — GT911 (SensorLib), jak w v4.1.
#include "hw_touch.h"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // getPoint() — dziala we wszystkich wersjach SensorLib
#include "hw_display.h"
#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <TouchDrv.hpp>

constexpr uint8_t I2C_SDA = 47;
constexpr uint8_t I2C_SCL = 48;

static TouchDrvGT911 gt;
static bool ready = false;
static lv_indev_t *indev = nullptr;
static int16_t px[5], py[5];
static volatile uint32_t lastActivity = 0;
static bool (*swallowQ)() = nullptr;
static void (*swallowTap)() = nullptr;

static void readCb(lv_indev_t *, lv_indev_data_t *data) {
  static int16_t lastX = 0, lastY = 0;
  static bool suppress = false;
  static uint32_t suppressStart = 0;
  if (suppress && millis() - suppressStart > 2000) suppress = false;   // bezpiecznik
  data->state = LV_INDEV_STATE_RELEASED;
  data->point.x = lastX;
  data->point.y = lastY;
  if (!ready) { suppress = false; return; }
  const uint8_t n = gt.getPoint(px, py, gt.getSupportTouchPoint());
  if (n == 0) { suppress = false; return; }
  lastActivity = millis();
  if (swallowQ && swallowQ()) {
    if (!suppress && swallowTap) swallowTap();
    suppress = true;
    suppressStart = millis();
    return;
  }
  if (suppress) return;
  lastX = constrain(px[0], 0, static_cast<int16_t>(SCREEN_W - 1));
  lastY = constrain(py[0], 0, static_cast<int16_t>(SCREEN_H - 1));
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = LV_INDEV_STATE_PRESSED;
}

bool touchInit() {
  gt.setPins(-1, -1);
  ready = gt.begin(Wire, GT911_SLAVE_ADDRESS_L, I2C_SDA, I2C_SCL);
  if (ready) gt.setMaxTouchPoint(1);
  Serial.printf("GT911: %s\n", ready ? "dotyk gotowy" : "nie wykryto kontrolera");
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);
  // Tryb EVENT: LVGL nie odpytuje sam; odczyt tylko przez touchPoll() z petli.
  lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
  lastActivity = millis();
  return ready;
}

void touchPoll() {
  static uint32_t lastUs = 0;
  const uint32_t t = micros();
  if (!indev || t - lastUs < 5000) return;   // GT911 odswieza ~100 Hz
  lastUs = t;
  lv_indev_read(indev);
}

uint32_t touchLastActivityMs() { return lastActivity; }

void touchSetSwallow(bool (*q)(), void (*tap)()) {
  swallowQ = q;
  swallowTap = tap;
}
