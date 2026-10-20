// =============================================================================
//  store.h — dane z hostingu po sparsowaniu (tablica wpisow w PSRAM)
//  Czysty C++ (bez Arduino/LVGL): kompiluje sie tez w symulatorze na PC.
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>

enum EntryType : uint8_t {
  ET_FEED = 0,        // KARMIENIE (minuty L/P)
  ET_MILK,            // MLEKO (stary typ, bez rodzaju)
  ET_MILK_MOTHER,     // MLEKO_MATKI
  ET_MILK_MODIFIED,   // MLEKO_MODYFIKOWANE
  ET_MILK_MIXED,      // MLEKO_MIESZANE
  ET_DIAPER_WET,      // PIELUCHA_MOKRA
  ET_DIAPER_DIRTY,    // PIELUCHA_BRUDNA
  ET_PUMP,            // ODCIAGANIE
  ET_VITAMIN_D,       // WITAMINA_D
  ET_WEIGHT,          // WAGA (gramy w kolumnie ml)
  ET_SLEEP_START,     // SEN_START
  ET_SLEEP_STOP,      // SEN_STOP
  ET_BATH,            // KAPIEL
  ET_OTHER
};

static inline bool etIsMilk(uint8_t t) { return t >= ET_MILK && t <= ET_MILK_MIXED; }

struct Entry {        // 16 B
  int32_t t;          // epoka (czas lokalny z CSV przeliczony przez mktime)
  int32_t day;        // dni od 1970-01-01 dla DATY z CSV (kalendarz lokalny)
  uint16_t mod;       // minuta doby z CSV (0..1439)
  uint16_t ml;        // ml / gramy
  uint8_t type;       // EntryType
  uint8_t left, right;  // minuty piersi
  uint8_t pad;
};

struct Store {
  Entry *e = nullptr;     // posortowane rosnaco po t (stabilnie wzgledem kolejnosci w pliku)
  uint32_t n = 0;
  uint32_t badLines = 0;  // wiersze odrzucone przez parser
  uint32_t crc = 0;       // CRC32 tresci CSV, z ktorej powstal
};

// Dni od 1970-01-01 dla daty kalendarzowej (algorytm Howarda Hinnanta).
int daysFromCivil(int y, int m, int d);
// Odwrotnie: dzien -> rok/miesiac/dzien.
void civilFromDays(int z, int &y, int &m, int &d);

// Szybka walidacja tresci CSV: naglowek "data,godzina,typ," i pelny ostatni wiersz.
bool csvLooksValid(const uint8_t *buf, size_t len);
uint32_t crc32Of(const uint8_t *buf, size_t len);

// Parsuje CSV do nowego Store (pamiec z alokatora storeAlloc). Zwraca nullptr przy bledzie.
// Wymaga ustawionej strefy czasowej (TZ) — uzywa mktime.
Store *storeParse(const uint8_t *buf, size_t len);
void storeFree(Store *s);

// Alokator duzych blokow (na ESP32: PSRAM). Zdefiniowany w store.cpp.
void *storeAlloc(size_t bytes);
void storeFreeMem(void *p);
