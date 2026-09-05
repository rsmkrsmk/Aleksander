// Warstwa danych + logika obliczeniowa odtworzona 1:1 z firmware
// (OfficialWaveshareHelloWorld.ino). Zrodlem danych jest plik CSV; wszystkie czasy
// liczymy w czasie LOKALNYM (kontener ma TZ=Europe/Warsaw), tak jak urzadzenie.
import fs from 'node:fs';
import path from 'node:path';
import * as C from './config.js';

// ------------------------------- Pomocnicze czasu -------------------------------
const pad2 = (n) => String(n).padStart(2, '0');

// "YYYY-MM-DD" dla podanej daty (czas lokalny).
export function dateIso(d) {
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;
}

// "YYYY-MM-DDTHH:MM" (bez sekund, bez strefy) — odpowiednik webDateTime().
export function webDateTime(d) {
  return `${dateIso(d)}T${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
}

// Pelny opis daty/godziny do pola "now" (display-only).
export function formatDateTime(d) {
  return `${pad2(d.getDate())}.${pad2(d.getMonth() + 1)}.${d.getFullYear()} ${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
}

// Poczatek dnia (00:00 lokalnie).
function beginningOfDay(d) {
  return new Date(d.getFullYear(), d.getMonth(), d.getDate(), 0, 0, 0, 0);
}

// Dzien przesuniety o N wstecz, zakotwiczony na 12:00 -> poczatek tego dnia.
// Odpowiednik dayOffsetFromToday(): uzywa poludnia by uniknac przeskokow DST.
export function dayOffsetFromToday(daysBack, now = new Date()) {
  const d = new Date(now.getFullYear(), now.getMonth(), now.getDate() - daysBack, 12, 0, 0, 0);
  return beginningOfDay(d);
}

// "YYYY-MM-DD","HH:MM" -> Date (czas lokalny). Zwraca null gdy niepoprawne.
export function csvDateTimeToDate(dateStr, timeStr) {
  if (!dateStr || dateStr.length !== 10 || !timeStr || timeStr.length < 5) return null;
  const y = Number(dateStr.slice(0, 4));
  const mo = Number(dateStr.slice(5, 7)) - 1;
  const da = Number(dateStr.slice(8, 10));
  const hh = Number(timeStr.slice(0, 2));
  const mm = Number(timeStr.slice(3, 5));
  if ([y, mo, da, hh, mm].some(Number.isNaN)) return null;
  return new Date(y, mo, da, hh, mm, 0, 0);
}

// "YYYY-MM-DDTHH:MM" -> Date z walidacja (odpowiednik parseWebDateTime).
// Odrzuca czas < 2025-01-01, tak jak firmware.
export function parseWebDateTime(value) {
  if (typeof value !== 'string' || value.length !== 16) return null;
  if (value[4] !== '-' || value[7] !== '-' || value[10] !== 'T' || value[13] !== ':') return null;
  const positions = [0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15];
  for (const p of positions) {
    const ch = value[p];
    if (ch < '0' || ch > '9') return null;
  }
  const y = Number(value.slice(0, 4));
  const mo = Number(value.slice(5, 7)) - 1;
  const da = Number(value.slice(8, 10));
  const hh = Number(value.slice(11, 13));
  const mm = Number(value.slice(14, 16));
  const d = new Date(y, mo, da, hh, mm, 0, 0);
  // Odrzucamy czas przed 2025-01-01 (lokalnie) — odpowiednik firmware `< 1735689600`.
  if (d.getTime() < new Date(2025, 0, 1, 0, 0, 0, 0).getTime()) return null;
  // Walidacja poprawnosci (jak weryfikacja localtime_r w firmware).
  if (d.getFullYear() !== y || d.getMonth() !== mo || d.getDate() !== da ||
      d.getHours() !== hh || d.getMinutes() !== mm) return null;
  return d;
}

// ------------------------------- Wiek dziecka -----------------------------------
function birthDate() {
  return new Date(C.BIRTH_YEAR, C.BIRTH_MONTH - 1, C.BIRTH_DAY, 12, 0, 0, 0);
}

export function calculateAgeDays(now = new Date()) {
  const nowNoon = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 12, 0, 0, 0);
  const diff = (nowNoon.getTime() - birthDate().getTime()) / 86400000;
  return Math.round(diff);
}

export function calculateAgeText(now = new Date()) {
  const days = calculateAgeDays(now);
  if (days < 0) return 'Wiek: data urodzenia jest w przyszlosci';
  const nowNoon = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 12, 0, 0, 0);
  const b = birthDate();
  let fullMonths = (nowNoon.getFullYear() - b.getFullYear()) * 12 + (nowNoon.getMonth() - b.getMonth());
  if (nowNoon.getDate() < b.getDate()) fullMonths -= 1;
  if (fullMonths < 0) fullMonths = 0;
  const weeks = Math.floor(days / 7);
  const extra = days % 7;
  return `Aleksander ma ${days} dni\n${weeks} tyg. i ${extra} dni | ${fullMonths} mies.`;
}

export function developmentTipForToday(now = new Date()) {
  const days = calculateAgeDays(now);
  if (days < 0) return 'Rozwoj: oczekiwanie na prawidlowy czas';
  return `Dzien ${days}: obserwuj rozwoj, zapewnij bliskosc, ruch i spokojny rytm dnia. Kazde dziecko rozwija sie we wlasnym tempie.`;
}

// Dzien zycia (0 = dzien urodzenia) dla daty CSV "YYYY-MM-DD".
export function dayOfLifeForDate(isoDate) {
  if (!isoDate || isoDate.length !== 10) return -1;
  const y = Number(isoDate.slice(0, 4));
  const mo = Number(isoDate.slice(5, 7)) - 1;
  const da = Number(isoDate.slice(8, 10));
  const d = new Date(y, mo, da, 12, 0, 0, 0);
  const diff = (d.getTime() - birthDate().getTime()) / 86400000;
  return Math.round(diff);
}

// ------------------------------- Typy wpisow -----------------------------------
export function isMilkType(t) {
  return t === 'MLEKO' || t === 'MLEKO_MATKI' || t === 'MLEKO_MODYFIKOWANE';
}

export function milkTypeLabel(t) {
  if (t === 'MLEKO_MATKI') return 'MLEKO MATKI';
  if (t === 'MLEKO_MODYFIKOWANE') return 'MLEKO MODYFIKOWANE';
  return 'MLEKO';
}

// -------------------------- Parser wiersza CSV (4 lub 6 kolumn) -----------------
export function parseCsvLine(line) {
  const first = line.indexOf(',');
  const second = line.indexOf(',', first + 1);
  const third = line.indexOf(',', second + 1);
  if (first < 0 || second < 0 || third < 0) return null;
  const entry = {
    date: line.slice(0, first),
    time: line.slice(first + 1, second),
    type: line.slice(second + 1, third),
    ml: 0,
    piersLeft: 0,
    piersRight: 0,
  };
  const fourth = line.indexOf(',', third + 1);
  entry.ml = parseInt(fourth < 0 ? line.slice(third + 1) : line.slice(third + 1, fourth), 10) || 0;
  if (fourth < 0) return entry;
  const fifth = line.indexOf(',', fourth + 1);
  entry.piersLeft = parseInt(line.slice(fourth + 1, fifth < 0 ? line.length : fifth), 10) || 0;
  if (fifth < 0) return entry;
  entry.piersRight = parseInt(line.slice(fifth + 1), 10) || 0;
  return entry;
}

export function describeCsvEntry(e) {
  const shortDate = `${e.date.slice(8, 10)}.${e.date.slice(5, 7)}. `;
  let t = `${shortDate}${e.time} `;
  if (isMilkType(e.type)) {
    t += `${milkTypeLabel(e.type)} ${e.ml} ml`;
  } else if (e.type === 'KARMIENIE') {
    t += 'KARMIENIE';
    if (e.piersLeft > 0 || e.piersRight > 0) t += ` L${e.piersLeft}/P${e.piersRight}`;
  } else {
    t += e.type;
  }
  return t;
}

// Format wpisu do UI (pole lastFeeding/lastMilk) — odpowiednik formatEntryForUi().
function formatEntryForUi(e) {
  const datePl = `${e.date.slice(8, 10)}.${e.date.slice(5, 7)}.${e.date.slice(0, 4)}`;
  if (e.type === 'KARMIENIE' && e.ml === 0) return `${datePl}  ${e.time}\nKARMIENIE`;
  const prefix = isMilkType(e.type) ? `${milkTypeLabel(e.type)} | ` : '';
  return `${datePl}  ${e.time}\n${prefix}${e.ml} ml`;
}

// --------------------------- Interpolacja tabel Napper --------------------------
function interpTable(x, xs, ys) {
  const n = xs.length;
  if (n <= 0) return 0;
  if (x <= xs[0]) return ys[0];
  if (x >= xs[n - 1]) return ys[n - 1];
  for (let i = 1; i < n; i++) {
    if (x <= xs[i]) {
      const x0 = xs[i - 1], x1 = xs[i], y0 = ys[i - 1], y1 = ys[i];
      if (x1 === x0) return Math.trunc(y0);
      return Math.trunc(y0 + ((y1 - y0) * (x - x0)) / (x1 - x0));
    }
  }
  return ys[n - 1];
}

export function wakeWindowMinutes(ageDays) {
  if (ageDays < 0) ageDays = 0;
  let minMin = interpTable(ageDays, C.WAKE_WIN_AGE_DAYS, C.WAKE_WIN_MIN_MINUTES);
  let maxMin = interpTable(ageDays, C.WAKE_WIN_AGE_DAYS, C.WAKE_WIN_MAX_MINUTES);
  if (maxMin < minMin) maxMin = minMin;
  return { minMin, maxMin };
}

export function sleepNeedMinutes(ageDays) {
  if (ageDays < 0) ageDays = 0;
  return {
    night: interpTable(ageDays, C.SLEEP_NEED_AGE_DAYS, C.SLEEP_NEED_NIGHT_MIN),
    day: interpTable(ageDays, C.SLEEP_NEED_AGE_DAYS, C.SLEEP_NEED_DAY_MIN),
  };
}

export function napTargetCount(ageDays) {
  if (ageDays < 0) ageDays = 0;
  return interpTable(ageDays, C.NAP_TARGET_AGE_DAYS, C.NAP_TARGET_NAPS);
}

function sleepHourIsNight(hour) {
  return hour >= C.SLEEP_NIGHT_START_HOUR || hour < C.SLEEP_NIGHT_END_HOUR;
}

// ------------------------------- Odczyt pliku CSV -------------------------------
function readLines() {
  const full = path.resolve(C.DATA_FILE);
  let text;
  try {
    text = fs.readFileSync(full, 'utf-8');
  } catch {
    return [];
  }
  const lines = text.split('\n');
  if (lines.length) lines.shift(); // pomijamy naglowek
  return lines;
}

// Zwraca wszystkie sparsowane wpisy z zachowaniem FIZYCZNEGO indeksu linii
// (dataIndex liczony dla KAZDEJ linii po naglowku — takze pustych/niepoprawnych),
// spojnie z /api/entries i deleteEntryByIndex firmware.
function readEntriesWithIndex() {
  const lines = readLines();
  const out = [];
  for (let dataIndex = 0; dataIndex < lines.length; dataIndex++) {
    let line = lines[dataIndex];
    // odetnij CR
    while (line.length && (line.endsWith('\r') || line.endsWith('\n'))) line = line.slice(0, -1);
    const trimmed = line.trim();
    if (trimmed.length === 0) continue;
    const e = parseCsvLine(trimmed);
    if (!e) continue;
    e.lineIndex = dataIndex;
    out.push(e);
  }
  return out;
}

// ---------------------------- Ostatnie wpisy / rytm -----------------------------
export function loadLatestEntries(now = new Date()) {
  const res = {
    lastFeeding: 'Brak zapisanego wpisu',
    lastMilk: 'Brak zapisanego wpisu',
    lastFeedingTime: null,
    lastMilkTime: null,
    lastWeightG: 0,
    sleepInProgress: false,
    sleepStartedTime: null,
    lastWakeTime: null,
    avgFeedingGapMin: 0,
    longestFeedingGapMin: 0,
    todayFeedingCount: 0,
    nextFeedingEta: null,
  };
  const entries = readEntriesWithIndex();
  const today = dateIso(dayOffsetFromToday(0, now));
  let prevFeedingToday = null;
  let sumGapMin = 0;
  let gapCount = 0;
  let sawSleepStart = false;

  for (const e of entries) {
    const stamp = csvDateTimeToDate(e.date, e.time);
    if (e.type === 'KARMIENIE') {
      res.lastFeeding = formatEntryForUi(e);
      res.lastFeedingTime = stamp;
      if (e.date === today) {
        res.todayFeedingCount += 1;
        if (prevFeedingToday) {
          const gap = Math.floor((stamp.getTime() - prevFeedingToday.getTime()) / 60000);
          if (gap > 0) {
            sumGapMin += gap;
            gapCount += 1;
            if (gap > res.longestFeedingGapMin) res.longestFeedingGapMin = gap;
          }
        }
        prevFeedingToday = stamp;
      }
    }
    if (isMilkType(e.type)) {
      res.lastMilk = formatEntryForUi(e);
      res.lastMilkTime = stamp;
    }
    if (e.type === 'WAGA') res.lastWeightG = e.ml;
    if (e.type === 'SEN_START') {
      res.sleepInProgress = true;
      res.sleepStartedTime = stamp;
      sawSleepStart = true;
    } else if (e.type === 'SEN_STOP') {
      res.sleepInProgress = false;
      res.sleepStartedTime = null;
      if (sawSleepStart) {
        res.lastWakeTime = stamp;
        sawSleepStart = false;
      }
    }
  }
  if (gapCount >= 1) res.avgFeedingGapMin = Math.trunc(sumGapMin / gapCount);
  if (res.lastFeedingTime) {
    res.nextFeedingEta = new Date(res.lastFeedingTime.getTime() + C.COUNTER_BLINK_MIN * 60000);
  }
  return res;
}

// ------------------------ Statystyki dnia (jeden przebieg) ----------------------
function emptyDaySummary() {
  return {
    feedingCount: 0, milkCount: 0, milkMl: 0, motherMilkMl: 0, modifiedMilkMl: 0,
    piersLeftMin: 0, piersRightMin: 0, diaperWet: 0, diaperDirty: 0, pumpingMl: 0,
    vitaminD: false, weightG: 0, sleepDayMin: 0, sleepNightMin: 0, napCount: 0,
  };
}

// Rozdziela sen [start,stop] na minuty nocne/dzienne po dniach z okna iso[].
function accrueSleepInterval(start, stop, iso, stats) {
  if (stop.getTime() <= start.getTime()) return;
  let cur = new Date(start.getTime());
  let guard = 0;
  while (cur.getTime() < stop.getTime() && guard++ < 4000) {
    const night = sleepHourIsNight(cur.getHours());
    // krok do najblizszej pelnej godziny
    let nextHour;
    if (cur.getMinutes() === 0 && cur.getSeconds() === 0) {
      nextHour = new Date(cur.getTime() + 3600000);
    } else {
      nextHour = new Date(cur.getTime() + (3600 - (cur.getMinutes() * 60 + cur.getSeconds())) * 1000);
    }
    const segEnd = nextHour.getTime() < stop.getTime() ? nextHour : stop;
    const segMin = Math.floor((segEnd.getTime() - cur.getTime()) / 60000);
    if (segMin > 0) {
      const segDate = dateIso(beginningOfDay(cur));
      for (let i = 0; i < iso.length; i++) {
        if (segDate === iso[i]) {
          if (night) stats[i].sleepNightMin += segMin;
          else stats[i].sleepDayMin += segMin;
          break;
        }
      }
    }
    cur = new Date(segEnd.getTime());
  }
}

function accrueNapCount(start, iso, stats) {
  if (!start) return;
  if (sleepHourIsNight(start.getHours())) return; // sen nocny nie jest drzemka
  const startDate = dateIso(beginningOfDay(start));
  for (let i = 0; i < iso.length; i++) {
    if (startDate === iso[i]) {
      stats[i].napCount += 1;
      return;
    }
  }
}

// Buduje statystyki dla okna STATS_DAY_COUNT dni (dzis + 7 wstecz).
const STATS_DAY_COUNT = 8;
export function buildDayStats(now = new Date()) {
  const iso = [];
  const days = [];
  const stats = [];
  for (let i = 0; i < STATS_DAY_COUNT; i++) {
    const d = dayOffsetFromToday(i, now);
    days.push(d);
    iso.push(dateIso(d));
    stats.push(emptyDaySummary());
  }
  const entries = readEntriesWithIndex();
  let openSleepStart = null;
  for (const e of entries) {
    if (e.type === 'SEN_START') {
      openSleepStart = csvDateTimeToDate(e.date, e.time);
      accrueNapCount(openSleepStart, iso, stats);
      continue;
    } else if (e.type === 'SEN_STOP') {
      if (openSleepStart) {
        const stop = csvDateTimeToDate(e.date, e.time);
        accrueSleepInterval(openSleepStart, stop, iso, stats);
        openSleepStart = null;
      }
      continue;
    }
    for (let i = 0; i < STATS_DAY_COUNT; i++) {
      if (e.date !== iso[i]) continue;
      const s = stats[i];
      if (e.type === 'KARMIENIE') {
        s.feedingCount += 1;
        s.piersLeftMin += e.piersLeft;
        s.piersRightMin += e.piersRight;
      } else if (isMilkType(e.type)) {
        s.milkCount += 1;
        s.milkMl += e.ml;
        if (e.type === 'MLEKO_MATKI') s.motherMilkMl += e.ml;
        else if (e.type === 'MLEKO_MODYFIKOWANE') s.modifiedMilkMl += e.ml;
      } else if (e.type === 'PIELUCHA_MOKRA') {
        s.diaperWet += 1;
      } else if (e.type === 'PIELUCHA_BRUDNA') {
        s.diaperDirty += 1;
      } else if (e.type === 'ODCIAGANIE') {
        s.pumpingMl += e.ml;
      } else if (e.type === 'WITAMINA_D') {
        s.vitaminD = true;
      } else if (e.type === 'WAGA') {
        s.weightG = e.ml;
      }
      break;
    }
  }
  // Sen trwajacy do teraz (brak STOP): dolicz do biezacej chwili.
  if (openSleepStart) accrueSleepInterval(openSleepStart, now, iso, stats);
  return { iso, days, stats };
}

export function dayStatsForOffset(offset, now = new Date()) {
  const { stats } = buildDayStats(now);
  return stats[offset] || emptyDaySummary();
}

export function calendarDayTitle(d, index) {
  const dd = `${pad2(d.getDate())}.${pad2(d.getMonth() + 1)}.${d.getFullYear()}`;
  if (index === 0) return `DZISIAJ - ${dd}`;
  if (index === 1) return `WCZORAJ - ${dd}`;
  if (index === 2) return `2 DNI TEMU - ${dd}`;
  return `${index} DNI TEMU - ${dd}`;
}

export function formatAgoText(then, now = new Date()) {
  if (!then) return '';
  let delta = Math.floor((now.getTime() - then.getTime()) / 1000);
  if (delta < -60) return 'w przyszlosci';
  if (delta < 0) delta = 0;
  const hours = Math.floor(delta / 3600);
  const minutes = Math.floor((delta % 3600) / 60);
  if (hours === 0) return `${minutes} min temu`;
  return `${hours} godz. ${minutes} min temu`;
}

// -------------------------------- Zapis / import --------------------------------
// Buforowany zapis wiersza (append) — odpowiednik appendEntry.
export function appendEntry(type, when, ml, piersLeft = -1, piersRight = -1) {
  const date = dateIso(when);
  const time = `${pad2(when.getHours())}:${pad2(when.getMinutes())}`;
  let row;
  if (piersLeft >= 0 || piersRight >= 0) {
    row = `${date},${time},${type},${ml},${Math.max(piersLeft, 0)},${Math.max(piersRight, 0)}\n`;
  } else {
    row = `${date},${time},${type},${ml}\n`;
  }
  ensureDataFile();
  fs.appendFileSync(path.resolve(C.DATA_FILE), row);
  return true;
}

// Usuwa wpis o fizycznym indeksie (dataIndex) — spojnie z /api/entries.
export function deleteEntryByIndex(entryIndex) {
  if (entryIndex < 0) return { ok: false };
  const full = path.resolve(C.DATA_FILE);
  let text;
  try {
    text = fs.readFileSync(full, 'utf-8');
  } catch {
    return { ok: false };
  }
  const lines = text.split('\n');
  const header = lines.shift();
  // Plik konczacy sie '\n' daje pusty ostatni element splita — to NIE jest wiersz danych
  // (firmware iteruje while(available()) i nigdy go nie widzi). Usuwamy go raz, po pozycji.
  if (lines.length && lines[lines.length - 1] === '') lines.pop();

  const kept = [];
  let removedDescription = '';
  let removedFound = false;
  // dataIndex = fizyczny indeks wiersza po naglowku, liczony dla KAZDEJ linii (takze
  // pustej/niepoprawnej) — spojnie z handleApiEntries.lineIndex i deleteEntryByIndex firmware.
  for (let dataIndex = 0; dataIndex < lines.length; dataIndex++) {
    let raw = lines[dataIndex];
    while (raw.length && raw.endsWith('\r')) raw = raw.slice(0, -1);
    if (dataIndex === entryIndex) {
      const e = parseCsvLine(raw.trim());
      if (e) removedDescription = describeCsvEntry(e);
      removedFound = true; // pomijamy ten wiersz w zapisie
    } else {
      kept.push(raw);
    }
  }
  if (!removedFound || entryIndex >= lines.length) return { ok: false };
  const body = kept.join('\n');
  fs.writeFileSync(full, `${header}\n${body}${body.length ? '\n' : ''}`);
  return { ok: true, removed: removedDescription };
}

export function ensureDataFile() {
  const full = path.resolve(C.DATA_FILE);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  if (!fs.existsSync(full)) fs.writeFileSync(full, `${C.CSV_HEADER}\n`);
}

export function readRawCsv() {
  try {
    return fs.readFileSync(path.resolve(C.DATA_FILE), 'utf-8');
  } catch {
    return `${C.CSV_HEADER}\n`;
  }
}

// Import: sanityzacja jak w firmware — kanoniczny naglowek, akceptuj tylko parsowalne
// wiersze do 160 B. Robi backup obecnego pliku. Zwraca {imported, skipped}.
export function importCsv(rawText) {
  const full = path.resolve(C.DATA_FILE);
  ensureDataFile();
  // backup
  try {
    fs.copyFileSync(full, path.resolve(C.BACKUP_FILE));
  } catch { /* brak pliku do backupu — pomijamy */ }
  const lines = rawText.split(/\r?\n/);
  const out = [C.CSV_HEADER];
  let imported = 0;
  let skipped = 0;
  let sawHeader = false;
  for (const line of lines) {
    const t = line.trim();
    if (t.length === 0) continue;
    if (!sawHeader && t.startsWith('data,')) { sawHeader = true; continue; }
    if (t.length > 160) { skipped += 1; continue; }
    const e = parseCsvLine(t);
    if (!e) { skipped += 1; continue; }
    out.push(t);
    imported += 1;
  }
  if (imported === 0) return { ok: false, imported: 0, skipped };
  fs.writeFileSync(full, `${out.join('\n')}\n`);
  return { ok: true, imported, skipped };
}

// Wpisy dla danego dnia (dla /api/entries) — z fizycznym lineIndex.
export function entriesForDate(targetDate) {
  const entries = readEntriesWithIndex();
  return entries
    .filter((e) => e.date.startsWith(targetDate))
    .map((e) => ({
      time: e.time,
      type: e.type,
      label: isMilkType(e.type) ? milkTypeLabel(e.type) : e.type,
      ml: e.ml,
      piersLeftMin: e.piersLeft,
      piersRightMin: e.piersRight,
      lineIndex: e.lineIndex,
    }));
}

// Seria pomiarow wagi (dla /api/weight-series).
export function weightSeries() {
  const entries = readEntriesWithIndex();
  const points = [];
  for (const e of entries) {
    if (e.type !== 'WAGA') continue;
    const dol = dayOfLifeForDate(e.date);
    if (dol < 0) continue;
    points.push({ day: dol, date: e.date, g: e.ml });
  }
  return points;
}
