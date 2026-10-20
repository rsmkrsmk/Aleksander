# Testy v5 na PC

- `test_stats.cpp` — parser (`store.cpp`) i obliczenia (`stats.cpp`) z `v5/` na pliku CSV; wydruk w formacie
  zgodnym ze scenariuszem `fw` symulatora v4 (porównanie parytetu). Budowa i uruchomienie — patrz README główne.
- `scen_v5.h` — scenariusze symulatora LVGL (`tools/sim/sim.cpp`) z prawdziwymi `store.cpp`, `stats.cpp` i `ui_modern.h` z v5:
  `saver`, `saver_night`, `dash_0`..`dash_5`, `dash_night_N`, `weightB` (widok przyrostu po przebudowie), `diag`.
  Zmienne: `CSV=plik.csv` (domyślnie /tmp/fw_data.csv), `NOW=epoka`.

Uwaga: `tools/sim/extract_glue.py` i `scen_fw.h` dotyczą kodu v4 (`v3.ino`, gałąź `v4.1-claude-pazdziernik`).
