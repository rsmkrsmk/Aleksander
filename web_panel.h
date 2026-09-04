#pragma once

// Panel jest osadzony w pamięci programu. Nie wymaga dostępu do Internetu ani zewnętrznych CDN.
const char WEB_APP_HTML[] PROGMEM = R"WEBPANEL(
<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#527F49">
<title>Leśny Dziennik Aleksandra</title>
<style>
:root{--paper:#F6F8F1;--surface:#FFFFFF;--tonal:#E6F1E0;--ink:#243528;--muted:#4E6252;--moss:#356D43;--berry:#3E5E9B;--acorn:#A85432;--line:#CBDFC4;--danger:#B84C4C;--shadow:0 8px 24px rgba(37,61,40,.12);--shadow-soft:0 2px 7px rgba(37,61,40,.10)}
*{box-sizing:border-box}body{margin:0;transition:background .3s ease,color .3s ease;background:radial-gradient(circle at 100% 0,#e8f2e2 0,transparent 33%),linear-gradient(145deg,#fbfcf8,var(--paper));color:var(--ink);font-family:Roboto,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;font-size:16px;line-height:1.35}.app{width:min(100%,680px);margin:auto;padding:max(14px,env(safe-area-inset-top)) 14px calc(24px + env(safe-area-inset-bottom))}body.night{--paper:#161B18;--surface:#26302A;--tonal:#24322A;--ink:#E8EFE4;--muted:#9DB3A0;--moss:#7FB88A;--berry:#7C9BD1;--acorn:#D98B5F;--line:#3A473E;background:#141a17}body.night .hero,body.night .development,body.night .extra-milk,body.night .nursing{background:none;border-color:var(--line)}body.night .entry{background:#1d2521}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin:0 5px 1px 0;vertical-align:middle}.dot.ok{background:#3f9d55}.dot.err{background:#c0392b}body.night .dot.ok{background:#7FB88A}.hero{display:flex;justify-content:space-between;align-items:flex-start;gap:10px;padding:17px 18px;margin:0 0 14px;background:linear-gradient(135deg,#eff7e9,#e3f0dc);border:1px solid var(--line);border-radius:28px;box-shadow:var(--shadow-soft)}.hero-line{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}.hero h1{font-size:1.06rem;line-height:1.18;margin:0;font-weight:850;letter-spacing:.025em}.hero p{margin:0;color:var(--muted);font-weight:750;font-size:.82rem}.friends{font-size:2rem;letter-spacing:-.42rem;white-space:nowrap;padding:3px 9px;background:rgba(255,255,255,.58);border-radius:18px}.ip{display:inline-block;margin-top:8px;padding:5px 10px;border-radius:999px;background:var(--surface);color:var(--moss);font-size:.76rem;font-weight:800;word-break:break-all;box-shadow:var(--shadow-soft)}.card{background:var(--surface);border:1px solid var(--line);border-radius:24px;box-shadow:var(--shadow-soft);padding:16px;margin:12px 0}.age{border:0;background:var(--tonal);text-align:center;font-weight:800;color:var(--moss);white-space:pre-line;box-shadow:none}.since{border-radius:18px;background:var(--tonal);text-align:center;font-weight:900;letter-spacing:.03em;color:var(--moss);padding:13px;margin:-4px 0 12px;font-size:.98rem}.development{background:linear-gradient(135deg,#f5faef,#e7f2e0);border-color:#c6dcbd;padding:15px 16px}.development h2{font-size:.73rem;letter-spacing:.09em;margin:0 0 8px;color:var(--moss);font-weight:850}.development p{margin:0;color:var(--ink);font-size:.9rem;line-height:1.42}.development .development-note{margin-top:8px;color:var(--muted);font-size:.72rem;font-weight:650}.two{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.info h2,.section-title{font-size:.73rem;letter-spacing:.09em;margin:0 0 9px;color:var(--ink);font-weight:850}.info.feeding h2,.info.milk h2{color:var(--ink)}.latest{margin:0;white-space:pre-line;min-height:44px;font-size:.9rem;font-weight:580}.actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;margin:14px 0 18px}.actions .wide{grid-column:1/-1}button{appearance:none;border:0;border-radius:18px;min-height:56px;padding:12px 14px;background:var(--moss);color:#fff;font:inherit;font-size:.88rem;font-weight:850;letter-spacing:.01em;cursor:pointer;box-shadow:0 5px 12px rgba(50,92,48,.23);transition:transform .14s ease,filter .14s ease,box-shadow .14s ease}button:hover{filter:brightness(1.03)}button:active{transform:translateY(2px) scale(.985);box-shadow:0 2px 5px rgba(50,92,48,.22)}button.feed{background:var(--acorn)}button.milk{background:var(--berry)}button.muted{background:var(--muted)}button.small{min-height:40px;border-radius:14px;padding:8px 12px;font-size:.77rem;box-shadow:var(--shadow-soft)}.status{color:var(--moss);background:var(--tonal);border-radius:999px;font-size:.76rem;font-weight:750;text-align:center;padding:7px 11px;margin:2px 0 14px}.status.error{color:var(--danger);background:#FCE8E8}.sysinfo{text-align:center;color:var(--muted);font-size:.7rem;font-weight:650;margin:-8px 0 6px;padding:0 8px}.hidden{display:none!important}.panel-head{display:flex;justify-content:space-between;gap:10px;align-items:center;margin:22px 2px 10px}.panel-head h2{font-size:1.05rem;margin:0;font-weight:850}.calendar-day{padding:15px}.calendar-top{display:flex;align-items:center;justify-content:space-between;gap:8px}.calendar-date{font-weight:850;font-size:.92rem}.calendar-summary{margin:12px 0;color:var(--muted);font-size:.88rem;font-weight:620}.calendar-buttons{display:grid;grid-template-columns:1fr 1fr;gap:9px}.detail-list{display:grid;gap:9px}.entry{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:12px;border:1px solid var(--line);border-radius:17px;background:#F9FCF7;box-shadow:0 1px 2px rgba(37,61,40,.05)}.delete-btn{background:none;border:1px solid var(--danger);color:var(--danger);border-radius:50%;width:24px;height:24px;font-size:12px;line-height:1;padding:0;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0;margin-left:6px}.delete-btn:hover{background:var(--danger);color:#fff}.entry-type{font-size:.72rem;font-weight:850;letter-spacing:.04em}.entry.feed .entry-type,.entry.milk .entry-type{color:var(--ink)}.entry-time{font-weight:780}.entry-ml{font-weight:850}.empty{padding:16px;color:var(--muted);text-align:center}.chart-wrap{height:245px;display:flex;align-items:flex-end;gap:5px;padding:20px 2px 28px;border-bottom:2px solid var(--line)}.bar-unit{height:100%;flex:1;min-width:0;display:flex;flex-direction:column;justify-content:flex-end;align-items:center;position:relative}.bar-value{font-size:.66rem;font-weight:850;white-space:nowrap;margin-bottom:5px}.bar-pair{height:100%;width:100%;display:flex;align-items:flex-end;justify-content:center;gap:3px}.bar{width:min(13px,28%);min-height:3px;border-radius:13px 13px 5px 5px;background:var(--moss);box-shadow:0 3px 7px rgba(50,92,48,.16)}.bar.feed{background:var(--acorn)}.bar.mother{background:var(--moss)}.bar.modified{background:var(--berry)}.bar-date{position:absolute;bottom:-22px;color:var(--muted);font-size:.64rem;white-space:nowrap;transform:rotate(-35deg);transform-origin:top center}.chart-total{text-align:center;color:var(--ink);font-size:.9rem;font-weight:850;margin:15px 0 1px}.chart-subtitle{text-align:center;color:var(--muted);font-size:.78rem;font-weight:800;letter-spacing:.06em;margin:2px 0 0;text-transform:uppercase}.chart-legend{display:flex;justify-content:center;gap:14px;flex-wrap:wrap;font-size:.72rem;color:var(--muted);font-weight:700;margin:2px 0 10px}.chart-legend .dot{width:8px;height:8px}.chart-table{width:100%;border-collapse:collapse;margin:8px 0 4px;font-size:.8rem;color:var(--ink)}.chart-table th{color:var(--muted);font-size:.7rem;font-weight:850;letter-spacing:.04em;text-transform:uppercase;padding:5px 4px;border-bottom:1px solid var(--line)}.chart-table td{padding:6px 4px;text-align:center;border-bottom:1px solid var(--line);font-weight:650}.chart-table td:first-child{font-weight:800;text-align:left}.chart-table .ok{color:var(--moss);font-weight:800}.chart-table .na{color:var(--muted)}.chart-bar-group{display:flex;flex-direction:column;align-items:center;height:100%;width:100%}.chart-note{font-size:.7rem;color:var(--muted);text-align:center;margin-top:14px}
.weight-entry{background:var(--tonal);border-radius:18px;padding:14px}.weight-entry label{display:block;color:var(--muted);font-weight:850;letter-spacing:.04em;font-size:.72rem;margin:0 0 8px}.weight-row{display:flex;align-items:center;gap:8px}.weight-row input{flex:1;min-width:0;padding:12px;border:1px solid var(--line);border-radius:14px;background:var(--surface);color:var(--ink);font:inherit;font-weight:850;font-size:1.15rem;text-align:center}.weight-row button{min-height:46px}.weight-svg{width:100%;height:auto;display:block;border-radius:14px;background:linear-gradient(180deg,var(--surface),var(--tonal));border:1px solid var(--line);margin-top:8px}.weight-svg .grid{stroke:var(--line);stroke-width:1}.weight-svg .axis-txt{fill:var(--muted);font-size:9px;font-weight:700}.weight-svg .exp-band{fill:var(--moss);opacity:.16;stroke:none}.weight-svg .exp-edge{fill:none;stroke:var(--moss);stroke-width:1;opacity:.5}.weight-svg .exp-line{fill:none;stroke:var(--berry);stroke-width:2;stroke-dasharray:5 4;opacity:.9}.weight-svg .gain-band{fill:var(--acorn);opacity:.12;stroke:none}.weight-svg .gain-edge{fill:none;stroke:var(--acorn);stroke-width:1.4;opacity:.65;stroke-dasharray:3 3}.weight-svg .act-line{fill:none;stroke:var(--moss);stroke-width:2.6}.weight-svg .act-dot{fill:var(--moss);stroke:#fff;stroke-width:1.2}body.night .weight-svg .act-dot{stroke:#26302a}.gap-row{display:flex;align-items:center;gap:8px;margin:6px 0}.gap-time{font-size:.72rem;font-weight:800;color:var(--muted);width:82px;flex-shrink:0}.gap-bar-track{flex:1;height:16px;background:var(--tonal);border-radius:9px;overflow:hidden}.gap-bar-fill{height:100%;border-radius:9px;background:linear-gradient(90deg,var(--moss),var(--berry))}.gap-val{font-size:.72rem;font-weight:850;width:64px;text-align:right;flex-shrink:0}.rd-card{background:var(--surface);border:1px solid var(--line);border-radius:16px;padding:12px 14px;margin:0 0 12px;box-shadow:var(--shadow-soft)}
.rd-head{display:flex;justify-content:space-between;align-items:baseline;gap:8px;margin-bottom:8px;padding-bottom:8px;border-bottom:1px solid var(--line)}
.rd-name{font-size:.76rem;font-weight:850;color:var(--ink);letter-spacing:.03em}
.rd-stat{font-size:.72rem;font-weight:650;color:var(--muted)}.rd-stat b{color:var(--moss);font-weight:850}
.rd-empty{font-size:.72rem;color:var(--muted);font-weight:650;padding:4px 0}
.rd-timeline{display:flex;flex-direction:column}
.rd-row{display:grid;grid-template-columns:46px 18px 1fr;align-items:center;gap:8px;min-height:34px;position:relative}
.rd-row::before{content:"";position:absolute;left:calc(46px + 8px + 8px);top:0;bottom:0;width:2px;background:var(--line)}
.rd-row:first-child::before{top:50%}.rd-row:last-child::before{bottom:50%}
.rd-time{font-size:.74rem;font-weight:850;color:var(--ink);text-align:right}
.rd-node{width:14px;height:14px;border-radius:50%;background:var(--acorn);border:2.5px solid var(--surface);box-shadow:0 0 0 1.5px var(--acorn);z-index:1;justify-self:center}
.rd-row.milk .rd-node{width:10px;height:10px;background:var(--berry);box-shadow:0 0 0 1.5px var(--berry)}
.rd-info{display:flex;flex-direction:column;line-height:1.15}
.rd-title{font-size:.78rem;font-weight:800;color:var(--ink)}
.rd-row.milk .rd-title{font-weight:700;color:var(--berry)}
.rd-sub{font-size:.66rem;font-weight:700;color:var(--muted)}
.nextfeed{text-align:center;font-size:.86rem;font-weight:800;color:var(--berry);background:var(--tonal);border-radius:14px;padding:9px;margin:-6px 0 12px}.nextfeed.hidden{display:none}
.feedbar{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:-6px 0 12px}.fb-cell{background:var(--tonal);border-radius:14px;padding:8px 4px;text-align:center;display:flex;flex-direction:column;gap:2px}.fb-cap{font-size:.58rem;font-weight:850;letter-spacing:.04em;color:var(--muted)}.fb-val{font-size:.98rem;font-weight:900;color:var(--moss)}.fb-cell.warn .fb-val{color:var(--acorn)}.fb-cell.danger .fb-val{color:var(--danger)}
.diag-card.hidden{display:none}.diag-grid{display:grid;grid-template-columns:1fr 1fr;gap:6px 14px;font-size:.8rem}.diag-grid .dk{color:var(--muted);font-weight:700}.diag-grid .dv{text-align:right;font-weight:850;color:var(--ink)}.diag-grid .dv.ok{color:var(--moss)}.diag-grid .dv.bad{color:var(--danger)}
.dayband-card{padding:12px 14px;margin:0 0 14px}.dayband-head{display:flex;justify-content:space-between;align-items:center;font-size:.7rem;font-weight:850;letter-spacing:.06em;color:var(--muted);margin-bottom:8px}.dayband-legend{display:inline-flex;align-items:center;gap:5px;font-size:.6rem;font-weight:700;letter-spacing:0}.dayband-legend .db{width:9px;height:9px;border-radius:3px;display:inline-block;margin-left:8px}.db.feed{background:var(--acorn)}.db.milk{background:var(--berry)}.db.diaper{background:var(--moss)}.db.sleep{background:#9a8cc4}
.dayband{position:relative;height:30px;border-radius:9px;overflow:hidden;background:linear-gradient(90deg,#e7edf5 0%,#f4f1e7 25%,#fbf6ec 50%,#f4f1e7 75%,#e7edf5 100%);border:1px solid var(--line)}body.night .dayband{background:linear-gradient(90deg,#1e2740 0%,#2a2f22 25%,#31301f 50%,#2a2f22 75%,#1e2740 100%)}
.dayband .db-night{position:absolute;top:0;bottom:0;background:repeating-linear-gradient(45deg,rgba(90,110,150,.18),rgba(90,110,150,.18) 4px,transparent 4px,transparent 8px)}
.dayband .db-mark{position:absolute;top:5px;width:5px;height:20px;border-radius:3px;transform:translateX(-2.5px);box-shadow:0 1px 2px rgba(0,0,0,.2)}
.dayband .db-mark.feed{background:var(--acorn)}.dayband .db-mark.milk{background:var(--berry)}.dayband .db-mark.diaper{background:var(--moss);top:9px;height:12px}
.dayband .db-sleep{position:absolute;top:0;bottom:0;background:rgba(154,140,196,.4);border-radius:0}
.dayband .db-now{position:absolute;top:0;bottom:0;width:2px;background:var(--ink);opacity:.55}
.dayband-hours{display:flex;justify-content:space-between;font-size:.6rem;color:var(--muted);font-weight:700;margin-top:3px;padding:0 1px}.form-card label{display:block;color:var(--muted);font-weight:850;letter-spacing:.04em;font-size:.75rem;margin:16px 0 6px}.form-card input[type="datetime-local"]{width:100%;padding:13px;border:1px solid var(--line);border-radius:16px;background:var(--surface);color:var(--ink);font:inherit;color-scheme:light}body.night .form-card input[type="datetime-local"]{color-scheme:dark}.time-nudge{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:12px}.time-nudge button{min-height:62px;font-size:.92rem}.nursing{margin-top:11px;padding:13px;border:1px solid #b9d5b0;border-radius:18px;background:#f2f8ef}.nursing h3{margin:0 0 8px;color:var(--ink);font-size:.78rem;letter-spacing:.07em}.nursing .sides{display:grid;grid-template-columns:1fr 1fr;gap:9px}.nursing label{display:flex;align-items:center;justify-content:space-between;gap:8px;color:var(--ink);font-weight:800;font-size:.85rem}.nursing input{width:76px;padding:9px;border:1px solid var(--line);border-radius:12px;background:#fff;color:var(--ink);font:inherit;text-align:center}.form-card input[type="range"]{width:100%;accent-color:var(--acorn);height:36px}.amount{color:var(--ink);font-size:1.32rem;font-weight:850;text-align:center}.range-ends{display:flex;justify-content:space-between;color:var(--muted);font-size:.76rem}.form-actions{display:grid;grid-template-columns:2fr 1fr;gap:10px;margin-top:20px}.milk-toggle{display:flex;align-items:center;gap:10px;margin:17px 0 8px;color:var(--ink);font-weight:800;font-size:.88rem}.milk-toggle input{width:20px;height:20px;accent-color:var(--moss)}.extra-milk{margin-top:11px;padding:13px;border:1px solid #b9d5b0;border-radius:18px;background:#f2f8ef}.extra-milk.hidden{display:none}.extra-milk h3{margin:0 0 8px;color:var(--ink);font-size:.78rem;letter-spacing:.07em}.form-card #bottleToggle{width:100%;margin-top:16px}.milk-kind{display:grid;grid-template-columns:1fr 1fr;gap:9px}.milk-kind button{min-height:44px}.milk-kind button.selected{outline:3px solid #d7e8d0;outline-offset:2px}.notice{margin-top:12px;min-height:20px;text-align:center;color:var(--muted);font-size:.85rem}.notice.error{color:var(--danger);font-weight:750}.notice.ok{color:var(--moss);font-weight:750}body.modal-open{overflow:hidden}.modal{position:fixed;inset:0;z-index:50;display:none;align-items:flex-end;justify-content:center;padding:16px;background:rgba(22,38,27,.45)}.modal.open{display:flex}.modal-backdrop{position:absolute;inset:0}.modal-dialog{position:relative;z-index:1;width:min(100%,620px);max-height:min(88dvh,760px);overflow-y:auto;margin:0;border-radius:28px;box-shadow:0 20px 52px rgba(19,35,24,.34);overscroll-behavior:contain}.modal-dialog.card{margin:0}.modal-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:6px}.modal-head h2{font-size:1.02rem;line-height:1.2;margin:0;font-weight:850}.modal-close{min-height:42px;min-width:42px;padding:8px 11px}@media (min-width:560px){.modal{align-items:center}.modal-dialog{max-height:82dvh}}@media (max-width:350px){.two,.actions{grid-template-columns:1fr}.friends{font-size:1.75rem}.hero h1{font-size:1.05rem}.chart-wrap{gap:2px}}
</style>
</head>
<body>
<main class="app">
  <header class="hero">
    <div><div class="hero-line"><h1>LEŚNY DZIENNIK ALEKSANDRA</h1><p id="clock">Łączenie z urządzeniem…</p></div><span class="ip" id="ip">Panel lokalny</span></div>
  </header>
  <section class="card age" id="age">Oczekiwanie na czas…</section>
  <section class="card development"><p id="developmentTip">Oczekiwanie na prawidłowy czas…</p><p class="development-note">Wskazówka jest orientacyjna — każde dziecko rozwija się we własnym tempie.</p></section>
  <section class="two">
    <article class="card info feeding"><h2>KARMIENIE</h2><p class="latest" id="lastFeeding">Brak zapisanego wpisu</p></article>
    <article class="card info milk"><h2>BUTELKA</h2><p class="latest" id="lastMilk">Brak zapisanego wpisu</p></article>
  </section>
  <section class="since" id="since">OSTATNIE KARMIENIE: …</section>
  <section class="feedbar" id="feedbar">
    <div class="fb-cell"><span class="fb-cap">OSTATNIE</span><span class="fb-val" id="fbLast">—</span></div>
    <div class="fb-cell"><span class="fb-cap">NASTĘPNE (~+4h)</span><span class="fb-val" id="fbNext">—</span></div>
    <div class="fb-cell"><span class="fb-cap">ŚR. PRZERWA</span><span class="fb-val" id="fbAvg">—</span></div>
  </section>
  <section class="card dayband-card"><div class="dayband-head"><span>PRZEBIEG DNIA</span><span class="dayband-legend"><i class="db feed"></i>karm.<i class="db milk"></i>butelka<i class="db diaper"></i>pielucha<i class="db sleep"></i>sen</span></div><div class="dayband" id="dayband"></div><div class="dayband-hours"><span>0</span><span>6</span><span>12</span><span>18</span><span>24</span></div></section>
  <nav class="actions" aria-label="Główne działania">
    <button class="feed wide" data-action="new-feed">KARMIENIE</button>
    <button data-action="other">INNE</button>
    <button class="milk" data-action="weight">WAGA</button>
    <button class="milk" data-action="calendar">KALENDARZ</button>
    <button data-action="chart">PODSUMOWANIE</button>
    <button class="wide milk" data-action="chart5">WYKRESY</button>
  </nav>
  <p class="status" id="status">Sprawdzanie stanu urządzenia…</p>
  <p class="sysinfo" id="sysinfo"></p>
  <div style="display:flex;gap:10px;justify-content:center;align-items:center;margin:-4px 0 14px">
    <a href="/export.csv" style="color:var(--muted);font-size:.8rem;font-weight:800;letter-spacing:.05em;text-decoration:none;padding:8px 12px;border:1px solid var(--line);border-radius:12px;background:var(--surface)">POBIERZ KOPIĘ CSV</a>
    <button type="button" class="small muted" data-action="import">IMPORTUJ DANE</button>
    <button type="button" class="small muted" data-action="send-backup">WYŚLIJ BACKUP</button>
    <button type="button" class="small muted" data-action="toggle-diag">DIAGNOSTYKA</button>
    <input type="file" id="importFile" accept=".csv,text/csv" class="hidden">
  </div>
  <section class="card diag-card hidden" id="diagCard">
    <div class="section-title">DIAGNOSTYKA URZĄDZENIA</div>
    <div class="diag-grid" id="diagGrid"></div>
  </section>

  <div id="calendarModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>LEŚNY KALENDARZ — 3 DNI</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <div id="calendarList"></div>
    </div>
  </div>

  <div id="detailModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2 id="detailTitle">DZIENNIK DNIA</h2><button class="small muted" data-action="back-calendar">KALENDARZ</button></div>
      <div class="detail-list" id="detailList"></div>
      <div class="actions"><button class="feed wide" data-action="day-feed">+ KARMIENIE</button></div>
    </div>
  </div>

  <div id="chartModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>PODSUMOWANIE — DZISIAJ I WCZORAJ</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <div class="actions" style="margin:0 0 10px"><button class="small muted" id="undoBtn" data-action="undo">USUN WPIS</button><button class="small milk" data-action="pumping">+ ODCIAGANIE</button><button class="small" id="vitBtn" data-action="vitamin">+ WIT.D</button></div>
      <div id="summaryList"></div>
    </div>
  </div>

  <div id="chart5Modal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>WYKRES KARMIEN — 5 DNI</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <div class="chart-legend"><span class="dot" style="background:var(--moss)"></span>Mleko matki <span class="dot" style="background:var(--berry)"></span>Mleko modyfikowane <span class="dot" style="background:var(--acorn)"></span>Karmienia</div>
      <div class="chart-wrap" id="milkChart"></div>
      <div style="margin-top:16px;font-size:.85rem;font-weight:700;color:var(--muted)">TABELA</div>
      <div id="extraTable"></div>
      <h3 class="chart-subtitle" style="margin-top:22px">ODSTEPY MIEDZY KARMIENIAMI — DZIS</h3>
      <p class="chart-note" id="gapSummary" style="margin:2px 0 8px"></p>
      <div id="gapChart"></div>
      <h3 class="chart-subtitle" style="margin-top:22px">OSTATNIE 3 DNI — RYTM KARMIEN</h3>
      <div id="rhythm3"></div>
    </div>
  </div>

</main>

  <div id="otherModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>INNE</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <div class="actions"><button class="milk wide" data-action="diaper">PIELUCHA</button><button class="wide" data-action="pumping">ODCIAG POKARMU</button><button class="wide" id="sleepBtn" data-action="sleep">ZASNĄŁ</button></div>
    </div>
  </div>

  <div id="weightModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>WAGA ALEKSANDRA</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <div class="weight-entry">
        <label for="weightG">NOWY POMIAR (GRAMY)</label>
        <div class="weight-row">
          <button type="button" class="small muted" data-action="w-minus">−10</button>
          <input id="weightG" type="number" min="2000" max="15000" step="10" value="3700" inputmode="numeric">
          <button type="button" class="small muted" data-action="w-plus">+10</button>
          <button type="button" class="feed" data-action="w-save">ZAPISZ</button>
        </div>
        <p class="notice" id="weightNotice"></p>
      </div>
      <div class="chart-legend" style="margin-top:14px"><span class="dot" style="background:var(--moss)"></span>Zmierzona <span class="dot" style="background:var(--berry)"></span>Mediana WHO <span class="dot" style="background:#bcd9c2"></span>Oczekiwany zakres (min–max) <span class="dot" style="background:var(--acorn)"></span>Przyrost od wypisu (25–30/15–20 g/d)</div>
      <div id="weightChart"></div>
      <p class="chart-note" id="weightNote"></p>
    </div>
  </div>

  <div id="diaperModal" class="modal" role="presentation" aria-hidden="true">
    <div class="modal-backdrop" data-action="home"></div>
    <div class="card modal-dialog">
      <div class="modal-head"><h2>PIELUCHA</h2><button class="small muted" data-action="home">ZAMKNIJ</button></div>
      <p class="notice">Zapisuje bieżący czas jednym dotknięciem.</p>
      <div class="actions"><button class="milk" data-action="diaper-wet">MOKRA</button><button data-action="diaper-dirty">BRUDNA</button></div>
    </div>
  </div>

  <div id="formModal" class="modal" role="presentation" aria-hidden="true">
  <div class="modal-backdrop" data-action="cancel-form"></div>
  <form class="card form-card modal-dialog" id="entryForm" role="dialog" aria-modal="true" aria-labelledby="formTitle">
    <div class="modal-head"><h2 id="formTitle">NOWE KARMIENIE</h2><button type="button" class="small muted modal-close" data-action="cancel-form" aria-label="Zamknij formularz">ZAMKNIJ</button></div>
    <label for="entryTime" id="timeLabel">CZAS Z LEŚNEGO ZEGARA</label>
    <input id="entryTime" type="datetime-local" required>
    <div class="time-nudge" id="nudgeBox"><button type="button" class="muted" data-action="minus15">−5 MIN</button><button type="button" class="muted" data-action="plus15">+5 MIN</button></div>
    <div class="nursing" id="nursingBox"><h3>PIERŚ — CZAS KARMIENIA (MINUTY)</h3><div class="sides"><label>LEWA<input id="piersL" type="number" min="0" max="90" step="5" value="0"></label><label>PRAWA<input id="piersR" type="number" min="0" max="90" step="5" value="0"></label></div></div>
    <p class="notice" id="quickNotice">Karmienie zapisuje czas. Ilość i rodzaj podajesz tylko po rozwinięciu Butelki.</p>
    <button type="button" class="milk wide" id="bottleToggle">SZCZEGÓŁY</button>
    <div class="extra-milk hidden" id="extraMilkOptions"><h3 id="extraTitle">BUTELKA — ILOŚĆ I RODZAJ</h3><div class="milk-kind" id="kindRow"><button type="button" class="selected" id="milkMother">MATKI</button><button type="button" class="milk" id="milkModified">MODYFIKOWANE</button></div><label for="milkMl" id="bottleLabel">ILOŚĆ W BUTELCE</label><div class="amount" id="milkAmount">30 ml</div><input id="milkMl" type="range" min="10" max="120" value="30"><div class="range-ends"><span>10 ml</span><span>120 ml</span></div></div>
    <div class="form-actions"><button type="submit" class="feed" id="saveButton">ZAPISZ</button><button type="button" class="muted" data-action="cancel-form">ANULUJ</button></div>
    <p class="notice" id="formNotice"></p>
  </form>
</div>
<script>
const state={data:null,view:'home',activeDay:null,milkType:'MLEKO_MATKI'};
const $=id=>document.getElementById(id);
const modals=['calendarModal','detailModal','chartModal','diaperModal','chart5Modal','otherModal','weightModal'];
// WHO masa dla wieku (chlopcy), percentyle 3 / 50 / 97 w kg, miesiace 0..21.
const WHO_P3 =[2.5,3.4,4.4,5.1,5.6,6.1,6.4,6.7,6.9,7.1,7.4,7.6,7.7,7.9,8.1,8.3,8.4,8.6,8.8,8.9,9.1,9.2];
const WHO_BOYS_KG=[3.3,4.5,5.6,6.4,7.0,7.5,7.9,8.3,8.6,8.9,9.2,9.4,9.6,9.9,10.1,10.3,10.5,10.7,10.9,11.1,11.3,11.5];
const WHO_P97=[4.3,5.7,7.0,7.9,8.6,9.2,9.7,10.2,10.5,10.9,11.2,11.5,11.8,12.1,12.4,12.7,12.9,13.2,13.5,13.7,14.0,14.3];
function whoInterpKg(tab,day){const m=day/30.4375;if(m<=0)return tab[0];if(m>=tab.length-1)return tab[tab.length-1];const i=Math.floor(m),f=m-i;return tab[i]+(tab[i+1]-tab[i])*f}
// Zwraca oczekiwany zakres wagi w gramach {min, med, max}, skalowany do wagi urodzeniowej.
function expectedWeightBand(day,birthG){const bw=birthG||3080;const scale=bw/(WHO_BOYS_KG[0]*1000);return {min:Math.round(whoInterpKg(WHO_P3,day)*1000*scale),med:Math.round(whoInterpKg(WHO_BOYS_KG,day)*1000*scale),max:Math.round(whoInterpKg(WHO_P97,day)*1000*scale)}}
function expectedWeightG(day,birthG){return expectedWeightBand(day,birthG).med}
// Zakres przyrostu wagi liczony od wagi WYJSCIOWEJ ze szpitala (2850 g w dniu 2 zycia,
// tj. 10.08.2026 przy urodzeniu 08.08.2026). Tempo: 0-3 mies. 25-30 g/dobe,
// 3-6 mies. 15-20 g/dobe. Zwraca {lo, hi} w gramach dla danego dnia zycia (day).
const DISCHARGE_DAY=2,DISCHARGE_G=2850;                 // dzien zycia i waga w dniu wyjscia
const GAIN_P1_END=Math.round(3*30.4375);                // koniec 0-3 mies. (~dzien 91 od urodzenia)
const GAIN_P2_END=Math.round(6*30.4375);                // koniec 3-6 mies. (~dzien 183)
function dischargeGainBand(day){
  // Kumulujemy przyrost od dnia wyjscia; przed nim brak danych.
  if(day<DISCHARGE_DAY)return null;
  let lo=DISCHARGE_G,hi=DISCHARGE_G;
  for(let d=DISCHARGE_DAY;d<day;d++){
    const inP1=d<GAIN_P1_END;                            // 0-3 mies.
    const inP2=d<GAIN_P2_END;                            // 3-6 mies.
    if(inP1){lo+=25;hi+=30}
    else if(inP2){lo+=15;hi+=20}
    // po 6 mies. nie rysujemy dalej (petla i tak konczy sie na day)
  }
  return {lo,hi}}
function show(view){state.view=view||null;if(view==='home')view=null;modals.forEach(m=>{const el=$(m);const on=m===view;el.classList.toggle('open',on);el.setAttribute('aria-hidden',on?'false':'true')});document.body.classList.toggle('modal-open',!!view);window.scrollTo({top:0,behavior:'smooth'})}
function openFormModal(){$('formModal').classList.add('open');$('formModal').setAttribute('aria-hidden','false');document.body.classList.add('modal-open')}
function closeFormModal(){$('formModal').classList.remove('open');$('formModal').setAttribute('aria-hidden','true');document.body.classList.remove('modal-open');state.pumpMode=false}
function setText(id,value){$(id).textContent=value||''}
function dateLabel(iso){const p=iso.split('-');return p.length===3?`${p[2]}.${p[1]}.${p[0]}`:iso}
function dateTimeInput(value){const d=value?new Date(value):new Date();if(Number.isNaN(d.getTime()))return '';const z=n=>String(n).padStart(2,'0');return `${d.getFullYear()}-${z(d.getMonth()+1)}-${z(d.getDate())}T${z(d.getHours())}:${z(d.getMinutes())}`}
async function request(url,options){const r=await fetch(url,options);let data={};try{data=await r.json()}catch(e){}if(!r.ok)throw new Error(data.message||'Błąd połączenia z urządzeniem');return data}
function clearPanels(){state.activeDay=null;show('home')}
function renderCalendar(days){const list=$('calendarList');list.replaceChildren();days.forEach(day=>{const card=document.createElement('article');card.className='card calendar-day';const top=document.createElement('div');top.className='calendar-top';const name=document.createElement('span');name.className='calendar-date';name.textContent=day.label;const edit=document.createElement('button');edit.className='small milk';edit.textContent='SZCZEGÓŁY';edit.addEventListener('click',()=>openDay(day.date,day.label));top.append(name,edit);const summary=document.createElement('p');summary.className='calendar-summary';summary.textContent=`KARM.: ${day.feedingCount} | MLEKO: ${day.milkMl} ml | PIERS: L${day.piersLeftMin||0}/P${day.piersRightMin||0}\nPIELUCHY: ${day.diaperWet||0}/${day.diaperDirty||0} | ODCIAG.: ${day.pumpingMl||0} ml | WIT.D: ${day.vitaminD?'TAK':'BRAK'}`;card.append(top,summary);list.append(card)})}
function isoDaysAgo(n){const d=new Date();d.setDate(d.getDate()-n);return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`}
function aggLine(es){let f=0,ml=0,pl=0,pr=0,wet=0,dirty=0,pump=0,vit=false;es.forEach(e=>{if(e.type==='KARMIENIE'){f++;pl+=e.piersLeftMin||0;pr+=e.piersRightMin||0}else if((e.type||'').startsWith('MLEKO')){ml+=e.ml||0}else if(e.type==='PIELUCHA_MOKRA'){wet++}else if(e.type==='PIELUCHA_BRUDNA'){dirty++}else if(e.type==='ODCIAGANIE'){pump+=e.ml||0}else if(e.type==='WITAMINA_D'){vit=true}});return `KARM.: ${f} | MLEKO: ${ml} ml | PIERS: L${pl}/P${pr}\nPIELUCHY: ${wet}/${dirty} | ODCIAG.: ${pump} ml | WIT.D: ${vit?'TAK':'BRAK'}`}
function buildEntryRow(en,parent){const row=document.createElement('div');row.className=`entry ${en.type==='KARMIENIE'?'feed':'milk'}`;const left=document.createElement('div');const tm=document.createElement('div');tm.className='entry-time';tm.textContent=en.time;const ty=document.createElement('div');ty.className='entry-type';ty.textContent=en.label||en.type;left.append(tm,ty);if(en.type==='KARMIENIE'&&((en.piersLeftMin||0)+(en.piersRightMin||0))>0){const pm=document.createElement('div');pm.className='entry-type';pm.style.color='var(--muted)';pm.textContent=`L${en.piersLeftMin||0}/P${en.piersRightMin||0} min`;left.append(pm)}const amt=document.createElement('div');amt.className='entry-ml';amt.textContent=en.type==='KARMIENIE'?'':`${en.ml} ml`;row.append(left,amt);const del=document.createElement('button');del.className='delete-btn';del.textContent='X';del.title='Usun ten wpis (wymaga potwierdzenia)';del.addEventListener('click',async e=>{e.stopPropagation();if(!confirm('Usunac ten wpis?\n\n'+en.label+', '+en.time+', '+(en.ml||'0')+' ml'))return;try{await request('/api/delete-entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({line:String(en.lineIndex)})});await refresh();renderSummary()}catch(ex){alert(ex.message)}});row.append(del);parent.append(row)}
async function renderSummary(){const root=$('summaryList');root.replaceChildren();const wait=document.createElement('p');wait.className='empty';wait.textContent='Wczytywanie…';root.append(wait);const cal=state.data&&state.data.calendar?state.data.calendar:[];const days=[cal[0],cal[1]].filter(Boolean);let lists;try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).catch(()=>({entries:[]}))))}catch(e){root.replaceChildren();const err=document.createElement('p');err.className='empty';err.textContent=e.message;root.append(err);return}root.replaceChildren();days.forEach((d,i)=>{const block=document.createElement('article');block.className='card';const t=document.createElement('div');t.className='calendar-date';t.textContent=(i===0?'DZISIAJ ':'WCZORAJ ')+dateLabel(d.date).slice(0,5);if(i===0)t.style.color='var(--moss)';const s=document.createElement('div');s.className='calendar-summary';s.style.margin='4px 0 2px';s.textContent=`KARM.: ${d.feedingCount} | MLEKO: ${d.milkMl} ml | PIERS: L${d.piersLeftMin||0}/P${d.piersRightMin||0}\nPIELUCHY: ${d.diaperWet||0}/${d.diaperDirty||0} | ODCIAG.: ${d.pumpingMl||0} ml | WIT.D: ${d.vitaminD?'TAK':'BRAK'}`;block.append(t,s);const es=(lists[i]&&lists[i].entries)||[];if(!es.length){const em=document.createElement('p');em.className='empty';em.textContent='Brak wpisow';block.append(em)}es.forEach(en=>buildEntryRow(en,block));root.append(block)});for(let k=2;k<2+(state.summaryExtra||0);k++){const ds=isoDaysAgo(k);try{const data=await request(`/api/entries?date=${encodeURIComponent(ds)}`);const es=data.entries||[];const block=document.createElement('article');block.className='card';const t=document.createElement('div');t.className='calendar-date';t.textContent=dateLabel(ds).slice(0,5);const s=document.createElement('div');s.className='calendar-summary';s.style.margin='4px 0 2px';s.textContent=aggLine(es);block.append(t,s);if(!es.length){const em=document.createElement('p');em.className='empty';em.textContent='Brak wpisow';block.append(em)}es.forEach(en=>buildEntryRow(en,block));root.append(block)}catch(e){}}const moreBtn=document.createElement('button');moreBtn.className='muted';moreBtn.style.width='100%';moreBtn.style.marginTop='4px';moreBtn.textContent='+ WCZYTAJ STARSZE DNI';moreBtn.addEventListener('click',()=>{state.summaryExtra=(state.summaryExtra||0)+7;renderSummary()});root.append(moreBtn);const t0=cal[0];if(t0)$('vitBtn').textContent=t0.vitaminD?'WIT.D OK':'+ WIT.D'}
function compactHomeEntry(value,bottle){if(!value||value.startsWith('Brak'))return 'Brak wpisu';const parts=value.split('\n'),time=(parts[0].split('  ').pop()||parts[0]),detail=(parts[1]||'').replace(/MLEKO |Mleko | \| /g,'');return bottle?`${time}\n${detail.slice(0,16)}`:`${time}\nZapisano`}
function render(data){state.data=data;setText('clock',data.now||'Brak potwierdzonego czasu');setText('ip',data.ip?`Panel WWW: http://${data.ip}`:'Panel WWW: oczekiwanie na Wi‑Fi');setText('age',data.age);setText('developmentTip',data.developmentTip||'Oczekiwanie na wskazówkę rozwojową…');let ft=compactHomeEntry(data.lastFeeding,false);const fcard=$('lastFeeding');if(data.lastFeedingAgo){ft=ft.replace('\nZapisano','\n'+data.lastFeedingAgo);fcard.style.color=(data.lastFeedingAgeMin>=240)?'var(--danger)':(data.lastFeedingAgeMin>=180?'#D98B5F':'')}else{fcard.style.color=''}setText('lastFeeding',ft);setText('lastMilk',compactHomeEntry(data.lastMilk,true));const sc=$('since');if(data.lastFeedingAgo){sc.textContent='OSTATNIE KARMIENIE: '+data.lastFeedingAgo;sc.style.color=(data.lastFeedingAgeMin>=240)?'var(--danger)':(data.lastFeedingAgeMin>=180?'#D98B5F':'var(--moss)')}else{sc.textContent='OSTATNIE KARMIENIE: brak wpisu';sc.style.color='var(--muted)'}document.body.classList.toggle('night',!!data.night);const dot=ok=>`<span class="dot ${ok?'ok':'err'}"></span>`;$('status').innerHTML=`${dot(data.wifi)}Wi‑Fi ${data.wifi?'połączono':'rozłączono'} &nbsp; ${dot(data.storage)}Pamięć ${data.storage?'gotowa':'BŁĄD'} &nbsp; ${dot(data.timeValid)}Czas ${data.timeValid?'gotowy':'oczekiwanie'}`;$('status').classList.toggle('error',!data.storage||!data.wifi);renderCalendar(data.calendar);const heapPct=data.totalHeap?Math.round(data.freeHeap/data.totalHeap*100):0;const psramPct=data.totalPsram?Math.round(data.freePsram/data.totalPsram*100):0;const up=data.uptimeSec||0;const d=Math.floor(up/86400);const h=Math.floor((up%86400)/3600);setText('sysinfo',`RAM ${data.freeHeap} KB (${heapPct}%)  PSRAM ${data.freePsram} KB (${psramPct}%)  CPU ${data.cpuLoad||0}%  Praca ${d}d ${h}h`);renderNextFeed(data);renderDiag(data);renderDayBand()}
function fmtGapMin(m){if(!m||m<=0)return '—';const h=Math.floor(m/60),mm=m%60;return h>0?`${h}h ${mm}min`:`${mm}min`}
function renderNextFeed(data){
  // Pasek karmien: OSTATNIE (temu) | NASTEPNE (~+4h) | SR. PRZERWA — jak na wygaszaczu.
  const last=data.lastFeedingAgo||'brak';setText('fbLast',last);
  const next=data.nextFeedingIso?('~'+data.nextFeedingIso.slice(11,16)):'—';setText('fbNext',next);
  setText('fbAvg',fmtGapMin(data.avgFeedingGapMin));
  // Kolor komorki "OSTATNIE" wg wieku karmienia (zielony/pomaranczowy/czerwony).
  const cell=$('fbLast').closest('.fb-cell');if(cell){cell.classList.remove('warn','danger');const age=data.lastFeedingAgeMin;if(age>=240)cell.classList.add('danger');else if(age>=180)cell.classList.add('warn')}
}
function renderDiag(data){const g=$('diagGrid');if(!g||$('diagCard').classList.contains('hidden'))return;const row=(k,v,cls)=>`<div class="dk">${k}</div><div class="dv ${cls||''}">${v}</div>`;const up=data.uptimeSec||0;const d=Math.floor(up/86400),h=Math.floor((up%86400)/3600),m=Math.floor((up%3600)/60);let html='';
  html+=row('Wi-Fi',data.wifi?'połączono':'ROZŁĄCZONO',data.wifi?'ok':'bad');
  if(data.wifi){html+=row('IP',data.ip||'—');html+=row('Sygnał',(data.rssi||0)+' dBm')}
  html+=row('Serwer HTTP',data.storage!==undefined?(data.wifi?'aktywny':'—'):'—',data.wifi?'ok':'');
  html+=row('Obsłużonych żądań',data.httpRequests!=null?data.httpRequests:'—');
  html+=row('Czas (NTP)',data.timeValid?'OK':'brak',data.timeValid?'ok':'bad');
  html+=row('Pamięć danych',data.storage?'OK':'BŁĄD',data.storage?'ok':'bad');
  html+=row('Pogoda',data.now&&data.freeHeap!=null?'—':'—');
  html+=row('RAM wolny',(data.freeHeap||0)+' KB');
  html+=row('RAM min',(data.minFreeHeap!=null?data.minFreeHeap:'?')+' KB');
  html+=row('PSRAM wolny',(data.freePsram||0)+' KB');
  html+=row('CPU',(data.cpuLoad||0)+' %');
  html+=row('Praca',`${d}d ${h}h ${m}min`);
  html+=row('Watchdog',data.watchdogReady?'aktywny':'wyłączony',data.watchdogReady?'ok':'bad');
  html+=row('Uruchomień',data.bootCount!=null?data.bootCount:'—');
  html+=row('Restartów watchdoga',data.watchdogResets!=null?data.watchdogResets:'—',(data.watchdogResets>0)?'bad':'');
  html+=row('Ostatni reset',data.resetReason||'—');
  g.innerHTML=html;
}
async function renderDayBand(){const band=$('dayband');if(!band)return;const iso=isoDaysAgo(0);let es=[];try{const d=await request(`/api/entries?date=${encodeURIComponent(iso)}`);es=d.entries||[]}catch(e){return}band.replaceChildren();
// Pas nocy 0-7 i 21-24
[[0,7],[21,24]].forEach(([a,b])=>{const n=document.createElement('div');n.className='db-night';n.style.left=(a/24*100)+'%';n.style.width=((b-a)/24*100)+'%';band.appendChild(n)});
const toMin=t=>{const p=(t||'').split(':');return p.length<2?null:(+p[0])*60+(+p[1])};
// Pasma snu: pary SEN_START -> SEN_STOP (sen trwajacy do teraz konczy sie znacznikiem czasu biezacego)
const nowM=new Date().getHours()*60+new Date().getMinutes();let sStart=null;
es.forEach(e=>{if(e.type==='SEN_START'){sStart=toMin(e.time)}else if(e.type==='SEN_STOP'&&sStart!=null){const m2=toMin(e.time);if(m2!=null){const sp=document.createElement('div');sp.className='db-sleep';sp.style.left=(sStart/1440*100)+'%';sp.style.width=(Math.max(m2-sStart,4)/1440*100)+'%';sp.title='Sen '+Math.floor((m2-sStart)/60)+'h '+((m2-sStart)%60)+'min';band.appendChild(sp)}sStart=null}});
if(sStart!=null){const sp=document.createElement('div');sp.className='db-sleep';sp.style.left=(sStart/1440*100)+'%';sp.style.width=(Math.max(nowM-sStart,4)/1440*100)+'%';sp.title='Sen w toku';band.appendChild(sp)}
// Markery zdarzen
es.forEach(e=>{const mins=toMin(e.time);if(mins==null)return;let cls=null;if(e.type==='KARMIENIE')cls='feed';else if((e.type||'').startsWith('MLEKO'))cls='milk';else if(e.type==='PIELUCHA_MOKRA'||e.type==='PIELUCHA_BRUDNA')cls='diaper';if(!cls)return;const m=document.createElement('div');m.className='db-mark '+cls;m.style.left=(mins/1440*100)+'%';m.title=e.time+' '+(e.label||e.type);band.appendChild(m)});
// Znacznik teraz
const now=new Date();const nm=now.getHours()*60+now.getMinutes();const nw=document.createElement('div');nw.className='db-now';nw.style.left=(nm/1440*100)+'%';band.appendChild(nw)}
function renderChart(cal){const MAX=Math.max(...cal.map(d=>d.milkMl),1);const milkChart=$('milkChart');milkChart.replaceChildren();cal.forEach(d=>{const u=document.createElement('div');u.className='bar-unit';const g=document.createElement('div');g.className='bar-pair';const mh=d.motherMilkMl/MAX*100;const modh=d.modifiedMilkMl/MAX*100;if(mh>0){const b=document.createElement('div');b.className='bar mother';b.style.height=mh+'%';b.title=d.motherMilkMl+' ml matki';g.append(b)}if(modh>0){const b=document.createElement('div');b.className='bar modified';b.style.height=modh+'%';b.title=d.modifiedMilkMl+' ml modyf.';g.append(b)}const v=document.createElement('div');v.className='bar-value';v.textContent=d.milkMl||'0';v.style.fontSize='.58rem';u.append(v,g);const feed=document.createElement('div');feed.style.fontSize='.65rem';feed.style.fontWeight='800';feed.style.color='var(--acorn)';feed.textContent='●'+d.feedingCount;u.append(feed);milkChart.append(u)});const extra=$('extraTable');let html='<table class="chart-table"><tr><th>Dzien</th><th>Karmienia</th><th>Mleko matki</th><th>Mleko mod.</th><th>Suma mleka</th></tr>';cal.forEach(d=>{const label=d.label.split(' - ')[0];const suma=(d.motherMilkMl||0)+(d.modifiedMilkMl||0);html+=`<tr><td>${label}</td><td>${d.feedingCount}</td><td>${d.motherMilkMl} ml</td><td>${d.modifiedMilkMl} ml</td><td class="ok">${suma} ml</td></tr>`});html+='</table>';extra.innerHTML=html}
async function refresh(){try{render(await request('/api/status'))}catch(error){setText('status',error.message);$('status').classList.add('error')}}
async function openDay(date,label){state.activeDay=date;state.detailLabel=label;setText('detailTitle',`DZIENNIK DNIA — ${label||dateLabel(date)}`);const list=$('detailList');list.replaceChildren();const empty=document.createElement('p');empty.className='empty';empty.textContent='Wczytywanie wpisów…';list.append(empty);show('detailModal');try{const data=await request(`/api/entries?date=${encodeURIComponent(date)}`);list.replaceChildren();if(!data.entries.length){const info=document.createElement('p');info.className='empty';info.textContent='Brak wpisów dla tego dnia';list.append(info);return}data.entries.forEach(entry=>{const row=document.createElement('div');row.className=`entry ${entry.type==='KARMIENIE'?'feed':'milk'}`;const left=document.createElement('div');const time=document.createElement('div');time.className='entry-time';time.textContent=entry.time;const type=document.createElement('div');type.className='entry-type';type.textContent=entry.label||entry.type;left.append(time,type);if(entry.type==='KARMIENIE'&&((entry.piersLeftMin||0)+(entry.piersRightMin||0))>0){const p=document.createElement('div');p.className='entry-type';p.style.color='var(--muted)';p.textContent=`PIERŚ: L${entry.piersLeftMin||0} P${entry.piersRightMin||0} min`;left.append(p)};const amount=document.createElement('div');amount.className='entry-ml';amount.textContent=entry.type==='KARMIENIE'&&Number(entry.ml)===0?'':`${entry.ml} ml`;row.append(left,amount);const del=document.createElement('button');del.className='delete-btn';del.textContent='X';del.addEventListener('click',async e=>{e.stopPropagation();if(!confirm('Usunac ten wpis?\n\n'+entry.label+', '+entry.time+', '+(entry.ml||'0')+' ml'))return;try{await request('/api/delete-entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({line:String(entry.lineIndex)})});refresh();openDay(state.activeDay,state.detailLabel)}catch(ex){alert(ex.message)}});row.append(del);list.append(row)})}catch(error){setText('detailList',error.message)}}
async function postEvent(type,ml=0){try{await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type:type,ml:String(ml)})});await refresh();return true}catch(e){setText('status',e.message);$('status').classList.add('error');return false}}
function doDeleteEntry(lineIndex){}
function openPumping(){state.pumpMode=true;setText('formTitle','ODCIAGANIE MLEKA');['timeLabel','entryTime','nudgeBox','quickNotice','bottleToggle','nursingBox'].forEach(id=>$(id).classList.add('hidden'));$('extraMilkOptions').classList.remove('hidden');setText('extraTitle','ODCIAGANIE — ILOŚĆ ML');$('kindRow').classList.add('hidden');$('bottleLabel').classList.add('hidden');const d=state.data||{};$('milkMl').value=d.defaultMl||30;$('milkMl').min=d.minMl||10;$('milkMl').max=d.maxMl||120;$('entryTime').value=dateTimeInput(state.data&&state.data.nowIso);setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal()}
function setMilkType(type){state.milkType=type;$('milkMother').classList.toggle('selected',type==='MLEKO_MATKI');$('milkModified').classList.toggle('selected',type==='MLEKO_MODYFIKOWANE')}
function updateBottle(){const open=state.bottleOpen;$('extraMilkOptions').classList.toggle('hidden',!open);setText('bottleToggle',open?'SZCZEGÓŁY — UKRYJ':'SZCZEGÓŁY')}
function openForm(date){state.pumpMode=false;['timeLabel','entryTime','nudgeBox','quickNotice','bottleToggle','nursingBox'].forEach(id=>$(id).classList.remove('hidden'));setText('extraTitle','BUTELKA — ILOŚĆ I RODZAJ');$('kindRow').classList.remove('hidden');$('bottleLabel').classList.remove('hidden');setText('formTitle',date?`DODAJ KARMIENIE — ${dateLabel(date)}`:'NOWE KARMIENIE');$('entryTime').value=date?`${date}T12:00`:dateTimeInput(state.data&&state.data.nowIso);const d=state.data||{};$('milkMl').value=d.defaultMl||30;$('milkMl').min=d.minMl||10;$('milkMl').max=d.maxMl||120;$('piersL').value=0;$('piersR').value=0;state.bottleOpen=false;setMilkType('MLEKO_MATKI');updateBottle();setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal()}
function nudge(minutes){const input=$('entryTime');const date=new Date(input.value);if(Number.isNaN(date.getTime()))return;date.setMinutes(date.getMinutes()+minutes);input.value=dateTimeInput(date)}
$('milkMl').addEventListener('input',()=>setText('milkAmount',`${$('milkMl').value} ml`));
$('bottleToggle').addEventListener('click',()=>{state.bottleOpen=!state.bottleOpen;updateBottle()});$('milkMother').addEventListener('click',()=>setMilkType('MLEKO_MATKI'));$('milkModified').addEventListener('click',()=>setMilkType('MLEKO_MODYFIKOWANE'));
$('entryForm').addEventListener('submit',async event=>{event.preventDefault();const notice=$('formNotice');notice.className='notice';setText('formNotice','Zapisywanie…');try{let body;if(state.pumpMode){body=new URLSearchParams({type:'ODCIAGANIE',when:$('entryTime').value,ml:$('milkMl').value})}else{const extra=state.bottleOpen;body=new URLSearchParams({type:'KARMIENIE',when:$('entryTime').value,ml:'0',extraMilk:extra?'1':'0',lewaMin:Number($('piersL').value)||0,prawaMin:Number($('piersR').value)||0});if(extra){body.set('milkType',state.milkType);body.set('milkMl',$('milkMl').value)}}const result=await request('/api/entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});notice.className='notice ok';setText('formNotice',result.message||'Wpis zapisany.');await refresh();setTimeout(()=>{const returnDay=state.activeDay;closeFormModal();if(returnDay&&!state.pumpMode)openDay(returnDay);else clearPanels()},550)}catch(error){notice.className='notice error';setText('formNotice',error.message)}});
function openWeight(){const d=state.data||{};const w=$('weightG');w.value=(d.lastWeightG&&d.lastWeightG>0)?d.lastWeightG:3700;setText('weightNotice','');show('weightModal');renderWeightChart()}
async function saveWeight(){const grams=Number($('weightG').value)||0;const notice=$('weightNotice');notice.className='notice';if(grams<2000||grams>15000){notice.className='notice error';setText('weightNotice','Podaj wage 2000-15000 g.');return}setText('weightNotice','Zapisywanie…');try{const r=await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type:'WAGA',ml:String(grams)})});notice.className='notice ok';setText('weightNotice',r.message||'Zapisano wage.');await refresh();renderWeightChart()}catch(e){notice.className='notice error';setText('weightNotice',e.message)}}
async function renderWeightChart(){const host=$('weightChart');host.replaceChildren();const birthG=(state.data&&state.data.birthWeightG)||3080;let pts=[];try{const data=await request('/api/weight-series');pts=(data.points||[]).filter(p=>p&&p.g>0).sort((a,b)=>a.day-b.day)}catch(e){}const today=(state.data&&state.data.developmentDay!=null&&state.data.developmentDay>=0)?state.data.developmentDay:(pts.length?pts[pts.length-1].day:60);const maxDay=Math.max(today,pts.length?pts[pts.length-1].day:0,30);const W=440,H=240,padL=44,padR=12,padT=12,padB=26;const plotW=W-padL-padR,plotH=H-padT-padB;const step=Math.max(1,Math.round(maxDay/60));
// Punkty pasma (min/med/max) wzdluz osi dni
const bandPts=[];for(let d=0;d<=maxDay;d+=step)bandPts.push(Object.assign({d},expectedWeightBand(d,birthG)));if(bandPts[bandPts.length-1].d!==maxDay)bandPts.push(Object.assign({d:maxDay},expectedWeightBand(maxDay,birthG)));
// Punkty pasma przyrostu od wagi wyjsciowej ze szpitala (rysowane tylko w zakresie 0-6 mies.)
const gainPts=[];{const gEnd=Math.min(maxDay,GAIN_P2_END);for(let d=DISCHARGE_DAY;d<=gEnd;d+=step){const b=dischargeGainBand(d);if(b)gainPts.push({d,lo:b.lo,hi:b.hi})}if(gainPts.length&&gainPts[gainPts.length-1].d!==gEnd){const b=dischargeGainBand(gEnd);if(b)gainPts.push({d:gEnd,lo:b.lo,hi:b.hi})}}
let maxG=0,minBand=1e9;bandPts.forEach(b=>{if(b.max>maxG)maxG=b.max;if(b.min<minBand)minBand=b.min});pts.forEach(p=>{if(p.g>maxG)maxG=p.g;if(p.g<minBand)minBand=p.g});gainPts.forEach(b=>{if(b.hi>maxG)maxG=b.hi;if(b.lo<minBand)minBand=b.lo});maxG=Math.ceil((maxG+300)/500)*500;const minG=Math.max(0,Math.floor((minBand-300)/500)*500);
const x=d=>padL+(maxDay<=0?0:d/maxDay*plotW);const y=g=>padT+plotH-(g-minG)/(maxG-minG)*plotH;const NS='http://www.w3.org/2000/svg';const svg=document.createElementNS(NS,'svg');svg.setAttribute('viewBox',`0 0 ${W} ${H}`);svg.setAttribute('class','weight-svg');const mk=(n,a)=>{const e=document.createElementNS(NS,n);for(const k in a)e.setAttribute(k,a[k]);return e};
// Siatka pozioma + os wag
for(let i=0;i<=4;i++){const gv=minG+(maxG-minG)*i/4;const yy=y(gv);svg.appendChild(mk('line',{class:'grid',x1:padL,y1:yy,x2:W-padR,y2:yy}));const t=mk('text',{class:'axis-txt',x:4,y:yy+3});t.textContent=(gv/1000).toFixed(1)+'kg';svg.appendChild(t)}
for(let i=0;i<=4;i++){const dv=Math.round(maxDay*i/4);const xx=x(dv);const t=mk('text',{class:'axis-txt',x:xx-8,y:H-8});t.textContent='d'+dv;svg.appendChild(t)}
// Pasmo przyrostu od wagi wyjsciowej (2850 g): 25-30 g/d (0-3 mies.), 15-20 g/d (3-6 mies.).
// Rysowane pod pasmem WHO, aby go nie zaslaniac; wypelnienie + przerywane granice.
if(gainPts.length>1){let gBand='';gainPts.forEach((b,i)=>{gBand+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.hi).toFixed(1)+' '});for(let i=gainPts.length-1;i>=0;i--){gBand+='L'+x(gainPts[i].d).toFixed(1)+' '+y(gainPts[i].lo).toFixed(1)+' '}gBand+='Z';svg.appendChild(mk('path',{class:'gain-band',d:gBand.trim()}));let gLo='',gHi='';gainPts.forEach((b,i)=>{gLo+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.lo).toFixed(1)+' ';gHi+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.hi).toFixed(1)+' '});svg.appendChild(mk('path',{class:'gain-edge',d:gLo.trim()}));svg.appendChild(mk('path',{class:'gain-edge',d:gHi.trim()}))}
// Pasmo oczekiwane (min..max) jako wypelniony obszar
let bandPath='';bandPts.forEach((b,i)=>{bandPath+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.max).toFixed(1)+' '});for(let i=bandPts.length-1;i>=0;i--){bandPath+='L'+x(bandPts[i].d).toFixed(1)+' '+y(bandPts[i].min).toFixed(1)+' '}bandPath+='Z';svg.appendChild(mk('path',{class:'exp-band',d:bandPath.trim()}));
// Linie granic min i max
let minPath='',maxPath='';bandPts.forEach((b,i)=>{minPath+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.min).toFixed(1)+' ';maxPath+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.max).toFixed(1)+' '});svg.appendChild(mk('path',{class:'exp-edge',d:minPath.trim()}));svg.appendChild(mk('path',{class:'exp-edge',d:maxPath.trim()}));
// Linia mediany (przerywana)
let medPath='';bandPts.forEach((b,i)=>{medPath+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.med).toFixed(1)+' '});svg.appendChild(mk('path',{class:'exp-line',d:medPath.trim()}));
// Rzeczywiste pomiary
if(pts.length){let actPath='';pts.forEach((p,i)=>{const px=x(p.day),py=y(p.g);actPath+=(i?'L':'M')+px.toFixed(1)+' '+py.toFixed(1)+' '});svg.appendChild(mk('path',{class:'act-line',d:actPath.trim()}));pts.forEach(p=>{const c=mk('circle',{class:'act-dot',cx:x(p.day),cy:y(p.g),r:3.5});const ti=mk('title',{});ti.textContent=`dzien ${p.day}: ${p.g} g`;c.appendChild(ti);svg.appendChild(c)})}
host.appendChild(svg);
if(pts.length){const last=pts[pts.length-1];const b=expectedWeightBand(last.day,birthG);const diff=last.g-b.med;let stan;if(last.g<b.min)stan='ponizej normy';else if(last.g>b.max)stan='powyzej normy';else stan='w normie';setText('weightNote',`Ostatni pomiar: ${last.g} g (dzien ${last.day}). Oczekiwany zakres WHO: ${b.min}–${b.max} g (mediana ${b.med} g, ${diff>=0?'+':''}${diff} g). Status: ${stan}.`)}else{setText('weightNote','Brak pomiarow wagi. Dodaj pierwszy pomiar powyzej — pojawi sie na tle oczekiwanego zakresu (zielone pasmo min–max) i mediany.')}}
async function renderAnalysis(){const gapHost=$('gapChart'),rhythmHost=$('rhythm3');gapHost.replaceChildren();rhythmHost.replaceChildren();const cal=(state.data&&state.data.calendar)||[];const days=cal.slice(0,3);let lists=[];try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).then(r=>r.entries||[]).catch(()=>[])))}catch(e){lists=days.map(()=>[])}
// Odstepy miedzy karmieniami DZIS (indeks 0)
const todayFeeds=(lists[0]||[]).filter(e=>e.type==='KARMIENIE').map(e=>e.time).sort();
if(todayFeeds.length<2){const p=document.createElement('p');p.className='empty';p.textContent=todayFeeds.length?'Tylko jedno karmienie dzis — brak odstepu do pokazania.':'Brak karmien dzis.';gapHost.appendChild(p);setText('gapSummary','')}else{const toMin=t=>{const p=t.split(':');return (+p[0])*60+(+p[1])};let gaps=[],maxGap=0;for(let i=1;i<todayFeeds.length;i++){const g=toMin(todayFeeds[i])-toMin(todayFeeds[i-1]);gaps.push({from:todayFeeds[i-1],to:todayFeeds[i],min:g});if(g>maxGap)maxGap=g}const avg=Math.round(gaps.reduce((a,b)=>a+b.min,0)/gaps.length);setText('gapSummary',`Karmien: ${todayFeeds.length} | sredni odstep: ${Math.floor(avg/60)}h ${avg%60}min`);gaps.forEach(g=>{const row=document.createElement('div');row.className='gap-row';const lab=document.createElement('div');lab.className='gap-time';lab.textContent=g.from+'→'+g.to;const track=document.createElement('div');track.className='gap-bar-track';const fill=document.createElement('div');fill.className='gap-bar-fill';fill.style.width=(maxGap?Math.max(6,g.min/maxGap*100):6)+'%';track.appendChild(fill);const val=document.createElement('div');val.className='gap-val';val.textContent=Math.floor(g.min/60)+'h '+(g.min%60)+'m';row.append(lab,track,val);gapHost.appendChild(row)})}
// Rytm 3 ostatnich dni: pionowa os czasu z odstepami. Liczymy TYLKO KARMIENIE (piers),
// butelki pokazujemy osobno i nie wliczamy do liczby karmien.
const fmtGap=m=>{const h=Math.floor(m/60),mm=m%60;return h>0?`${h}h ${mm}min`:`${mm}min`};
days.forEach((d,i)=>{
  const all=(lists[i]||[]).map(e=>{const isFeed=e.type==='KARMIENIE',isMilk=(e.type||'').startsWith('MLEKO');if(!isFeed&&!isMilk)return null;const p=e.time.split(':');return {mins:(+p[0])*60+(+p[1]),milk:isMilk,time:e.time,label:e.label||e.type,ml:e.ml||0}}).filter(Boolean).sort((a,b)=>a.mins-b.mins);
  const feeds=all.filter(f=>!f.milk); // tylko karmienia piersia
  const gaps=[];for(let j=1;j<feeds.length;j++)gaps.push(feeds[j].mins-feeds[j-1].mins);
  const avg=gaps.length?Math.round(gaps.reduce((a,b)=>a+b,0)/gaps.length):0;
  const wrap=document.createElement('article');wrap.className='rd-card';
  const head=document.createElement('div');head.className='rd-head';
  const name=document.createElement('div');name.className='rd-name';name.textContent=(i===0?'DZIS':i===1?'WCZORAJ':'2 DNI TEMU')+' · '+dateLabel(d.date).slice(0,5);
  const stat=document.createElement('div');stat.className='rd-stat';stat.innerHTML=`<b>${feeds.length}</b> karmien`+(gaps.length?` · sr. co <b>${fmtGap(avg)}</b>`:'');
  head.append(name,stat);wrap.appendChild(head);
  if(!all.length){const em=document.createElement('div');em.className='rd-empty';em.textContent='Brak karmien tego dnia';wrap.appendChild(em);rhythmHost.appendChild(wrap);return}
  const tl=document.createElement('div');tl.className='rd-timeline';
  let prevFeed=null;
  all.forEach(f=>{
    const row=document.createElement('div');row.className='rd-row'+(f.milk?' milk':'');
    const time=document.createElement('div');time.className='rd-time';time.textContent=f.time;
    const node=document.createElement('div');node.className='rd-node';
    const info=document.createElement('div');info.className='rd-info';
    const title=document.createElement('div');title.className='rd-title';title.textContent=f.milk?(f.label+(f.ml?` · ${f.ml} ml`:'')):'Karmienie';
    info.appendChild(title);
    if(!f.milk){const g=(prevFeed!=null)?('po '+fmtGap(f.mins-prevFeed)):'pierwsze';const sub=document.createElement('div');sub.className='rd-sub';sub.textContent=g;info.appendChild(sub);prevFeed=f.mins}
    else{const sub=document.createElement('div');sub.className='rd-sub';sub.textContent='butelka';info.appendChild(sub)}
    row.append(time,node,info);tl.appendChild(row)
  });
  wrap.appendChild(tl);rhythmHost.appendChild(wrap)
})}
document.addEventListener('click',async event=>{const action=event.target.closest('[data-action]')?.dataset.action;if(!action)return;if(action==='new-feed')openForm();if(action==='quick-feed'){if(await postEvent('KARMIENIE'))clearPanels()}if(action==='quick-feed-close'){if(await postEvent('KARMIENIE')){closeFormModal();clearPanels()}}if(action==='diaper')show('diaperModal');if(action==='import')$('importFile').click();if(action==='toggle-diag'){const c=$('diagCard');c.classList.toggle('hidden');if(!c.classList.contains('hidden')&&state.data)renderDiag(state.data)}if(action==='send-backup'){try{const r=await request('/api/send-backup',{method:'POST'});setText('status',r.message||'Backup wysylany.');$('status').classList.remove('error')}catch(e){setText('status',e.message);$('status').classList.add('error')}}if(action==='diaper-wet'){if(await postEvent('PIELUCHA_MOKRA'))clearPanels()}if(action==='diaper-dirty'){if(await postEvent('PIELUCHA_BRUDNA'))clearPanels()}if(action==='pumping')openPumping();if(action==='vitamin')await postEvent('WITAMINA_D');if(action==='undo'){renderSummary()}if(action==='calendar')show('calendarModal');if(action==='chart'){show('chartModal');state.summaryExtra=0;renderSummary()}if(action==='chart5'){show('chart5Modal');if(state.data&&state.data.calendar)renderChart(state.data.calendar);renderAnalysis()}if(action==='other'){const sb=$('sleepBtn');if(sb)sb.textContent=(state.data&&state.data.sleepInProgress)?'OBUDZIŁ SIĘ':'ZASNĄŁ';show('otherModal')}if(action==='sleep'){const t=(state.data&&state.data.sleepInProgress)?'SEN_STOP':'SEN_START';if(await postEvent(t))clearPanels()}if(action==='weight'){openWeight()}if(action==='w-minus'){const w=$('weightG');w.value=Math.max(2000,(Number(w.value)||3700)-10)}if(action==='w-plus'){const w=$('weightG');w.value=Math.min(15000,(Number(w.value)||3700)+10)}if(action==='w-save'){saveWeight()}if(action==='home')clearPanels();if(action==='back-calendar')show('calendarModal');if(action==='day-feed'&&state.activeDay)openForm(state.activeDay);if(action==='cancel-form'){setText('formNotice','');const returnDay=state.activeDay;closeFormModal();returnDay?openDay(returnDay):clearPanels()}if(action==='minus15')nudge(-5);if(action==='plus15')nudge(5)});
document.addEventListener('keydown',event=>{if(event.key!=='Escape')return;if($('formModal').classList.contains('open')){setText('formNotice','');const returnDay=state.activeDay;closeFormModal();returnDay?openDay(returnDay):clearPanels()}else if(modals.some(m=>$(m).classList.contains('open'))){clearPanels()}});
$('importFile').addEventListener('change',async event=>{const f=event.target.files&&event.target.files[0];event.target.value='';if(!f)return;if(!confirm('Import ZASTAPI wszystkie dane na urzadzeniu zawartoscia pliku.\nObecne dane zostana najpierw zapisane jako kopia bezpieczenstwa.\nKontynuowac?'))return;try{const text=await f.text();if(text.length>512*1024){alert('Plik jest za duzy (limit 512 KB).');return}const r=await request('/api/import',{method:'POST',headers:{'Content-Type':'text/csv'},body:text});await refresh();alert(r.message||'Import zakonczony.')}catch(e){setText('status',e.message);$('status').classList.add('error');alert(e.message)}});
refresh();setInterval(refresh,10000);
</script>
</body>
</html>
)WEBPANEL";
