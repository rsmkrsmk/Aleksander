// store.cpp — parser CSV z hostingu do tablicy wpisow (jeden przebieg, bez String).
#include "store.h"
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

void *storeAlloc(size_t bytes) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (p) return p;
#endif
  return malloc(bytes);
}
void storeFreeMem(void *p) { free(p); }   // heap_caps_malloc i malloc zwalnia free()

int daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilFromDays(int z, int &y, int &m, int &d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int yy = static_cast<int>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = yy + (m <= 2);
}

bool csvLooksValid(const uint8_t *buf, size_t len) {
  static const char HDR[] = "data,godzina,typ,";
  if (!buf || len < 30) return false;
  if (buf[len - 1] != '\n') return false;
  return memcmp(buf, HDR, sizeof(HDR) - 1) == 0;
}

uint32_t crc32Of(const uint8_t *buf, size_t len) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    c ^= buf[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static bool digits(const char *s, int n) {
  for (int i = 0; i < n; ++i) if (s[i] < '0' || s[i] > '9') return false;
  return true;
}
static int num(const char *s, int n) {
  int v = 0;
  for (int i = 0; i < n; ++i) v = v * 10 + (s[i] - '0');
  return v;
}

static uint8_t typeOf(const char *s, size_t n) {
  struct M { const char *name; uint8_t t; };
  static const M map[] = {
      {"KARMIENIE", ET_FEED},           {"MLEKO", ET_MILK},
      {"MLEKO_MATKI", ET_MILK_MOTHER},  {"MLEKO_MODYFIKOWANE", ET_MILK_MODIFIED},
      {"MLEKO_MIESZANE", ET_MILK_MIXED}, {"PIELUCHA_MOKRA", ET_DIAPER_WET},
      {"PIELUCHA_BRUDNA", ET_DIAPER_DIRTY}, {"ODCIAGANIE", ET_PUMP},
      {"WITAMINA_D", ET_VITAMIN_D},     {"WAGA", ET_WEIGHT},
      {"SEN_START", ET_SLEEP_START},    {"SEN_STOP", ET_SLEEP_STOP},
      {"KAPIEL", ET_BATH}};
  for (const M &m : map) {
    if (strlen(m.name) == n && memcmp(m.name, s, n) == 0) return m.t;
  }
  return ET_OTHER;
}

// Liczba calkowita z pola [s, e) (puste/nie-liczba = 0, ujemne = 0).
static long field(const char *s, const char *e) {
  long v = 0;
  bool any = false;
  while (s < e && *s == ' ') ++s;
  while (s < e && *s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); any = true; ++s; if (v > 1000000) break; }
  return any ? v : 0;
}

// Wiersz: RRRR-MM-DD,GG:MM,TYP,ML[,L,P]  (dodatkowe kolumny sa ignorowane)
static bool parseLine(const char *s, const char *end, Entry &out) {
  if (end - s < 18) return false;
  if (!digits(s, 4) || s[4] != '-' || !digits(s + 5, 2) || s[7] != '-' || !digits(s + 8, 2) || s[10] != ',') return false;
  if (!digits(s + 11, 2) || s[13] != ':' || !digits(s + 14, 2) || s[16] != ',') return false;
  const int y = num(s, 4), mo = num(s + 5, 2), d = num(s + 8, 2), hh = num(s + 11, 2), mm = num(s + 14, 2);
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || y < 2020 || y > 2100) return false;
  const char *f[6];
  const char *fe[6];
  int nf = 0;
  const char *p = s + 17;
  while (nf < 6) {
    const char *c = static_cast<const char *>(memchr(p, ',', static_cast<size_t>(end - p)));
    f[nf] = p;
    fe[nf] = c ? c : end;
    ++nf;
    if (!c) break;
    p = c + 1;
  }
  if (nf < 2) return false;   // typ i ml sa wymagane
  out.type = typeOf(f[0], static_cast<size_t>(fe[0] - f[0]));
  long ml = field(f[1], fe[1]);
  out.ml = static_cast<uint16_t>(ml > 65535 ? 65535 : ml);
  long l = nf > 2 ? field(f[2], fe[2]) : 0, r = nf > 3 ? field(f[3], fe[3]) : 0;
  out.left = static_cast<uint8_t>(l > 255 ? 255 : l);
  out.right = static_cast<uint8_t>(r > 255 ? 255 : r);
  out.pad = 0;
  out.day = daysFromCivil(y, mo, d);
  out.mod = static_cast<uint16_t>(hh * 60 + mm);
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = mo - 1;
  t.tm_mday = d;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_isdst = -1;
  out.t = static_cast<int32_t>(mktime(&t));
  return true;
}

Store *storeParse(const uint8_t *buf, size_t len) {
  if (!buf || len == 0) return nullptr;
  // Gorne oszacowanie liczby wierszy = liczba '\n' + 1.
  size_t lines = 1;
  for (size_t i = 0; i < len; ++i) if (buf[i] == '\n') ++lines;
  Store *s = static_cast<Store *>(storeAlloc(sizeof(Store)));
  if (!s) return nullptr;
  new (s) Store();
  s->e = static_cast<Entry *>(storeAlloc(lines * sizeof(Entry)));
  if (!s->e) { storeFreeMem(s); return nullptr; }
  const char *p = reinterpret_cast<const char *>(buf);
  const char *end = p + len;
  while (p < end) {
    const char *nl = static_cast<const char *>(memchr(p, '\n', static_cast<size_t>(end - p)));
    const char *le = nl ? nl : end;
    const char *te = le;
    while (te > p && (te[-1] == '\r' || te[-1] == ' ')) --te;
    if (te > p && !(te - p >= 5 && memcmp(p, "data,", 5) == 0)) {
      if (parseLine(p, te, s->e[s->n])) ++s->n;
      else ++s->badLines;
    }
    p = nl ? nl + 1 : end;
  }
  std::stable_sort(s->e, s->e + s->n, [](const Entry &a, const Entry &b) { return a.t < b.t; });
  s->crc = crc32Of(buf, len);
  return s;
}

void storeFree(Store *s) {
  if (!s) return;
  if (s->e) storeFreeMem(s->e);
  storeFreeMem(s);
}
