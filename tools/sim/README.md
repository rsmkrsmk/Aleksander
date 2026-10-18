# Symulator PC interfejsu urządzenia

Renderuje dokładnie ten sam kod LVGL 9.3 (`ui_modern.h`) co firmware, bez ESP32 — do RGB565 → PNG/GIF.

Wymagane (poza repo): źródła LVGL 9.3.0 w katalogu `lvgl/` oraz `inc/lv_conf.h` (kopia `lv_conf.h` z repo
z `LV_COLOR_DEPTH 16`, `LV_USE_ARC/CHART/IMAGE/LINE/CANVAS`), `gcc`, `python3` + `numpy`, `pillow`.

```
make sim_ui                       # scenariusze z samych danych demo (scen_ui.h)
./sim_ui boot out/boot 400,2000,4000        # ekran startowy: klatki w ms
./sim_ui saver_night out/s 1000             # tablica (wygaszacz), tryb nocny
./sim_ui dash_3 out/d 1500                  # panel, strona 3 (Waga); dash_night_N = noc
python3 raw2png.py out/*.raw                # RAW -> PNG

python3 gen_csv.py                # syntetyczny plik /tmp/fw_data.csv (40 dni)
python3 extract_glue.py           # wycina model danych z v3.ino
make sim_fw && ./sim_fw fw_4 out/n 1100     # wykresy z prawdziwego kodu firmware na danych CSV
```
