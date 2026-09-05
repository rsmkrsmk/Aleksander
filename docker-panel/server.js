// Testowa kopia panelu WWW urzadzenia Aleksander (ESP32) w Node.js + Express.
// Odtwarza kontrakt HTTP 1:1 na podstawie pliku CSV (data/karmienia.csv).
// Pominieto funkcje SPRZETOWE urzadzenia (Telegram, pogoda, watchdog, mDNS) —
// to swiadomie tylko warstwa panelu do testow, baza pod dalsze prace.
import express from 'express';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import * as C from './config.js';
import * as store from './store.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const app = express();

// Ciala: formularze (x-www-form-urlencoded) oraz surowy CSV (dla /api/import).
app.use(express.urlencoded({ extended: false }));
app.use(express.text({ type: ['text/csv', 'text/plain'], limit: '1mb' }));

// Wspolny naglowek jak sendJson() firmware.
function sendJson(res, status, obj) {
  res.set('Cache-Control', 'no-store, max-age=0');
  res.status(status).type('application/json; charset=utf-8').send(JSON.stringify(obj));
}

// --------------------------- Ustawienia (sleepTelegram) -------------------------
function loadSettings() {
  const settings = { sleepTelegram: false };
  try {
    const text = fs.readFileSync(path.resolve(C.SETTINGS_FILE), 'utf-8');
    for (const line of text.split('\n')) {
      const eq = line.indexOf('=');
      if (eq <= 0) continue;
      const key = line.slice(0, eq).trim();
      const val = line.slice(eq + 1).trim();
      if (key === 'sleepTelegram') settings.sleepTelegram = Number(val) !== 0;
    }
  } catch { /* brak pliku = domyslne */ }
  return settings;
}

function saveSettings(settings) {
  fs.mkdirSync(path.dirname(path.resolve(C.SETTINGS_FILE)), { recursive: true });
  fs.writeFileSync(path.resolve(C.SETTINGS_FILE), `sleepTelegram=${settings.sleepTelegram ? 1 : 0}\n`);
}

// -------------------------------- GET / -----------------------------------------
app.get('/', (_req, res) => {
  res.set('Cache-Control', 'no-store, max-age=0');
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// ------------------------------ GET /api/status ---------------------------------
app.get('/api/status', (_req, res) => {
  const now = new Date();
  const latest = store.loadLatestEntries(now);
  const ageDays = store.calculateAgeDays(now);
  const ww = store.wakeWindowMinutes(ageDays);
  const need = store.sleepNeedMinutes(ageDays);
  const { stats } = store.buildDayStats(now);
  const today = stats[0];
  const settings = loadSettings();

  // Sen: stan biezacy + predykcja okna.
  let napStart = null;
  let napEnd = null;
  let sleepState = 'brak';
  if (latest.sleepInProgress) {
    sleepState = 'spi';
  } else if (latest.lastWakeTime) {
    napStart = new Date(latest.lastWakeTime.getTime() + ww.minMin * 60000);
    napEnd = new Date(latest.lastWakeTime.getTime() + ww.maxMin * 60000);
    if (now.getTime() < napStart.getTime()) sleepState = 'czuwa';
    else if (now.getTime() <= napEnd.getTime()) sleepState = 'okno';
    else sleepState = 'przekroczone';
  }
  const sleepSinceMin = latest.sleepInProgress && latest.sleepStartedTime
    ? Math.floor((now.getTime() - latest.sleepStartedTime.getTime()) / 60000) : -1;
  const wakeSinceMin = (!latest.sleepInProgress && latest.lastWakeTime)
    ? Math.floor((now.getTime() - latest.lastWakeTime.getTime()) / 60000) : -1;

  const nightActive = (() => {
    const h = now.getHours();
    return h >= C.NIGHT_START_HOUR || h < C.NIGHT_END_HOUR;
  })();

  const calendar = [];
  for (let i = 0; i < 5; i++) {
    const d = store.dayOffsetFromToday(i, now);
    const s = stats[i];
    calendar.push({
      date: store.dateIso(d),
      label: store.calendarDayTitle(d, i),
      feedingCount: s.feedingCount,
      milkMl: s.milkMl,
      motherMilkMl: s.motherMilkMl,
      modifiedMilkMl: s.modifiedMilkMl,
      piersLeftMin: s.piersLeftMin,
      piersRightMin: s.piersRightMin,
      diaperWet: s.diaperWet,
      diaperDirty: s.diaperDirty,
      pumpingMl: s.pumpingMl,
      vitaminD: s.vitaminD,
      weightG: s.weightG,
    });
  }

  const payload = {
    now: store.formatDateTime(now),
    nowIso: store.webDateTime(now),
    ip: 'docker-panel',
    age: store.calculateAgeText(now),
    developmentTip: store.developmentTipForToday(now),
    developmentDay: ageDays,
    lastFeeding: latest.lastFeeding,
    lastMilk: latest.lastMilk,
    lastFeedingAgo: latest.lastFeedingTime ? store.formatAgoText(latest.lastFeedingTime, now) : '',
    lastFeedingAgeMin: latest.lastFeedingTime
      ? Math.floor((now.getTime() - latest.lastFeedingTime.getTime()) / 60000) : -1,
    avgFeedingGapMin: latest.avgFeedingGapMin,
    nextFeedingIso: latest.nextFeedingEta ? store.webDateTime(latest.nextFeedingEta) : '',
    longestFeedingGapMin: latest.longestFeedingGapMin,
    sleepInProgress: latest.sleepInProgress,
    sleepState,
    sleepSinceMin,
    wakeSinceMin,
    wakeWindowMinMin: ww.minMin,
    wakeWindowMaxMin: ww.maxMin,
    nextNapStartIso: napStart ? store.webDateTime(napStart) : '',
    nextNapEndIso: napEnd ? store.webDateTime(napEnd) : '',
    sleepDayMin: today.sleepDayMin,
    sleepNightMin: today.sleepNightMin,
    sleepNeedDayMin: need.day,
    sleepNeedNightMin: need.night,
    napCount: today.napCount,
    napTarget: store.napTargetCount(ageDays),
    sleepTelegram: settings.sleepTelegram,
    wifi: true,
    storage: true,
    dataFileHuge: false,
    timeValid: true,
    minMl: C.ML_MIN,
    maxMl: C.ML_MAX,
    defaultMl: C.DEFAULT_ML,
    birthWeightG: C.BIRTH_WEIGHT_G,
    lastWeightG: latest.lastWeightG,
    calendar,
    night: nightActive,
    mdns: 'karmienie.local',
    undoWindowSec: 60,
    // Sysinfo / diagnostyka — w kopii Docker to wartosci zastepcze (nie ma sprzetu ESP32).
    freeHeap: 0,
    totalHeap: 0,
    freePsram: 0,
    totalPsram: 0,
    maxAlloc: 0,
    uptimeSec: Math.floor(process.uptime()),
    cpuLoad: 0,
    minFreeHeap: 0,
    rssi: 0,
    httpRequests: 0,
    bootCount: 0,
    watchdogResets: 0,
    watchdogReady: false,
    resetReason: 'docker',
  };
  sendJson(res, 200, payload);
});

// ----------------------------- GET /api/entries ---------------------------------
app.get('/api/entries', (req, res) => {
  const date = req.query.date;
  if (!date) return sendJson(res, 400, { message: 'Brakuje daty.' });
  const day = store.parseWebDateTime(`${date}T12:00`);
  if (!day) return sendJson(res, 400, { message: 'Nieprawidłowy format daty.' });
  const targetDate = store.dateIso(day);
  const entries = store.entriesForDate(targetDate);
  sendJson(res, 200, { date: targetDate, entries });
});

// -------------------------- GET /api/weight-series ------------------------------
app.get('/api/weight-series', (_req, res) => {
  const points = store.weightSeries();
  sendJson(res, 200, { birthWeightG: C.BIRTH_WEIGHT_G, points });
});

// ------------------------------ POST /api/entry ---------------------------------
app.post('/api/entry', (req, res) => {
  const type = req.body.type;
  const ml = parseInt(req.body.ml, 10) || 0;
  if (!req.body.type || req.body.when === undefined || req.body.ml === undefined) {
    return sendJson(res, 400, { message: 'Niepelne dane formularza.' });
  }
  const when = store.parseWebDateTime(req.body.when);
  if (!when) return sendJson(res, 400, { message: 'Nieprawidlowy czas wpisu.' });

  if (store.isMilkType(type)) {
    if (ml < C.ML_MIN || ml > C.ML_MAX) return sendJson(res, 400, { message: 'Nieprawidlowa ilosc mleka.' });
    store.appendEntry(type, when, ml);
    return sendJson(res, 201, { message: 'Wpis mleka zapisany w pamieci urzadzenia.' });
  }
  if (type === 'WAGA') {
    if (ml < C.WEIGHT_MIN_G || ml > C.WEIGHT_MAX_G) return sendJson(res, 400, { message: 'Nieprawidlowa waga (gramy).' });
    store.appendEntry('WAGA', when, ml);
    return sendJson(res, 201, { message: 'Zapisano wage.' });
  }
  if (!store.isMilkType(type) && type !== 'KARMIENIE') {
    const validType = ['ODCIAGANIE', 'PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D'].includes(type);
    if (!validType) return sendJson(res, 400, { message: 'Nieznany typ zdarzenia.' });
    if (type === 'WITAMINA_D') {
      const today = store.dayStatsForOffset(0);
      if (today.vitaminD) return sendJson(res, 200, { message: 'Witamina D juz zapisana dzisiaj.' });
    }
    store.appendEntry(type, when, ml);
    return sendJson(res, 201, { message: 'Zapisano zdarzenie.' });
  }
  // KARMIENIE
  if (type !== 'KARMIENIE' || ml !== 0) {
    return sendJson(res, 400, { message: 'Karmienie nie wymaga ilosci ml; podaj ja tylko dla Butelki.' });
  }
  const extraMilk = req.body.extraMilk === '1';
  let milkType = '';
  let milkMl = 0;
  if (extraMilk) {
    if (req.body.milkType === undefined || req.body.milkMl === undefined) {
      return sendJson(res, 400, { message: 'Brakuje typu lub ilosci dodatkowego mleka.' });
    }
    milkType = req.body.milkType;
    milkMl = parseInt(req.body.milkMl, 10) || 0;
    if ((milkType !== 'MLEKO_MATKI' && milkType !== 'MLEKO_MODYFIKOWANE') || milkMl < C.ML_MIN || milkMl > C.ML_MAX) {
      return sendJson(res, 400, { message: 'Nieprawidlowe dodatkowe mleko.' });
    }
  }
  const clamp = (v, lo, hi) => Math.min(Math.max(parseInt(v, 10) || 0, lo), hi);
  const piersLeftMin = clamp(req.body.lewaMin, 0, 120);
  const piersRightMin = clamp(req.body.prawaMin, 0, 120);
  store.appendEntry('KARMIENIE', when, ml, piersLeftMin, piersRightMin);
  if (extraMilk) store.appendEntry(milkType, when, milkMl);
  sendJson(res, 201, extraMilk
    ? { message: 'Zapisano karmienie i dodatkowe mleko.' }
    : { message: 'Karmienie zapisane w pamieci urzadzenia.' });
});

// -------------------------- POST /api/delete-entry ------------------------------
app.post('/api/delete-entry', (req, res) => {
  if (req.body.line === undefined) return sendJson(res, 400, { message: 'Brakuje indeksu linii do usuniecia.' });
  const lineIndex = parseInt(req.body.line, 10);
  const result = store.deleteEntryByIndex(Number.isNaN(lineIndex) ? -1 : lineIndex);
  if (!result.ok) return sendJson(res, 400, { message: 'Nie udalo sie usunac wpisu.' });
  sendJson(res, 200, { message: 'Usunieto wpis.', removed: result.removed });
});

// --------------------------- POST /api/send-backup ------------------------------
// W kopii Docker nie ma Telegrama — zwracamy komunikat zgodny z kontraktem.
app.post('/api/send-backup', (_req, res) => {
  sendJson(res, 400, { message: 'Telegram nie jest skonfigurowany (kopia testowa Docker).' });
});

// ------------------------------ POST /api/event ---------------------------------
app.post('/api/event', (req, res) => {
  const type = req.body.type;
  const validType = ['PIELUCHA_MOKRA', 'PIELUCHA_BRUDNA', 'WITAMINA_D', 'ODCIAGANIE', 'WAGA', 'SEN_START', 'SEN_STOP'].includes(type);
  if (!validType) return sendJson(res, 400, { message: 'Nieznany typ zdarzenia.' });

  let when = new Date();
  if (req.body.when !== undefined) {
    const parsed = store.parseWebDateTime(req.body.when);
    if (!parsed) return sendJson(res, 400, { message: 'Nieprawidlowy czas zdarzenia.' });
    when = parsed;
  }

  if (type === 'WAGA') {
    const grams = parseInt(req.body.ml, 10) || 0;
    if (grams < C.WEIGHT_MIN_G || grams > C.WEIGHT_MAX_G) return sendJson(res, 400, { message: 'Nieprawidlowa waga (gramy).' });
    store.appendEntry('WAGA', when, grams);
    return sendJson(res, 201, { message: 'Zapisano wage.' });
  }

  let ml = Math.min(Math.max(parseInt(req.body.ml, 10) || 0, 0), C.ML_MAX);
  if (type === 'ODCIAGANIE' && ml < C.ML_MIN) return sendJson(res, 400, { message: 'Podaj ilosc odciagnietego mleka.' });
  if (type === 'WITAMINA_D') {
    const today = store.dayStatsForOffset(0);
    if (today.vitaminD) return sendJson(res, 200, { message: 'Witamina D juz zapisana dzisiaj.' });
    ml = 0;
  }
  store.appendEntry(type, when, ml);
  sendJson(res, 201, { message: 'Zapisano zdarzenie.' });
});

// ------------------------------- GET /export.csv --------------------------------
app.get('/export.csv', (_req, res) => {
  res.set('Cache-Control', 'no-store, max-age=0');
  res.set('Content-Disposition', 'attachment; filename=karmienia.csv');
  res.type('text/csv; charset=utf-8').send(store.readRawCsv());
});

// ------------------------------ POST /api/import --------------------------------
app.post('/api/import', (req, res) => {
  const raw = typeof req.body === 'string' ? req.body : '';
  if (!raw || raw.length === 0) return sendJson(res, 400, { message: 'Pusty plik importu.' });
  // Limit liczony w BAJTACH (jak firmware body.length()), nie w jednostkach UTF-16.
  if (Buffer.byteLength(raw, 'utf8') > 512 * 1024) return sendJson(res, 400, { message: 'Plik jest za duzy (limit 512 KB).' });
  const result = store.importCsv(raw);
  if (!result.ok) return sendJson(res, 400, { message: 'Brak poprawnych wierszy do importu.' });
  const msg = result.skipped > 0
    ? `Zaimportowano ${result.imported} wpisow. Pominieto ${result.skipped} niepoprawnych.`
    : `Zaimportowano ${result.imported} wpisow.`;
  sendJson(res, 200, { message: msg });
});

// ------------------------------ POST /api/setting -------------------------------
app.post('/api/setting', (req, res) => {
  const key = req.body.key;
  const value = req.body.value;
  if (key !== 'sleepTelegram') return sendJson(res, 400, { message: 'Nieznane ustawienie.' });
  const settings = loadSettings();
  settings.sleepTelegram = (parseInt(value, 10) || 0) !== 0;
  saveSettings(settings);
  sendJson(res, 200, { message: 'Zapisano.', sleepTelegram: settings.sleepTelegram });
});

// --------------------------------- 404 ------------------------------------------
app.use((req, res) => {
  if (req.path.startsWith('/api/')) return sendJson(res, 404, { message: 'Nie znaleziono adresu API.' });
  res.status(404).type('text/plain; charset=utf-8').send('Nie znaleziono strony.');
});

store.ensureDataFile();
app.listen(C.PORT, () => {
  console.log(`Panel Aleksander (kopia Docker) nasłuchuje na porcie ${C.PORT}`);
  console.log(`Dane: ${path.resolve(C.DATA_FILE)}`);
});
