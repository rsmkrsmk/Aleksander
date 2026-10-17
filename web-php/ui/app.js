/* ============================================================================
   Leśny Dziennik Aleksandra — logika panelu WWW (redesign)
   Konsumuje ten sam kontrakt API co poprzednio (bez zmian w backendzie).
   ============================================================================ */
'use strict';

const state = { data:null, page:'start', view:null, activeDay:null, detailLabel:null,
  milkMother:true, milkModified:false, bottleOpen:false, pumpMode:false, summaryExtra:0,
  statView:'summary', histPeriod:'week',
  editFeedLine:null, editMilkHad:false,
  // Osobne ilosci mleka (mieszane = oba rodzaje > 0 => dwa wiersze). milkMl (suwak)
  // uzywany gdy tylko JEDEN rodzaj; przy obu rodzajach uzywamy tych dwoch pol.
  milkMotherMl:60, milkModifiedMl:60, mlPopKind:null };
const $ = id => document.getElementById(id);
const MODALS = ['formModal','bathModal','memoryModal'];
const PAGES = ['start','diary','stats','weight','sleep'];
const PAGE_TITLES = {start:'Leśny Dziennik',diary:'Dziennik',stats:'Statystyki',weight:'Waga',sleep:'Sen'};

/* ---------- WHO / przyrost (jak w oryginale) ---------- */
const WHO_P3=[2.5,3.4,4.4,5.1,5.6,6.1,6.4,6.7,6.9,7.1,7.4,7.6,7.7,7.9,8.1,8.3,8.4,8.6,8.8,8.9,9.1,9.2];
const WHO_MED=[3.3,4.5,5.6,6.4,7.0,7.5,7.9,8.3,8.6,8.9,9.2,9.4,9.6,9.9,10.1,10.3,10.5,10.7,10.9,11.1,11.3,11.5];
const WHO_P97=[4.3,5.7,7.0,7.9,8.6,9.2,9.7,10.2,10.5,10.9,11.2,11.5,11.8,12.1,12.4,12.7,12.9,13.2,13.5,13.7,14.0,14.3];
const DISCHARGE_DAY=2, DISCHARGE_G=2850;
const GAIN_P1_END=Math.round(3*30.4375), GAIN_P2_END=Math.round(6*30.4375);
function whoInterpKg(tab,day){const m=day/30.4375;if(m<=0)return tab[0];if(m>=tab.length-1)return tab[tab.length-1];const i=Math.floor(m),f=m-i;return tab[i]+(tab[i+1]-tab[i])*f}
function expectedWeightBand(day,birthG){const bw=birthG||3080;const scale=bw/(WHO_MED[0]*1000);return{min:Math.round(whoInterpKg(WHO_P3,day)*1000*scale),med:Math.round(whoInterpKg(WHO_MED,day)*1000*scale),max:Math.round(whoInterpKg(WHO_P97,day)*1000*scale)}}
function dischargeGainBand(day){if(day<DISCHARGE_DAY)return null;let lo=DISCHARGE_G,hi=DISCHARGE_G;for(let d=DISCHARGE_DAY;d<day;d++){if(d<GAIN_P1_END){lo+=25;hi+=30}else if(d<GAIN_P2_END){lo+=15;hi+=20}}return{lo,hi}}

/* ---------- Pomocnicze ---------- */
const NS='http://www.w3.org/2000/svg';
const svgEl=(n,a)=>{const e=document.createElementNS(NS,n);for(const k in a)e.setAttribute(k,a[k]);return e};
function setText(id,v){const el=$(id);if(el)el.textContent=v==null?'':v}
/* Subtelny count-up liczby (premium/minimal). Respektuje prefers-reduced-motion. */
const _cuState={};
function countUp(id,target){
  const el=$(id);if(!el)return;target=Number(target)||0;
  const reduce=window.matchMedia&&window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  const from=_cuState[id]||0;_cuState[id]=target;
  if(reduce||from===target){el.textContent=target;return}
  const dur=520,t0=performance.now();
  const step=now=>{const p=Math.min((now-t0)/dur,1);const e=1-Math.pow(1-p,3);el.textContent=Math.round(from+(target-from)*e);if(p<1)requestAnimationFrame(step)};
  requestAnimationFrame(step);
}
function pad(n){return String(n).padStart(2,'0')}
function dateLabel(iso){const p=(iso||'').split('-');return p.length===3?`${p[2]}.${p[1]}.${p[0]}`:iso}
function isoDaysAgo(n){const d=new Date();d.setDate(d.getDate()-n);return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}`}
function dateTimeInput(v){const d=v?new Date(v):new Date();if(Number.isNaN(d.getTime()))return'';return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`}
function fmtGap(m){if(m==null||m<0)return'—';const h=Math.floor(m/60),mm=m%60;return h>0?`${h}h ${mm}min`:`${mm}min`}
function fmtDur(m){if(m==null||m<0)return'—';const h=Math.floor(m/60),mm=m%60;return h>0?`${h}h ${mm} min`:`${mm} min`}
function isoHM(iso){return(iso&&iso.length>=16)?iso.slice(11,16):''}

async function request(url,options){
  const r=await fetch(url,options);let data={};
  try{data=await r.json()}catch(e){}
  if(!r.ok)throw new Error(data.message||'Błąd połączenia z serwerem');
  return data;
}

/* ---------- Toasty ---------- */
function toast(msg,kind='ok'){
  const w=$('toastWrap');const t=document.createElement('div');t.className=`toast ${kind}`;
  const ic=kind==='ok'
    ?'<path d="M5 13l4 4L19 7" stroke="#fff" stroke-width="2.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'
    :'<path d="M12 8v5M12 16.5v.5" stroke="#fff" stroke-width="2.4" stroke-linecap="round"/>';
  t.innerHTML=`<span class="ti"><svg viewBox="0 0 24 24">${ic}</svg></span><span>${msg}</span>`;
  w.appendChild(t);requestAnimationFrame(()=>t.classList.add('show'));
  setTimeout(()=>{t.classList.remove('show');setTimeout(()=>t.remove(),320)},3200);
}

/* ---------- Nawigacja podstron ---------- */
function navTo(page){
  if(!PAGES.includes(page))page='start';
  state.page=page;
  PAGES.forEach(p=>{const el=$('page-'+p);if(el){const on=(p===page);el.hidden=!on;el.classList.toggle('page-active',on)}});
  document.querySelectorAll('.dock .nav').forEach(b=>b.classList.toggle('active',b.dataset.nav===page));
  setText('pageTitle',PAGE_TITLES[page]||'Leśny Dziennik');
  // render zawartości przy wejściu (lazy)
  renderPage(page);
  window.scrollTo({top:0,behavior:'smooth'});
}
/* Renderuje zawartość podstrony korzystając z danych z ostatniego /api/status. */
function renderPage(page){
  if(page==='diary'){state.activeDay=null;const dd=$('diaryDetail');if(dd)dd.classList.add('hidden');if(state.data)renderCalendarPreview(state.data.calendar)}
  else if(page==='stats'){renderStatView(state.statView)}
  else if(page==='weight'){const d=state.data||{};$('weightG').value=(d.lastWeightG&&d.lastWeightG>0)?d.lastWeightG:3700;setText('weightNotice','');renderWeightChart();renderGainChart()}
  else if(page==='sleep'){renderSleep()}
}
/* Przełącznik widoków w Statystykach: summary | charts | rhythm | history */
const STAT_VIEWS=['summary','charts','rhythm','history'];
function renderStatView(view){
  if(!STAT_VIEWS.includes(view))view='summary';
  state.statView=view;
  STAT_VIEWS.forEach(v=>{const el=$('stat-'+v);if(el)el.classList.toggle('hidden',v!==view)});
  document.querySelectorAll('#statTabs .tab').forEach(t=>t.classList.toggle('active',t.dataset.stat===view));
  if(view==='summary'){state.summaryExtra=0;renderSummary()}
  else if(view==='charts'){if(state.data&&state.data.calendar)renderChart(state.data.calendar);renderGaps()}
  else if(view==='rhythm'){renderAnalysis()}
  else if(view==='history'){renderHistory(state.histPeriod)}
}

/* ---------- Modale (formularze) ---------- */
function show(view){
  state.view=view||null;
  MODALS.forEach(m=>{const el=$(m);if(!el)return;const on=(m===view);el.classList.toggle('open',on);el.setAttribute('aria-hidden',on?'false':'true')});
  document.body.classList.toggle('modal-open',MODALS.some(m=>{const el=$(m);return el&&el.classList.contains('open')}));
}
function openFormModal(){$('formModal').classList.add('open');$('formModal').setAttribute('aria-hidden','false');document.body.classList.add('modal-open')}
function closeFormModal(){const p=$('mlPop');if(p)p.classList.add('hidden');state.mlPopKind=null;$('formModal').classList.remove('open');$('formModal').setAttribute('aria-hidden','true');document.body.classList.remove('modal-open');state.pumpMode=false}
/* Zamyka otwarte modale-formularze (nie zmienia aktywnej podstrony). */
function clearPanels(){MODALS.forEach(m=>{const el=$(m);if(el){el.classList.remove('open');el.setAttribute('aria-hidden','true')}});document.body.classList.remove('modal-open')}

/* ---------- Motyw dzień/noc ---------- */
function applyTheme(t){document.documentElement.setAttribute('data-theme',t);try{localStorage.setItem('lesny-theme',t)}catch(e){}}
let themeManual=false;
(function initTheme(){let saved=null;try{saved=localStorage.getItem('lesny-theme')}catch(e){}if(saved){themeManual=true;applyTheme(saved)}})();
$('themeToggle').addEventListener('click',()=>{themeManual=true;const cur=document.documentElement.getAttribute('data-theme');applyTheme(cur==='night'?'day':'night')});

/* ---------- Ikony wpisów ---------- */
function entryIcon(type){
  if(type==='KARMIENIE')return['feed','<path d="M8 3h8l-1 5a4 4 0 0 1-6 0L8 3Z" fill="#fff"/><path d="M10.5 13h3l-.5 8h-2l-.5-8Z" fill="#fff"/>'];
  if((type||'').startsWith('MLEKO'))return['milk','<path d="M9 2h6v3l1 3v12a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V8l1-3V2Z" fill="#fff"/>'];
  if(type==='PIELUCHA_MOKRA'||type==='PIELUCHA_BRUDNA')return['diaper','<path d="M4 6h16v5a8 8 0 0 1-16 0V6Z" fill="#fff"/>'];
  if(type==='SEN_START'||type==='SEN_STOP')return['sleep','<path d="M20 14A8 8 0 0 1 10 4a7 7 0 1 0 10 10Z" fill="#fff"/>'];
  if(type==='KAPIEL')return['bath','<path d="M4 12h16v1a6 6 0 0 1-6 6H10a6 6 0 0 1-6-6v-1Z" fill="#fff"/><path d="M6 12V6.5a2 2 0 0 1 2-2 2 2 0 0 1 2 2V7" stroke="#fff" stroke-width="1.6" stroke-linecap="round"/>'];
  return['other','<circle cx="12" cy="12" r="7" stroke="#fff" stroke-width="1.8" fill="none"/><path d="M12 8v4l3 2" stroke="#fff" stroke-width="1.8" fill="none" stroke-linecap="round"/>'];
}

/* ============================================================================
   RENDER — ekran główny
   ============================================================================ */
function render(data){
  state.data=data;
  setText('clock',data.now||'…');
  setText('ageChip',(data.age||'').split('\n')[1]||(data.age||'').split('\n')[0]||'…');
  setText('devTip',data.developmentTip||'Oczekiwanie na wskazówkę…');

  // licznik + pierścień (r=78)
  const ageMin=data.lastFeedingAgeMin;
  const ring=$('ringProg');const C=2*Math.PI*70;
  ring.setAttribute('stroke-dasharray',C.toFixed(1));
  if(ageMin>=0){
    const frac=Math.min(ageMin/240,1);
    ring.setAttribute('stroke-dashoffset',(C*(1-frac)).toFixed(1));
    const h=Math.floor(ageMin/60),m=ageMin%60;
    setText('counterBig',h>0?`${h}:${pad(m)}`:`${m}`);
    setText('counterLbl',h>0?'godz. temu':'min temu');
    setText('counterHeadline',data.lastFeedingAgo||'—');
  }else{ring.setAttribute('stroke-dashoffset',C.toFixed(1));setText('counterBig','—');setText('counterLbl','brak wpisu');setText('counterHeadline','Brak wpisu')}
  setText('nextTime',data.nextFeedingIso?('~'+data.nextFeedingIso.slice(11,16)):'—');
  setText('avgGap',fmtGap(data.avgFeedingGapMin));
  const dot=$('mDot');if(dot){dot.className='d';if(ageMin>=240)dot.classList.add('danger');else if(ageMin>=180)dot.classList.add('warn')}

  // oddychający pierścień gdy zbliża się / minął czas karmienia
  const rw=$('ringWrap');if(rw)rw.classList.toggle('breathe',ageMin>=180);

  // kafelki dnia (dziś = calendar[0]): liczba + krótka informacja pod spodem
  const t0=(data.calendar&&data.calendar[0])||{};
  countUp('dsFeeds',t0.feedingCount||0);
  countUp('dsMilk',t0.milkMl||0);
  countUp('dsSleep',data.napCount||0);
  const clockOf=txt=>{const m=/(\d{1,2}:\d{2})/.exec(String(txt||'').split('\n')[0]);return m?m[1]:''};
  const lf=clockOf(data.lastFeeding),lm=clockOf(data.lastMilk);
  setText('dsFeedsSub',lf?`ostatnie o ${lf}`:'brak wpisów');
  setText('dsMilkSub',lm?`butelka o ${lm}`:'brak butelek');
  const sleepTotal=(data.sleepDayMin||0)+(data.sleepNightMin||0);
  setText('dsSleepSub',data.sleepInProgress?'śpi teraz':(sleepTotal>0?`razem ${Math.floor(sleepTotal/60)}h${sleepTotal%60?` ${sleepTotal%60}min`:''}`:'brak snu dziś'));
  // kąpiel: ostatnia + planowana następna (co bathIntervalDays dni)
  {
    const tile=$('dsBathTile');let big='brak',sub='zapisz pierwszą kąpiel',late=false;
    if(data.lastBath){
      const pb=data.lastBath.split('-');const then=new Date(+pb[0],+pb[1]-1,+pb[2]);
      const nowN=new Date();const nowD=new Date(nowN.getFullYear(),nowN.getMonth(),nowN.getDate());
      const days=Math.max(0,Math.round((nowD-then)/86400000));
      const iv=data.bathIntervalDays||3;const dn=iv-days;
      big=days===0?'dziś':days===1?'wczoraj':`${days} dni temu`;
      late=dn<0;
      sub=late?`zaległa o ${-dn} ${-dn===1?'dzień':'dni'}`:dn===0?'następna: dziś':dn===1?'następna: jutro':`następna za ${dn} dni`;
    }
    setText('dsBath',big);setText('dsBathSub',sub);if(tile)tile.classList.toggle('late',late);
  }

  // połączona karta posiłku (czas + opis osobno)
  // W hero: karmienie = sama godzina (bo "X temu" jest w wielkim nagłówku), butelka = godz + ml/rodzaj
  fillMini('feedTime','feedDesc',data.lastFeeding,null,false);
  fillMini('milkTime','milkDesc',data.lastMilk,null,true);

  // motyw wg pory (o ile użytkownik nie wybrał ręcznie)
  if(!themeManual)applyTheme(data.night?'night':'day');

  setText('dockSleep',data.sleepInProgress?'Śpi':'Sen');

  renderDayBand();
  renderDiag(data);
  // odśwież żywe fragmenty aktywnej podstrony (polling co 10 s)
  if(state.page==='diary'&&!state.activeDay)renderCalendarPreview(data.calendar);
  else if(state.page==='sleep')renderSleep();
}
/* Rozdziela wpis "…  HH:MM \n szczegóły" na czas + opis dla mini-kart. */
function fillMini(timeId,descId,value,ago,bottle){
  if(!value||value.startsWith('Brak')){setText(timeId,'—');setText(descId,'Brak wpisu');return}
  const parts=value.split('\n');
  const time=(parts[0].split('  ').pop()||parts[0]);
  let desc;
  if(ago)desc=ago;
  else if(bottle){
    // parts[1] np. "MLEKO MODYFIKOWANE | 100 ml" -> "100 ml · modyfikowane"
    const raw=parts[1]||'';
    const mlMatch=raw.match(/(\d+)\s*ml/i);
    const ml=mlMatch?`${mlMatch[1]} ml`:'';
    let kind='';
    if(/MODYFIKOWANE/i.test(raw))kind='modyfikowane';
    else if(/MATKI/i.test(raw))kind='matki';
    desc=[ml,kind].filter(Boolean).join(' · ');
  }
  else desc='';  // karmienie w hero: sama godzina (bez powielania "temu")
  setText(timeId,time);setText(descId,desc);
}



/* ---------- Kalendarz ---------- */
function chip(svg,txt){return `<span class="chip"><svg viewBox="0 0 24 24" fill="none">${svg}</svg>${txt}</span>`}
function buildCalCard(day,onClick){
  const p=(day.date||'').split('-');
  const card=document.createElement('article');card.className='card cal-day';
  if(day.label&&day.label.startsWith('DZISIAJ'))card.classList.add('today');
  const months=['sty','lut','mar','kwi','maj','cze','lip','sie','wrz','paź','lis','gru'];
  const mIdx=Math.max(0,Math.min(11,(parseInt(p[1],10)||1)-1));
  const hasAny=dayHasActivity(day);
  const chipsHtml = hasAny
    ? (
        ((day.feedingCount||0)>0 ? chip('<path d="M8 3h8l-1 5a4 4 0 0 1-6 0L8 3Z" fill="currentColor"/>',`${day.feedingCount} karm.`) : '')+
        ((day.milkMl||0)>0 ? chip('<path d="M9 2h6v3l1 3v12a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V8l1-3V2Z" fill="currentColor"/>',`${day.milkMl} ml`) : '')+
        (((day.diaperWet||0)+(day.diaperDirty||0))>0 ? chip('<path d="M4 6h16v5a8 8 0 0 1-16 0V6Z" fill="currentColor"/>',`${(day.diaperWet||0)+(day.diaperDirty||0)} piel.`) : '')+
        (day.vitaminD?chip('<circle cx="12" cy="12" r="8" stroke="currentColor" stroke-width="1.6"/>','Wit.D'):'')
      )
    : `<span class="chip chip-empty">Brak wpisów</span>`;
  if(!hasAny)card.classList.add('cal-empty');
  card.innerHTML=
    `<div class="cd-top">`+
      `<div class="dnum"><span class="d">${p[2]||''}</span><span class="m">${months[mIdx]}</span></div>`+
      `<div class="cbody"><div class="cn">${day.label||''}</div><div class="chips">`+
        chipsHtml+
      `</div></div>`+
      `<span class="carr"><svg viewBox="0 0 24 24" width="20" height="20" fill="none"><path d="M9 6l6 6-6 6" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg></span>`+
    `</div>`;
  card.addEventListener('click',onClick);
  return card;
}
/* Czy dzien ma jakiekolwiek wpisy (do czystego stanu "Brak wpisow"). */
function dayHasActivity(d){
  return (d.feedingCount||0)>0 || (d.milkMl||0)>0 || (d.diaperWet||0)>0 || (d.diaperDirty||0)>0 || d.vitaminD;
}
/* Podgląd na stronie: dziś + wczoraj. Pełny modal: 5 dni (z mini-osią dnia). */
function renderCalendarPreview(days){
  if(!days)return;
  const list=$('calendarList');
  if(list){
    list.replaceChildren();
    days.forEach(day=>{const c=buildCalCard(day,()=>openDay(day.date,day.label));list.append(c);addMiniBar(c,day.date)});
  }
}
/* Cache wpisów per data (TTL 30s) — mini-bary i inne widoki nie odpytują przy każdym pollingu. */
let _entriesCache={};
function invalidateEntries(){_entriesCache={}}
async function entriesFor(date){
  const c=_entriesCache[date];
  if(c&&(Date.now()-c.t)<30000)return c.es;
  const d=await request(`/api/entries?date=${encodeURIComponent(date)}`);
  const es=d.entries||[];_entriesCache[date]={t:Date.now(),es};return es;
}
/* Mini oś doby w kafelku dnia — asynchronicznie, nie blokuje renderu. */
async function addMiniBar(card,date){
  let es=[];try{es=await entriesFor(date)}catch(e){return}
  if(!es.length)return;
  const bar=document.createElement('div');bar.className='cminibar';
  let sStart=null;
  es.forEach(e=>{if(e.type==='SEN_START'){sStart=toMin(e.time)}else if(e.type==='SEN_STOP'&&sStart!=null){const m2=toMin(e.time);if(m2!=null){const s=document.createElement('div');s.className='s';s.style.left=(sStart/1440*100)+'%';s.style.width=(Math.max(m2-sStart,5)/1440*100)+'%';bar.append(s)}sStart=null}});
  es.forEach(e=>{const mins=toMin(e.time);if(mins==null)return;let cls=e.type==='KARMIENIE'?'feed':(e.type||'').startsWith('MLEKO')?'milk':(e.type==='PIELUCHA_MOKRA'||e.type==='PIELUCHA_BRUDNA')?'diaper':null;if(!cls)return;const m=document.createElement('div');m.className='m '+cls;m.style.left=(mins/1440*100)+'%';bar.append(m)});
  card.append(bar);
}

/* ---------- Dayband ---------- */
async function renderDayBand(){
  const band=$('dayband');if(!band)return;
  let es=[];try{const d=await request(`/api/entries?date=${encodeURIComponent(isoDaysAgo(0))}`);es=d.entries||[]}catch(e){return}
  band.replaceChildren();
  const toMin=t=>{const p=(t||'').split(':');return p.length<2?null:(+p[0])*60+(+p[1])};
  const nowM=new Date().getHours()*60+new Date().getMinutes();
  let sStart=null;
  es.forEach(e=>{
    if(e.type==='SEN_START'){sStart=toMin(e.time)}
    else if(e.type==='SEN_STOP'&&sStart!=null){const m2=toMin(e.time);if(m2!=null){const sp=document.createElement('div');sp.className='db-sleep';sp.style.left=(sStart/1440*100)+'%';sp.style.width=(Math.max(m2-sStart,4)/1440*100)+'%';band.appendChild(sp)}sStart=null}
  });
  if(sStart!=null){const sp=document.createElement('div');sp.className='db-sleep';sp.style.left=(sStart/1440*100)+'%';sp.style.width=(Math.max(nowM-sStart,4)/1440*100)+'%';band.appendChild(sp)}
  es.forEach(e=>{const mins=toMin(e.time);if(mins==null)return;let cls=null;if(e.type==='KARMIENIE')cls='feed';else if((e.type||'').startsWith('MLEKO'))cls='milk';else if(e.type==='PIELUCHA_MOKRA'||e.type==='PIELUCHA_BRUDNA')cls='diaper';if(!cls)return;const m=document.createElement('div');m.className='db-mark '+cls;m.style.left=(mins/1440*100)+'%';m.title=e.time+' '+(e.label||e.type);band.appendChild(m)});
  const nw=document.createElement('div');nw.className='db-now';nw.style.left=(nowM/1440*100)+'%';band.appendChild(nw);
}

/* ---------- Diagnostyka ---------- */
function renderDiag(data){
  const g=$('diagGrid');if(!g||$('diagCard').classList.contains('hidden'))return;
  const row=(k,v,cls)=>`<div class="dk">${k}</div><div class="dv ${cls||''}">${v}</div>`;
  const up=data.uptimeSec||0,d=Math.floor(up/86400),h=Math.floor((up%86400)/3600),m=Math.floor((up%3600)/60);
  let html='';
  html+=row('Serwer',data.storage?'aktywny':'—',data.storage?'ok':'bad');
  html+=row('Czas',data.timeValid?'OK':'—',data.timeValid?'ok':'bad');
  html+=row('Pamięć danych',data.storage?'OK':'błąd',data.storage?'ok':'bad');
  html+=row('Wiek (dni)',data.developmentDay!=null?data.developmentDay:'—');
  html+=row('Ostatnia waga',(data.lastWeightG||0)+' g');
  html+=row('Tryb',data.night?'nocny':'dzienny');
  html+=`<div class="dk">Powiadomienia snu (Telegram)</div><div class="dv"><label class="switch"><input type="checkbox" id="sleepTgChk" ${data.sleepTelegram?'checked':''}><span class="track2"></span></label></div>`;
  g.innerHTML=html;
  const chk=$('sleepTgChk');
  if(chk)chk.addEventListener('change',async()=>{try{await request('/api/setting',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({key:'sleepTelegram',value:chk.checked?'1':'0'})});toast('Zapisano ustawienie')}catch(e){toast(e.message,'err')}});
}

/* ============================================================================
   Dziennik dnia + wpisy
   ============================================================================ */
function buildEntry(en,onDeleted){
  const [cls,icon]=entryIcon(en.type);
  const row=document.createElement('div');row.className='entry';
  const sub=(en.type==='KARMIENIE'&&((en.piersLeftMin||0)+(en.piersRightMin||0))>0)?`L${en.piersLeftMin||0} / P${en.piersRightMin||0} min`:'';
  const amt=(en.type==='KARMIENIE'||Number(en.ml)===0)?'':`${en.ml} ml`;
  row.innerHTML=
    `<span class="e-ic ${cls}"><svg viewBox="0 0 24 24" fill="none">${icon}</svg></span>`+
    `<div class="e-main"><div class="e-title">${en.label||en.type}${amt?` · ${amt}`:''}</div>${sub?`<div class="e-sub">${sub}</div>`:''}</div>`+
    `<span class="e-time">${en.time}</span>`+
    `<button class="e-del" title="Usuń wpis"><svg viewBox="0 0 24 24" fill="none"><path d="M6 6l12 12M18 6 6 18" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></button>`;
  row.querySelector('.e-del').addEventListener('click',async e=>{
    e.stopPropagation();
    if(!confirm(`Usunąć wpis?\n${en.label||en.type}, ${en.time}${amt?', '+amt:''}`))return;
    try{await request('/api/delete-entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({line:String(en.lineIndex)})});toast('Usunięto wpis');await refresh();if(onDeleted)onDeleted()}
    catch(ex){toast(ex.message,'err')}
  });
  return row;
}
const DOW=['niedziela','poniedziałek','wtorek','środa','czwartek','piątek','sobota'];
function toMin(t){const p=(t||'').split(':');return p.length<2?null:(+p[0])*60+(+p[1])}

/* ============================================================================
   WSPÓLNA OŚ CZASU (kreska + kropki) — główny sposób pokazywania wpisów.
   Karmienia = duże kropki (feed), butelki = mniejsze (milk), pieluchy/inne osobno,
   sen = pasmo. Pod karmieniem pokazujemy odstęp „po Xh Ymin". onChanged() po usunięciu.
   ============================================================================ */
function buildTimeline(entries, onChanged){
  const wrap=document.createElement('div');wrap.className='tline';
  if(!entries||!entries.length){const em=document.createElement('p');em.className='empty';em.textContent='Brak wpisów';return em}
  // sekwencja pozycji w porządku wejścia (API jest chronologiczne); sen parujemy do pasm
  let prevFeed=null,sStart=null;
  entries.forEach(e=>{
    if(e.type==='SEN_START'){sStart=e.time;return}
    if(e.type==='SEN_STOP'){
      if(sStart!=null){const a=toMin(sStart),b=toMin(e.time);const band=document.createElement('div');band.className='tl-sleepband';
        band.innerHTML=`<svg viewBox="0 0 24 24" fill="none"><path d="M20 14A8 8 0 0 1 10 4a7 7 0 1 0 10 10Z" fill="currentColor"/></svg>Sen ${sStart}–${e.time}${(a!=null&&b!=null&&b>a)?` · ${fmtGap(b-a)}`:''}`;
        wrap.append(band);sStart=null}
      return;
    }
    const [cls,icon]=entryIcon(e.type);
    const row=document.createElement('div');row.className='tl-row '+cls;
    // podtytuł + odstęp
    let sub='';
    if(e.type==='KARMIENIE'){
      const pm=(e.piersLeftMin||0)+(e.piersRightMin||0);
      sub=pm>0?`Pierś · L${e.piersLeftMin||0} / P${e.piersRightMin||0} min`:'Karmienie';
    }else if((e.type||'').startsWith('MLEKO')){
      sub=(e.label||'Butelka')+(e.ml?` · ${e.ml} ml`:'');
    }else if(e.type==='ODCIAGANIE'){sub='Odciąganie'+(e.ml?` · ${e.ml} ml`:'');}
    else if(e.type==='PIELUCHA_MOKRA'){sub='Pielucha mokra';}
    else if(e.type==='PIELUCHA_BRUDNA'){sub='Pielucha brudna';}
    else if(e.type==='WITAMINA_D'){sub='Witamina D';}
    else if(e.type==='WAGA'){sub='Waga'+(e.ml?` · ${e.ml} g`:'');}
    else sub=e.label||e.type;
    // odstęp od poprzedniego karmienia (tylko dla karmień)
    let gapHtml='';
    if(e.type==='KARMIENIE'){const m=toMin(e.time);if(prevFeed!=null&&m!=null&&m>prevFeed)gapHtml=`<span class="tl-gap">po ${fmtGap(m-prevFeed)}</span>`;if(m!=null)prevFeed=m}
    const title=e.type==='KARMIENIE'?'Karmienie':((e.type||'').startsWith('MLEKO')?(e.label||'Butelka'):(e.type==='ODCIAGANIE'?'Odciąganie':(e.type==='PIELUCHA_MOKRA'?'Pielucha':(e.type==='PIELUCHA_BRUDNA'?'Pielucha':(e.type==='WITAMINA_D'?'Witamina D':(e.type==='WAGA'?'Waga':(e.label||e.type)))))));
    const editBtn=(e.type==='KARMIENIE')
      ? `<button class="tl-edit" title="Edytuj karmienie"><svg viewBox="0 0 24 24" fill="none"><path d="M4 20h4l10-10-4-4L4 16v4Z" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"/><path d="M13.5 6.5l4 4" stroke="currentColor" stroke-width="1.8"/></svg></button>`
      : '';
    row.innerHTML=
      `<span class="tl-time">${e.time}</span>`+
      `<span class="tl-rail"><span class="tl-node"></span></span>`+
      `<div class="tl-card"><span class="tl-ic ${cls}"><svg viewBox="0 0 24 24" fill="none">${icon}</svg></span>`+
        `<div class="tl-main"><div class="tl-t">${title}</div><div class="tl-s">${sub}</div></div>`+
        gapHtml+
        editBtn+
        `<button class="tl-del" title="Usuń wpis"><svg viewBox="0 0 24 24" fill="none"><path d="M6 6l12 12M18 6 6 18" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg></button>`+
      `</div>`;
    if(e.type==='KARMIENIE'){
      const editEl=row.querySelector('.tl-edit');
      if(editEl)editEl.addEventListener('click',ev=>{
        ev.stopPropagation();
        // znajdz WSZYSTKIE sparowane wiersze mleka o TYM SAMYM czasie (matki+modyfikowane
        // = mleko mieszane, lub stary jednowierszowy MLEKO_MIESZANE)
        const milks=(entries||[]).filter(x=>x.time===e.time&&(x.type||'').startsWith('MLEKO'));
        openEditFeeding(e,milks);
      });
    }
    row.querySelector('.tl-del').addEventListener('click',async ev=>{
      ev.stopPropagation();
      if(!confirm(`Usunąć wpis?\n${title}, ${e.time}`))return;
      try{await request('/api/delete-entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({line:String(e.lineIndex)})});toast('Usunięto wpis');invalidateEntries();await refresh();if(onChanged)onChanged()}
      catch(ex){toast(ex.message,'err')}
    });
    wrap.append(row);
  });
  // sen w toku (otwarty start bez stop)
  if(sStart!=null){const band=document.createElement('div');band.className='tl-sleepband';band.innerHTML=`<svg viewBox="0 0 24 24" fill="none"><path d="M20 14A8 8 0 0 1 10 4a7 7 0 1 0 10 10Z" fill="currentColor"/></svg>Sen od ${sStart} · w toku`;wrap.append(band)}
  return wrap;
}

/* Graficzny widok dnia: nagłówek + bento podsumowanie + łuk doby + oś czasu 24h + wpisy. */
async function openDay(date,label){
  state.activeDay=date;state.detailLabel=label;
  const dd=$('diaryDetail');if(dd){dd.classList.remove('hidden');dd.scrollIntoView({behavior:'smooth',block:'start'})}
  const list=$('detailList');list.replaceChildren();
  const sk=document.createElement('div');sk.className='skeleton';sk.style.height='120px';list.append(sk);
  let entries=[];
  try{const data=await request(`/api/entries?date=${encodeURIComponent(date)}`);entries=data.entries||[]}
  catch(e){setText('detailList',e.message);return}
  list.replaceChildren();

  // agregacja dnia
  let feeds=0,milkMl=0,mother=0,modif=0,wet=0,dirty=0,pump=0,vit=false,baths=0;
  const sleeps=[];let sStart=null;
  entries.forEach(e=>{
    if(e.type==='KARMIENIE')feeds++;
    else if((e.type||'').startsWith('MLEKO')){milkMl+=e.ml||0;if(e.type==='MLEKO_MATKI')mother+=e.ml||0;else if(e.type==='MLEKO_MODYFIKOWANE')modif+=e.ml||0}
    else if(e.type==='PIELUCHA_MOKRA')wet++;
    else if(e.type==='PIELUCHA_BRUDNA')dirty++;
    else if(e.type==='ODCIAGANIE')pump+=e.ml||0;
    else if(e.type==='WITAMINA_D')vit=true;
    else if(e.type==='KAPIEL')baths++;
    else if(e.type==='SEN_START')sStart=toMin(e.time);
    else if(e.type==='SEN_STOP'&&sStart!=null){const m2=toMin(e.time);if(m2!=null)sleeps.push([sStart,m2]);sStart=null}
  });
  // Sen w toku (SEN_START bez STOP): dla dnia dzisiejszego domykamy "do teraz" — spójnie z paskiem na home.
  if(sStart!=null&&date===isoDaysAgo(0)){const nowM=new Date().getHours()*60+new Date().getMinutes();if(nowM>sStart)sleeps.push([sStart,nowM])}
  const p=(date||'').split('-');const dObj=new Date(+p[0],(+p[1]||1)-1,+p[2]||1);
  const dowName=Number.isNaN(dObj.getTime())?'':DOW[dObj.getDay()];

  // nagłówek dnia
  const hero=document.createElement('div');hero.className='day-hero';
  hero.innerHTML=`<div class="dd">${label&&label.split(' - ')[0]?label.split(' - ')[0]:dateLabel(date)}</div><div class="dw">${dowName} · ${dateLabel(date)}</div>`;
  list.append(hero);

  // bento podsumowanie
  const bento=document.createElement('div');bento.className='day-bento';
  const cell=(cls,v,k)=>`<div class="db-cell ${cls}"><div class="cv">${v}</div><div class="ck">${k}</div></div>`;
  bento.innerHTML=
    cell('feed',feeds,'karmień')+
    cell('milk',milkMl,'ml mleka')+
    cell('diaper',(wet+dirty),'pieluchy')+
    cell('sleep',sleeps.length,'drzemki')+
    (baths?cell('bath',baths,'kąpiel'):'');
  list.append(bento);

  // łuk doby (pasma snu + znaczniki)
  const arc=document.createElement('div');arc.className='day-arc';
  sleeps.forEach(([a,b])=>{const s=document.createElement('div');s.className='db-sleep';s.style.left=(a/1440*100)+'%';s.style.width=(Math.max(b-a,5)/1440*100)+'%';arc.append(s)});
  entries.forEach(e=>{const mins=toMin(e.time);if(mins==null)return;let cls=e.type==='KARMIENIE'?'feed':(e.type||'').startsWith('MLEKO')?'milk':(e.type==='PIELUCHA_MOKRA'||e.type==='PIELUCHA_BRUDNA')?'diaper':null;if(!cls)return;const m=document.createElement('div');m.className='db-mark '+cls;m.style.left=(mins/1440*100)+'%';arc.append(m)});
  list.append(arc);

  // OŚ CZASU — kreska + kropki (główny widok wpisów)
  const tlTitle=document.createElement('div');tlTitle.className='tl-title';tlTitle.textContent='Oś czasu dnia';list.append(tlTitle);
  list.append(buildTimeline(entries,()=>openDay(state.activeDay,state.detailLabel)));
}

/* ---------- Podsumowanie (2 dni + starsze) ---------- */
async function renderSummary(){
  const root=$('summaryList');root.replaceChildren();
  const sk=document.createElement('div');sk.className='skeleton';sk.style.height='80px';root.append(sk);
  const cal=state.data&&state.data.calendar?state.data.calendar:[];
  const days=[cal[0],cal[1]].filter(Boolean);
  let lists;try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).catch(()=>({entries:[]}))))}catch(e){root.replaceChildren();setText('summaryList',e.message);return}
  root.replaceChildren();
  const dayStatLine=es=>{let f=0,ml=0;es.forEach(e=>{if(e.type==='KARMIENIE')f++;else if((e.type||'').startsWith('MLEKO'))ml+=e.ml||0});return `${f} karmień · ${ml} ml`};
  days.forEach((d,i)=>{
    const es=(lists[i]&&lists[i].entries)||[];
    const block=document.createElement('div');block.className='day-block';
    const h=document.createElement('div');h.className='dbh';
    h.innerHTML=`<span class="t">${(i===0?'Dzisiaj':'Wczoraj')} · ${dateLabel(d.date).slice(0,5)}</span><span class="s">${dayStatLine(es)}</span>`;
    block.append(h);
    block.append(buildTimeline(es,()=>renderSummary()));
    root.append(block);
  });
  for(let k=2;k<2+(state.summaryExtra||0);k++){
    const ds=isoDaysAgo(k);
    try{const es=await entriesFor(ds);
      const block=document.createElement('div');block.className='day-block';
      const h=document.createElement('div');h.className='dbh';
      h.innerHTML=`<span class="t">${dateLabel(ds).slice(0,5)}</span><span class="s">${dayStatLine(es)}</span>`;
      block.append(h);block.append(buildTimeline(es,()=>renderSummary()));root.append(block);
    }catch(e){}
  }
  const more=document.createElement('button');more.className='btn ghost';more.textContent='＋ Wczytaj starsze dni';more.style.marginTop='4px';
  more.addEventListener('click',()=>{state.summaryExtra=(state.summaryExtra||0)+7;renderSummary()});root.append(more);
  // Aktualizacja etykiety przycisku Wit.D (ostatni węzeł tekstowy w przycisku)
  const t0=cal[0];const vb=$('vitBtn');
  if(t0&&vb){const last=vb.childNodes[vb.childNodes.length-1];if(last&&last.nodeType===3)last.textContent=t0.vitaminD?'Wit. D ✓':'Wit. D'}
}

/* ============================================================================
   Wykresy — 5 dni (mleko/karmienia), odstępy, rytm
   ============================================================================ */
function renderChart(cal){
  const MAX=Math.max(...cal.map(d=>d.milkMl),1);
  const host=$('milkChart');host.replaceChildren();
  cal.slice().reverse().forEach(d=>{
    const col=document.createElement('div');col.className='bar-col';
    const v=document.createElement('div');v.className='v';v.textContent=d.milkMl||'0';
    const stack=document.createElement('div');stack.className='bstack';stack.style.height=((d.milkMl/MAX)*150+4)+'px';
    if(d.motherMilkMl>0){const s=document.createElement('div');s.className='bseg mother';s.style.height=(d.motherMilkMl/Math.max(d.milkMl,1)*100)+'%';s.title=d.motherMilkMl+' ml matki';stack.append(s)}
    if((d.mixedMilkMl||0)>0){const s=document.createElement('div');s.className='bseg mixed';s.style.height=(d.mixedMilkMl/Math.max(d.milkMl,1)*100)+'%';s.title=d.mixedMilkMl+' ml mieszane';stack.append(s)}
    if(d.modifiedMilkMl>0){const s=document.createElement('div');s.className='bseg modified';s.style.height=(d.modifiedMilkMl/Math.max(d.milkMl,1)*100)+'%';s.title=d.modifiedMilkMl+' ml modyf.';stack.append(s)}
    const lab=document.createElement('div');lab.className='lab';lab.textContent=(d.label||'').split(' - ').pop().slice(0,5);
    const feeds=document.createElement('div');feeds.className='feeds';feeds.textContent='● '+d.feedingCount;
    col.append(v,stack,feeds,lab);host.append(col);
  });
  const anyMixed=cal.some(d=>(d.mixedMilkMl||0)>0);
  let html='<table class="dtable"><tr><th>Dzień</th><th>Karm.</th><th>Matki</th><th>Mod.</th>'+(anyMixed?'<th>Miesz.</th>':'')+'<th>Suma</th></tr>';
  cal.forEach(d=>{const label=(d.label||'').split(' - ')[0];html+=`<tr><td>${label}</td><td>${d.feedingCount}</td><td>${d.motherMilkMl} ml</td><td>${d.modifiedMilkMl} ml</td>`+(anyMixed?`<td>${d.mixedMilkMl||0} ml</td>`:'')+`<td class="ok">${d.milkMl||0} ml</td></tr>`});
  html+='</table>';$('extraTable').innerHTML=html;
  // --- Kąpiele: mini kalendarz (zamiast słupków z 5 dni) ---
  const extraEl=$('extraTable');
  const bathBox=document.createElement('div');bathBox.style.marginTop='14px';
  bathBox.append(bcalElem('div','bath5-title','Kąpiele'));
  bathBox.firstChild.style.cssText='font-size:.72rem;font-weight:800;letter-spacing:.05em;color:var(--muted);text-transform:uppercase;margin-bottom:8px';
  const bathHost=document.createElement('div');bathBox.append(bathHost);extraEl.append(bathBox);
  renderBathCalendar(bathHost);
}
/* Odstępy karmień dziś — renderowane w widoku "Wykresy". */
async function renderGaps(){
  const gapHost=$('gapChart');if(!gapHost)return;gapHost.replaceChildren();
  let today=[];try{today=await entriesFor(isoDaysAgo(0))}catch(e){}
  const feeds=today.filter(e=>e.type==='KARMIENIE').map(e=>e.time).filter(Boolean).sort();
  if(feeds.length<2){const p=document.createElement('p');p.className='empty';p.textContent=feeds.length?'Tylko jedno karmienie dziś.':'Brak karmień dziś.';gapHost.append(p);setText('gapSummary','');return}
  let gaps=[],maxGap=0;for(let i=1;i<feeds.length;i++){const g=toMin(feeds[i])-toMin(feeds[i-1]);gaps.push({from:feeds[i-1],to:feeds[i],min:g});if(g>maxGap)maxGap=g}
  const avg=Math.round(gaps.reduce((a,b)=>a+b.min,0)/gaps.length);
  setText('gapSummary',`Karmień: ${feeds.length} · średni odstęp ${fmtGap(avg)}`);
  gaps.forEach(g=>{const row=document.createElement('div');row.className='gap-row';row.innerHTML=`<div class="gap-time">${g.from} → ${g.to}</div><div class="gap-track"><div class="gap-fill" style="width:${maxGap?Math.max(6,g.min/maxGap*100):6}%"></div></div><div class="gap-val">${fmtGap(g.min)}</div>`;gapHost.append(row)});
}

/* Rytm — 3 dni: wizualne porównanie + szczegółowa oś każdego dnia. */
async function renderAnalysis(){
  const rh=$('rhythm3');if(rh)rh.replaceChildren();
  const cmp=$('rhythmCompare');if(cmp)cmp.replaceChildren();
  const cal=(state.data&&state.data.calendar)||[];const days=cal.slice(0,3);
  if(!days.length){if(rh){const p=document.createElement('p');p.className='empty';p.textContent='Brak danych.';rh.append(p)}return}
  let lists=[];try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).then(r=>r.entries||[]).catch(()=>[])))}catch(e){lists=days.map(()=>[])}

  // Metryki per dzień
  const dayLabels=['Dziś','Wczoraj','2 dni temu'];
  const stats=days.map((d,i)=>{
    const es=lists[i]||[];
    const feedTimes=es.filter(e=>e.type==='KARMIENIE').map(e=>toMin(e.time)).filter(v=>v!=null).sort((a,b)=>a-b);
    const gaps=[];for(let j=1;j<feedTimes.length;j++)gaps.push(feedTimes[j]-feedTimes[j-1]);
    const avgGap=gaps.length?Math.round(gaps.reduce((a,b)=>a+b,0)/gaps.length):0;
    let ml=0;es.forEach(e=>{if((e.type||'').startsWith('MLEKO'))ml+=e.ml||0});
    const diapers=es.filter(e=>e.type==='PIELUCHA_MOKRA'||e.type==='PIELUCHA_BRUDNA').length;
    return {label:dayLabels[i]||dateLabel(d.date).slice(0,5),date:d.date,feeds:feedTimes.length,ml,diapers,avgGap,feedTimes};
  });

  // --- WIZUALNE PORÓWNANIE (słupki obok siebie dla 4 metryk) ---
  if(cmp){
    const metrics=[
      {key:'feeds',name:'Karmienia',unit:'',cls:'feed'},
      {key:'ml',name:'Mleko',unit:' ml',cls:'milk'},
      {key:'diapers',name:'Pieluchy',unit:'',cls:'diaper'},
      {key:'avgGap',name:'Śr. przerwa',unit:'',fmt:fmtGap,cls:'acc'},
    ];
    let h='<div class="cmp-grid">';
    metrics.forEach(m=>{
      const vals=stats.map(s=>s[m.key]||0);const max=Math.max(1,...vals);
      h+=`<div class="cmp-card"><div class="cmp-name">${m.name}</div><div class="cmp-bars">`;
      stats.forEach((s,i)=>{
        const v=s[m.key]||0;const pct=Math.round(v/max*100);
        const disp=m.fmt?m.fmt(v):(v+m.unit);
        h+=`<div class="cmp-col"><div class="cmp-track"><div class="cmp-fill ${m.cls}" style="height:${Math.max(4,pct)}%"></div></div><div class="cmp-v">${disp}</div><div class="cmp-d">${s.label.replace('2 dni temu','2 dni')}</div></div>`;
      });
      h+='</div></div>';
    });
    h+='</div>';
    // trend: dziś vs wczoraj
    if(stats.length>=2){
      const d0=stats[0],d1=stats[1];
      const arrows={up:'<svg viewBox="0 0 24 24" fill="none"><path d="M12 5v14M6 11l6-6 6 6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>',down:'<svg viewBox="0 0 24 24" fill="none"><path d="M12 19V5M6 13l6 6 6-6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>',flat:'<svg viewBox="0 0 24 24" fill="none"><path d="M5 12h14" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"/></svg>'};
      const trend=(a,b,unit,fmt)=>{const diff=a-b;const cls=diff>0?'up':diff<0?'down':'flat';const val=fmt?fmt(Math.abs(diff)):(Math.abs(diff)+unit);return `<span class="trend ${cls}">${arrows[cls]}${diff===0?'bez zmian':val}</span>`};
      h+=`<div class="cmp-trend"><div class="ct-row"><span>Karmienia vs wczoraj</span>${trend(d0.feeds,d1.feeds,'')}</div>`+
         `<div class="ct-row"><span>Mleko vs wczoraj</span>${trend(d0.ml,d1.ml,' ml')}</div>`+
         `<div class="ct-row"><span>Pieluchy vs wczoraj</span>${trend(d0.diapers,d1.diapers,'')}</div></div>`;
    }
    cmp.innerHTML=h;
  }

  // --- SZCZEGÓŁOWA OŚ KAŻDEGO DNIA ---
  if(!rh)return;
  days.forEach((d,i)=>{
    const all=(lists[i]||[]).map(e=>{const f=e.type==='KARMIENIE',mk=(e.type||'').startsWith('MLEKO');if(!f&&!mk)return null;const p=e.time.split(':');return{mins:(+p[0])*60+(+p[1]),milk:mk,time:e.time,label:e.label||e.type,ml:e.ml||0}}).filter(Boolean).sort((a,b)=>a.mins-b.mins);
    const feeds=all.filter(f=>!f.milk);const gaps=[];for(let j=1;j<feeds.length;j++)gaps.push(feeds[j].mins-feeds[j-1].mins);
    const avg=gaps.length?Math.round(gaps.reduce((a,b)=>a+b,0)/gaps.length):0;
    const wrap=document.createElement('div');wrap.className='rday';
    wrap.innerHTML=`<div class="rh"><span class="rn">${dayLabels[i]||''} · ${dateLabel(d.date).slice(0,5)}</span><span class="rs"><b>${feeds.length}</b> karmień${gaps.length?` · śr. ${fmtGap(avg)}`:''}</span></div>`;
    if(!all.length){const em=document.createElement('div');em.className='empty';em.textContent='Brak karmień';wrap.append(em);rh.append(wrap);return}
    let prev=null;
    all.forEach(f=>{const it=document.createElement('div');it.className='ritem'+(f.milk?' milk':'');
      const sub=f.milk?'butelka':(prev!=null?'po '+fmtGap(f.mins-prev):'pierwsze');if(!f.milk)prev=f.mins;
      it.innerHTML=`<div class="rt">${f.time}</div><div class="rn2"></div><div class="ri"><div class="t">${f.milk?(f.label+(f.ml?` · ${f.ml} ml`:'')):'Karmienie'}</div><div class="s">${sub}</div></div>`;
      wrap.append(it)});
    rh.append(wrap);
  });
}

/* ============================================================================
   HISTORIA — agregacja z /export.csv (tydzień / miesiąc / rok)
   ============================================================================ */
let _csvCache=null; // {t, rows:[{date,time,type,ml,l,p}]}
async function loadCsvRows(){
  if(_csvCache&&(Date.now()-_csvCache.t)<60000)return _csvCache.rows;
  let text='';
  try{const r=await fetch('/export.csv');text=await r.text()}catch(e){return []}
  const rows=[];
  text.split(/\r?\n/).forEach((line,idx)=>{
    if(!line.trim())return;
    const c=line.split(',');
    if(idx===0&&/data/i.test(c[0]))return; // nagłówek
    const [date,time,type,ml,l,p]=c;
    if(!date||!type)return;
    rows.push({date:(date||'').trim(),time:(time||'').trim(),type:(type||'').trim(),ml:parseInt(ml,10)||0,l:parseInt(l,10)||0,p:parseInt(p,10)||0});
  });
  _csvCache={t:Date.now(),rows};
  return rows;
}
/* Agreguje wiersze CSV do mapy per-dzień z metrykami. */
function aggregateByDay(rows){
  const days={};
  const get=d=>days[d]||(days[d]={feeds:0,milkMl:0,motherMl:0,modMl:0,mixMl:0,wet:0,dirty:0,pump:0,vitD:0,sleepMin:0,weightG:0,baths:0,_sleepStart:null});
  rows.forEach(r=>{
    const d=get(r.date);
    switch(r.type){
      case 'KARMIENIE':d.feeds++;break;
      case 'MLEKO_MATKI':d.milkMl+=r.ml;d.motherMl+=r.ml;break;
      case 'MLEKO_MODYFIKOWANE':d.milkMl+=r.ml;d.modMl+=r.ml;break;
      case 'MLEKO_MIESZANE':d.milkMl+=r.ml;d.mixMl+=r.ml;break;
      case 'MLEKO':d.milkMl+=r.ml;d.modMl+=r.ml;break;
      case 'PIELUCHA_MOKRA':d.wet++;break;
      case 'PIELUCHA_BRUDNA':d.dirty++;break;
      case 'ODCIAGANIE':d.pump+=r.ml;break;
      case 'WITAMINA_D':d.vitD++;break;
      case 'WAGA':d.weightG=r.ml;break;
      case 'SEN_START':d._sleepStart=toMin(r.time);break;
      case 'SEN_STOP':if(d._sleepStart!=null){const e=toMin(r.time);if(e!=null&&e>d._sleepStart)d.sleepMin+=e-d._sleepStart;d._sleepStart=null}break;
      case 'KAPIEL':d.baths++;break;
    }
  });
  return days;
}
const MONTHS_PL=['sty','lut','mar','kwi','maj','cze','lip','sie','wrz','paź','lis','gru'];

async function renderHistory(period){
  const host=$('historyBody');if(!host)return;
  host.replaceChildren();
  const sk=document.createElement('div');sk.className='skeleton';sk.style.height='160px';host.append(sk);
  const rows=await loadCsvRows();
  const byDay=aggregateByDay(rows);
  const allDates=Object.keys(byDay).sort();
  host.replaceChildren();
  if(!allDates.length){const p=document.createElement('p');p.className='empty';p.textContent='Brak danych w historii.';host.append(p);return}

  // Zbuduj kubełki wg okresu
  let buckets;
  if(period==='week'){
    buckets=[];for(let i=6;i>=0;i--){const d=isoDaysAgo(i);buckets.push({sub:d.split('-')[2],dates:[d]})}
  }else if(period==='month'){
    buckets=[];for(let w=4;w>=0;w--){const ds=[];for(let k=6;k>=0;k--)ds.push(isoDaysAgo(w*7+k));buckets.push({sub:(w===0?'ten':'−'+w),dates:ds})}
  }else{
    buckets=[];const now=new Date();
    for(let m=11;m>=0;m--){const dt=new Date(now.getFullYear(),now.getMonth()-m,1);const y=dt.getFullYear(),mo=dt.getMonth();const ds=[];const daysInMonth=new Date(y,mo+1,0).getDate();for(let day=1;day<=daysInMonth;day++)ds.push(`${y}-${pad(mo+1)}-${pad(day)}`);buckets.push({sub:MONTHS_PL[mo],dates:ds})}
  }

  // Agreguj metryki w każdym kubełku
  const B=buckets.map(b=>{
    let feeds=0,milkMl=0,motherMl=0,modMl=0,mixMl=0,wet=0,dirty=0,pump=0,vitD=0,sleepMin=0,baths=0,activeDays=0,lastWeight=0;
    b.dates.forEach(d=>{const x=byDay[d];if(!x)return;if(x.feeds||x.milkMl||x.wet||x.dirty)activeDays++;feeds+=x.feeds;milkMl+=x.milkMl;motherMl+=x.motherMl;modMl+=x.modMl;mixMl+=(x.mixMl||0);wet+=x.wet;dirty+=x.dirty;pump+=x.pump;vitD+=x.vitD;sleepMin+=x.sleepMin;baths+=(x.baths||0);if(x.weightG)lastWeight=x.weightG});
    return {sub:b.sub,feeds,milkMl,motherMl,modMl,mixMl,wet,dirty,pump,vitD,sleepMin,baths,activeDays,lastWeight};
  });

  // Sumy zbiorcze okresu
  const tot=B.reduce((a,b)=>({feeds:a.feeds+b.feeds,milkMl:a.milkMl+b.milkMl,motherMl:a.motherMl+b.motherMl,modMl:a.modMl+b.modMl,mixMl:a.mixMl+b.mixMl,wet:a.wet+b.wet,dirty:a.dirty+b.dirty,pump:a.pump+b.pump,vitD:a.vitD+b.vitD,sleepMin:a.sleepMin+b.sleepMin,baths:a.baths+b.baths,activeDays:a.activeDays+b.activeDays}),{feeds:0,milkMl:0,motherMl:0,modMl:0,mixMl:0,wet:0,dirty:0,pump:0,vitD:0,sleepMin:0,baths:0,activeDays:0});
  const dd=Math.max(1,tot.activeDays);

  // --- KAFELKI ZBIORCZE ---
  const kpi=document.createElement('div');kpi.className='hist-kpi';
  const kcell=(v,k,cls)=>`<div class="hk ${cls}"><div class="hk-v">${v}</div><div class="hk-k">${k}</div></div>`;
  kpi.innerHTML=
    kcell(tot.feeds,'karmień','feed')+
    kcell(tot.milkMl+' ml','mleka','milk')+
    kcell(tot.wet+tot.dirty,'pieluch','diaper')+
    kcell(fmtDurShort(tot.sleepMin),'snu','sleep')+
    kcell(tot.baths,'kąpieli','bath');
  host.append(kpi);
  const avgLine=document.createElement('p');avgLine.className='chart-note';avgLine.style.margin='0 2px 18px';
  avgLine.innerHTML=`Średnio na dzień (${tot.activeDays} dni z danymi): <b>${(tot.feeds/dd).toFixed(1)}</b> karmień · <b>${Math.round(tot.milkMl/dd)}</b> ml · <b>${((tot.wet+tot.dirty)/dd).toFixed(1)}</b> pieluch`;
  host.append(avgLine);

  // --- WYKRESY ---
  host.append(histBarChart('Karmienia w czasie',B,b=>b.feeds,'feed',''));
  host.append(histStackChart('Mleko: matki vs modyfikowane',B));
  host.append(histBarChart('Pieluchy w czasie',B,b=>b.wet+b.dirty,'diaper',''));
  if(B.some(b=>b.sleepMin>0))host.append(histBarChart('Sen (godziny) w czasie',B,b=>Math.round(b.sleepMin/60*10)/10,'sleep',' h'));
  if(B.some(b=>b.pump>0))host.append(histBarChart('Odciąganie (ml) w czasie',B,b=>b.pump,'milk',' ml'));
  host.append(bathCalendarCard());

  // --- WAGA w okresie ---
  try{
    const ws=await request('/api/weight-series');
    if(ws&&ws.points&&ws.points.length){
      const pts=ws.points.slice().sort((a,b)=>a.day-b.day);
      const card=document.createElement('section');card.className='card';card.style.marginTop='16px';
      card.innerHTML=`<div class="eyebrow" style="margin-bottom:8px">Waga — pomiary</div>`;
      card.append(miniLineChart(pts.map(p=>({x:p.day,y:p.g,lbl:p.g+'g'}))));
      host.append(card);
    }
  }catch(e){}
}
function fmtDurShort(m){if(!m)return'0h';const h=Math.floor(m/60);return h>0?`${h}h`:`${m}min`}

/* Wykres słupkowy dla kubełków historii. */
function histBarChart(title,buckets,valFn,cls,unit){
  const wrap=document.createElement('section');wrap.className='card hist-chart';wrap.style.marginTop='16px';
  const vals=buckets.map(valFn);const max=Math.max(1,...vals);
  let h=`<div class="eyebrow" style="margin-bottom:12px">${title}</div><div class="hbars">`;
  buckets.forEach((b,i)=>{
    const v=vals[i];const pct=Math.round(v/max*100);
    h+=`<div class="hbar-col"><div class="hbar-v">${v||''}${v&&unit?unit:''}</div><div class="hbar-track"><div class="hbar-fill ${cls}" style="height:${v?Math.max(3,pct):0}%"></div></div><div class="hbar-lab">${b.sub}</div></div>`;
  });
  h+='</div>';wrap.innerHTML=h;return wrap;
}
/* Wykres słupkowy ze stosem matki/mieszane/modyfikowane. */
function histStackChart(title,buckets){
  const wrap=document.createElement('section');wrap.className='card hist-chart';wrap.style.marginTop='16px';
  const max=Math.max(1,...buckets.map(b=>b.milkMl));
  const anyMix=buckets.some(b=>(b.mixMl||0)>0);
  let h=`<div class="eyebrow" style="margin-bottom:6px">${title}</div><div class="chart-legend" style="margin:0 0 12px"><span><span class="dt" style="background:var(--acc)"></span>Matki</span>`+(anyMix?`<span><span class="dt" style="background:linear-gradient(135deg,#12b877,#5f95ec)"></span>Mieszane</span>`:'')+`<span><span class="dt" style="background:var(--milk)"></span>Modyf.</span></div><div class="hbars">`;
  buckets.forEach(b=>{
    const totH=Math.round(b.milkMl/max*100);
    const tot=Math.max(b.milkMl,1);
    const motherPct=Math.round(b.motherMl/tot*100);
    const mixPct=Math.round((b.mixMl||0)/tot*100);
    const modPct=Math.max(0,100-motherPct-mixPct);
    h+=`<div class="hbar-col"><div class="hbar-v">${b.milkMl||''}</div><div class="hbar-track"><div class="hbar-stack" style="height:${b.milkMl?Math.max(3,totH):0}%"><div class="hs mother" style="height:${motherPct}%"></div><div class="hs mixed" style="height:${mixPct}%"></div><div class="hs modified" style="height:${modPct}%"></div></div></div><div class="hbar-lab">${b.sub}</div></div>`;
  });
  h+='</div>';wrap.innerHTML=h;return wrap;
}
/* Prosty wykres liniowy SVG (waga). */
function miniLineChart(points){
  const W=320,H=120,pd=14;
  const xs=points.map(p=>p.x),ys=points.map(p=>p.y);
  const minX=Math.min(...xs),maxX=Math.max(...xs),minY=Math.min(...ys),maxY=Math.max(...ys);
  const sx=x=>pd+(maxX===minX?0.5:(x-minX)/(maxX-minX))*(W-2*pd);
  const sy=y=>H-pd-(maxY===minY?0.5:(y-minY)/(maxY-minY))*(H-2*pd);
  const d=points.map((p,i)=>`${i?'L':'M'}${sx(p.x).toFixed(1)} ${sy(p.y).toFixed(1)}`).join(' ');
  const svgNS='http://www.w3.org/2000/svg';
  const el=document.createElementNS(svgNS,'svg');el.setAttribute('viewBox',`0 0 ${W} ${H}`);el.setAttribute('class','weight-svg');el.style.width='100%';el.style.height='auto';
  el.innerHTML=`<path d="${d}" fill="none" stroke="var(--acc-2)" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>`+
    points.map(p=>`<circle cx="${sx(p.x).toFixed(1)}" cy="${sy(p.y).toFixed(1)}" r="3" fill="var(--acc)"/>`).join('')+
    `<text x="4" y="${sy(points[0].y)-6}" class="axis-txt">${points[0].lbl}</text>`+
    `<text x="${W-42}" y="${sy(points[points.length-1].y)-6}" class="axis-txt">${points[points.length-1].lbl}</text>`;
  return el;
}

/* ============================================================================
   Sen
   ============================================================================ */
function renderSleep(){
  const d=state.data||{};
  const btn=$('sleepToggleBtn');
  btn.textContent=d.sleepInProgress?'Obudził się':'Zasnął';
  btn.className='btn '+(d.sleepInProgress?'acorn':'plum');
  const big=$('sleepBig'),bar=$('sleepBar'),bt=$('sleepBarText'),win=$('sleepWindow');
  bar.className='sleep-bar';
  const st=d.sleepState||'brak';
  if(st==='spi'){big.textContent='Śpi'+(d.sleepSinceMin>=0?` · ${fmtDur(d.sleepSinceMin)}`:'');bar.classList.add('ok');bt.textContent='Sen w toku';win.textContent=''}
  else if(st==='czuwa'||st==='okno'||st==='przekroczone'){
    big.textContent='Czuwa'+(d.wakeSinceMin>=0?` · ${fmtDur(d.wakeSinceMin)}`:'');
    const s=isoHM(d.nextNapStartIso),e=isoHM(d.nextNapEndIso);
    if(st==='czuwa'){bar.classList.add('ok');bt.textContent=`Za wcześnie — okno drzemki ~${s}–${e}`}
    else if(st==='okno'){bar.classList.add('warn');bt.textContent=`Okno drzemki teraz (do ~${e})`}
    else{bar.classList.add('bad');bt.textContent='Przekroczone okno (ryzyko przemęczenia)'}
    win.textContent=`Okno czuwania wg wieku: ${fmtDur(d.wakeWindowMinMin)} – ${fmtDur(d.wakeWindowMaxMin)}`;
  }else{big.textContent='Brak danych';bt.textContent='Gdy dziecko zaśnie — dotknij „Zasnął”';win.textContent=''}
  const dayOk=((d.sleepNeedDayMin||0)>0&&(d.sleepDayMin||0)>=d.sleepNeedDayMin)?'ok':'';
  const nightOk=((d.sleepNeedNightMin||0)>0&&(d.sleepNightMin||0)>=d.sleepNeedNightMin)?'ok':'';
  $('sleepBalGrid').innerHTML=
    `<div class="bk">Drzemki</div><div class="bv">${d.napCount||0} (cel ~${d.napTarget||0})</div>`+
    `<div class="bk">Sen dzień</div><div class="bv ${dayOk}">${fmtDur(d.sleepDayMin)} / ${fmtDur(d.sleepNeedDayMin)}</div>`+
    `<div class="bk">Sen noc</div><div class="bv ${nightOk}">${fmtDur(d.sleepNightMin)} / ${fmtDur(d.sleepNeedNightMin)}</div>`+
    `<div class="bk">Razem</div><div class="bv">${fmtDur((d.sleepDayMin||0)+(d.sleepNightMin||0))}</div>`;
}

/* ============================================================================
   Waga — wykres SVG (pasma WHO + przyrost + pomiary)
   ============================================================================ */
function openWeight(){navTo('weight')}
async function saveWeight(){
  const grams=Number($('weightG').value)||0;const n=$('weightNotice');n.className='notice';
  if(grams<2000||grams>15000){n.className='notice error';n.textContent='Podaj wagę 2000–15000 g.';return}
  n.textContent='Zapisywanie…';
  try{const r=await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type:'WAGA',ml:String(grams)})});n.className='notice ok';n.textContent=r.message||'Zapisano.';toast('Zapisano wagę');await refresh();renderWeightChart()}
  catch(e){n.className='notice error';n.textContent=e.message}
}
/* ---------- Kąpiele: mini kalendarz (ostatnia + planowana co N dni) ---------- */
const BCAL_MONTHS=['styczeń','luty','marzec','kwiecień','maj','czerwiec','lipiec','sierpień','wrzesień','październik','listopad','grudzień'];
const BCAL_DOW=['Pn','Wt','Śr','Cz','Pt','So','Nd'];
const BCAL_DOW_SHORT=['nd','pn','wt','śr','cz','pt','sb'];
const bcalState={info:null,y:null,m:null};
function bcalParse(iso){const p=String(iso).split('-');return new Date(+p[0],+p[1]-1,+p[2])}
function bcalIso(y,m,d){return `${y}-${String(m+1).padStart(2,'0')}-${String(d).padStart(2,'0')}`}
function bcalShort(iso){const d=bcalParse(iso);return `${BCAL_DOW_SHORT[d.getDay()]} ${String(d.getDate()).padStart(2,'0')}.${String(d.getMonth()+1).padStart(2,'0')}`}
function bcalAgo(n){return n<=0?'dzisiaj':n===1?'wczoraj':`${n} dni temu`}
function bcalIn(n){return n<=0?'dzisiaj':n===1?'jutro':`za ${n} dni`}
function bcalElem(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
async function renderBathCalendar(host){
  if(!host)return;
  if(!bcalState.info)host.replaceChildren(bcalElem('p','empty','Wczytywanie…'));
  try{
    const info=await request('/api/bath-calendar');
    bcalState.info=info;
    if(bcalState.y==null){const t=bcalParse(info.today);bcalState.y=t.getFullYear();bcalState.m=t.getMonth()}
    drawBathCalendar(host);
  }catch(e){host.replaceChildren(bcalElem('p','empty','Nie udało się wczytać kalendarza kąpieli.'))}
}
function drawBathCalendar(host){
  const info=bcalState.info;if(!info)return;
  const baths=new Set(info.dates||[]);
  const late=info.next!=null&&info.daysToNext<0;
  const due=info.next?(late?info.today:info.next):null; // zaległa kąpiel = „dziś”
  const root=bcalElem('div','bcal');

  const sum=bcalElem('div','bcal-sum');
  const a=bcalElem('div','bcal-pill');
  a.append(bcalElem('span','bcal-k','OSTATNIA'),
           bcalElem('span','bcal-v',info.last?bcalShort(info.last):'brak'),
           bcalElem('span','bcal-s',info.last?bcalAgo(info.daysSinceLast):'nie zapisano jeszcze kąpieli'));
  const b=bcalElem('div','bcal-pill'+(late?' late':''));
  let nv='—',ns='po pierwszej kąpieli';
  if(info.next){
    if(late){const n=-info.daysToNext;nv='dziś';ns=`zaległa o ${n} ${n===1?'dzień':'dni'}`}
    else{nv=bcalShort(info.next);ns=bcalIn(info.daysToNext)}
  }
  b.append(bcalElem('span','bcal-k',`NASTĘPNA · CO ${info.intervalDays} DNI`),bcalElem('span','bcal-v',nv),bcalElem('span','bcal-s',ns));
  sum.append(a,b);

  const nav=bcalElem('div','bcal-nav');
  const prev=bcalElem('button','bcal-arrow','‹');prev.type='button';prev.setAttribute('aria-label','Poprzedni miesiąc');
  const next=bcalElem('button','bcal-arrow','›');next.type='button';next.setAttribute('aria-label','Następny miesiąc');
  const shift=n=>{let m=bcalState.m+n,y=bcalState.y;if(m<0){m=11;y--}else if(m>11){m=0;y++}bcalState.m=m;bcalState.y=y;drawBathCalendar(host)};
  prev.addEventListener('click',()=>shift(-1));next.addEventListener('click',()=>shift(1));
  nav.append(prev,bcalElem('div','bcal-title',`${BCAL_MONTHS[bcalState.m]} ${bcalState.y}`),next);

  const grid=bcalElem('div','bcal-grid');
  BCAL_DOW.forEach(n=>grid.append(bcalElem('div','bcal-h',n)));
  const first=new Date(bcalState.y,bcalState.m,1);
  const lead=(first.getDay()+6)%7; // poniedziałek = 0
  const days=new Date(bcalState.y,bcalState.m+1,0).getDate();
  for(let i=0;i<lead;i++)grid.append(bcalElem('div','bcal-d bcal-e'));
  for(let d=1;d<=days;d++){
    const iso=bcalIso(bcalState.y,bcalState.m,d);
    let cls='bcal-d';
    if(baths.has(iso))cls+=' bath';
    else if(iso===due)cls+=' due'+(late?' late':'');
    if(iso===info.today)cls+=' today';
    const c=bcalElem('div',cls,String(d));
    if(baths.has(iso))c.title='Kąpiel';else if(iso===due)c.title=late?'Kąpiel zaległa':'Planowana kąpiel';
    grid.append(c);
  }

  const legend=bcalElem('div','bcal-legend');
  const lg=(cls,t)=>{const s=bcalElem('span','');const i=bcalElem('i',cls);s.append(i,document.createTextNode(t));return s};
  legend.append(lg('l-bath','kąpiel'),lg('l-due','planowana'),lg('l-today','dziś'));
  root.append(sum,nav,grid,legend);
  host.replaceChildren(root);
}
function bathCalendarCard(){
  const wrap=document.createElement('section');wrap.className='card hist-chart';wrap.style.marginTop='16px';
  wrap.append(bcalElem('div','eyebrow','Kąpiele'));
  const host=bcalElem('div','');host.style.marginTop='12px';wrap.append(host);
  renderBathCalendar(host);
  return wrap;
}
/* ---------- Kąpiel (tylko data) ---------- */
function openBath(){const inp=$('bathDate');const d=new Date();inp.value=`${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`;const n=$('bathNotice');if(n){n.className='notice';n.textContent=''}show('bathModal')}
async function saveBath(){
  const val=$('bathDate').value;const n=$('bathNotice');if(n){n.className='notice'}
  if(!val||!/^\d{4}-\d{2}-\d{2}$/.test(val)){if(n){n.className='notice error';n.textContent='Wybierz datę kąpieli.'}return}
  if(n)n.textContent='Zapisywanie…';
  try{const r=await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type:'KAPIEL',when:val+'T12:00'})});if(n){n.className='notice ok';n.textContent=r.message||'Zapisano kąpiel.'}toast('Zapisano kąpiel');await refresh();setTimeout(()=>clearPanels(),600)}
  catch(e){if(n){n.className='notice error';n.textContent=e.message}}
}
async function renderWeightChart(){
  const host=$('weightChart');host.replaceChildren();
  const birthG=(state.data&&state.data.birthWeightG)||3080;
  let pts=[];try{const data=await request('/api/weight-series');pts=(data.points||[]).filter(p=>p&&p.g>0).sort((a,b)=>a.day-b.day)}catch(e){}
  const today=(state.data&&state.data.developmentDay>=0)?state.data.developmentDay:(pts.length?pts[pts.length-1].day:60);
  const maxDay=Math.max(today,pts.length?pts[pts.length-1].day:0,30);
  const W=440,H=250,padL=44,padR=14,padT=14,padB=28,plotW=W-padL-padR,plotH=H-padT-padB,step=Math.max(1,Math.round(maxDay/60));
  const bandPts=[];for(let d=0;d<=maxDay;d+=step)bandPts.push(Object.assign({d},expectedWeightBand(d,birthG)));if(bandPts[bandPts.length-1].d!==maxDay)bandPts.push(Object.assign({d:maxDay},expectedWeightBand(maxDay,birthG)));
  const gainPts=[];{const gEnd=Math.min(maxDay,GAIN_P2_END);for(let d=DISCHARGE_DAY;d<=gEnd;d+=step){const b=dischargeGainBand(d);if(b)gainPts.push({d,lo:b.lo,hi:b.hi})}if(gainPts.length&&gainPts[gainPts.length-1].d!==gEnd){const b=dischargeGainBand(gEnd);if(b)gainPts.push({d:gEnd,lo:b.lo,hi:b.hi})}}
  let maxG=0,minBand=1e9;bandPts.forEach(b=>{if(b.max>maxG)maxG=b.max;if(b.min<minBand)minBand=b.min});pts.forEach(p=>{if(p.g>maxG)maxG=p.g;if(p.g<minBand)minBand=p.g});gainPts.forEach(b=>{if(b.hi>maxG)maxG=b.hi;if(b.lo<minBand)minBand=b.lo});
  maxG=Math.ceil((maxG+300)/500)*500;const minG=Math.max(0,Math.floor((minBand-300)/500)*500);
  const x=d=>padL+(maxDay<=0?0:d/maxDay*plotW),y=g=>padT+plotH-(g-minG)/(maxG-minG)*plotH;
  const svg=svgEl('svg',{viewBox:`0 0 ${W} ${H}`,class:'weight-svg'});
  const defs=svgEl('defs',{});defs.innerHTML=`<linearGradient id="wGrad" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#5aa06e"/><stop offset="1" stop-color="#3f5e9b"/></linearGradient>`;svg.append(defs);
  for(let i=0;i<=4;i++){const gv=minG+(maxG-minG)*i/4,yy=y(gv);svg.append(svgEl('line',{class:'grid',x1:padL,y1:yy,x2:W-padR,y2:yy}));const t=svgEl('text',{class:'axis-txt',x:4,y:yy+3});t.textContent=(gv/1000).toFixed(1)+'kg';svg.append(t)}
  for(let i=0;i<=4;i++){const dv=Math.round(maxDay*i/4),xx=x(dv);const t=svgEl('text',{class:'axis-txt',x:xx-8,y:H-8});t.textContent='d'+dv;svg.append(t)}
  if(gainPts.length>1){let gB='';gainPts.forEach((b,i)=>{gB+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.hi).toFixed(1)+' '});for(let i=gainPts.length-1;i>=0;i--)gB+='L'+x(gainPts[i].d).toFixed(1)+' '+y(gainPts[i].lo).toFixed(1)+' ';gB+='Z';svg.append(svgEl('path',{class:'gain-band',d:gB.trim()}));let lo='',hi='';gainPts.forEach((b,i)=>{lo+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.lo).toFixed(1)+' ';hi+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.hi).toFixed(1)+' '});svg.append(svgEl('path',{class:'gain-edge',d:lo.trim()}));svg.append(svgEl('path',{class:'gain-edge',d:hi.trim()}))}
  let bp='';bandPts.forEach((b,i)=>{bp+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.max).toFixed(1)+' '});for(let i=bandPts.length-1;i>=0;i--)bp+='L'+x(bandPts[i].d).toFixed(1)+' '+y(bandPts[i].min).toFixed(1)+' ';bp+='Z';svg.append(svgEl('path',{class:'exp-band',d:bp.trim()}));
  let mnp='',mxp='';bandPts.forEach((b,i)=>{mnp+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.min).toFixed(1)+' ';mxp+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.max).toFixed(1)+' '});svg.append(svgEl('path',{class:'exp-edge',d:mnp.trim()}));svg.append(svgEl('path',{class:'exp-edge',d:mxp.trim()}));
  let med='';bandPts.forEach((b,i)=>{med+=(i?'L':'M')+x(b.d).toFixed(1)+' '+y(b.med).toFixed(1)+' '});svg.append(svgEl('path',{class:'exp-line',d:med.trim()}));
  if(pts.length){let ap='';pts.forEach((p,i)=>{ap+=(i?'L':'M')+x(p.day).toFixed(1)+' '+y(p.g).toFixed(1)+' '});const pl=svgEl('path',{class:'act-line',d:ap.trim()});svg.append(pl);
    // animacja rysowania
    try{const len=pl.getTotalLength?pl.getTotalLength():600;pl.style.strokeDasharray=len;pl.style.strokeDashoffset=len;pl.getBoundingClientRect();pl.style.transition='stroke-dashoffset 1s ease';pl.style.strokeDashoffset=0}catch(e){}
    pts.forEach(p=>{const c=svgEl('circle',{class:'act-dot',cx:x(p.day),cy:y(p.g),r:3.6});const ti=svgEl('title',{});ti.textContent=`dzień ${p.day}: ${p.g} g`;c.append(ti);svg.append(c)})}
  host.append(svg);
  if(pts.length){const last=pts[pts.length-1],b=expectedWeightBand(last.day,birthG),diff=last.g-b.med;let stan=last.g<b.min?'poniżej normy':last.g>b.max?'powyżej normy':'w normie';
    setText('weightNote',`Ostatni pomiar: ${last.g} g (dzień ${last.day}). Zakres WHO: ${b.min}–${b.max} g (mediana ${b.med} g, ${diff>=0?'+':''}${diff} g). Status: ${stan}.`)}
  else setText('weightNote','Brak pomiarów. Dodaj pierwszy — pojawi się na tle oczekiwanego zakresu WHO.');
}

/* Referencyjne pasmo dziennego przyrostu (g/dzień): 25-30 przez 0-3 mies.,
   15-20 przez 3-6 mies., potem 10-15. Zwraca {lo, hi} dla danego dnia życia. */
function gainBandPerDay(day){
  if(day<DISCHARGE_DAY)return null;
  if(day<GAIN_P1_END)return{lo:25,hi:30};
  if(day<GAIN_P2_END)return{lo:15,hi:20};
  return{lo:10,hi:15};
}
/* Przyrost wagi: trend TYGODNIOWY (g/dzień) wyliczony regresją liniową z ważeń
   z okna ±7 dni wokół każdego tygodnia życia. Pojedyncze ważenia mają błąd rzędu
   ±100 g (pora dnia, ubranie, karmienie), więc różnica dwóch kolejnych pomiarów
   daje "losowe piki" — trend z kilku pomiarów jest stabilny i czytelny. */
function wgSlope(pts,from,to){
  const w=pts.filter(p=>p.day>=from&&p.day<=to);
  if(w.length<2)return null;
  if(w[w.length-1].day-w[0].day<5)return null;
  const mx=w.reduce((a,p)=>a+p.day,0)/w.length,my=w.reduce((a,p)=>a+p.g,0)/w.length;
  let sxy=0,sxx=0;w.forEach(p=>{sxy+=(p.day-mx)*(p.g-my);sxx+=(p.day-mx)*(p.day-mx)});
  return sxx>0?sxy/sxx:null;
}
function wgClass(val,band){
  if(val<=0)return 'neg';
  if(!band)return 'ok';
  return val<band.lo?'low':(val>band.hi?'high':'ok');
}
async function renderGainChart(){
  const host=$('weightGainChart');if(!host)return;host.replaceChildren();
  const note=$('weightGainNote');if(note)note.replaceChildren();
  const birthG=(state.data&&state.data.birthWeightG)||3080;
  let ws=[];try{const d=await request('/api/weight-series');ws=(d.points||[]).filter(p=>p&&p.g>0).sort((a,b)=>a.day-b.day)}catch(e){}
  if(ws.length<2){const p=bcalElem('p','empty','Dodaj co najmniej dwa pomiary wagi, aby zobaczyć trend przyrostu.');host.append(p);return}
  const pts=ws.slice();if(pts[0].day>0)pts.unshift({day:0,g:birthG,date:''});
  const lastDay=pts[pts.length-1].day;
  // tygodnie życia 1..N (tydzień 0 pomijamy: adaptacja po porodzie, naturalny ubytek wagi)
  let weeks=[];
  for(let k=1;k*7<=lastDay;k++){
    const c=k*7+3.5;const v=wgSlope(pts,c-7,c+7);
    weeks.push({k,c,val:v==null?null:Math.round(v),band:gainBandPerDay(Math.round(c)),partial:(k*7+7>lastDay)});
  }
  weeks=weeks.slice(-14);
  if(!weeks.some(w=>w.val!=null)){host.append(bcalElem('p','empty','Za mało pomiarów w odstępie kilku dni, aby policzyć trend. Zważ dziecko ponownie za kilka dni.'));return}
  const vals=weeks.filter(w=>w.val!=null).map(w=>w.val);
  const bandHi=Math.max(...weeks.map(w=>w.band?w.band.hi:0));
  let yMax=Math.max(bandHi,...vals)+6;yMax=Math.ceil(yMax/10)*10;
  let yMin=Math.min(0,...vals);yMin=yMin<0?Math.floor((yMin-4)/10)*10:0;
  const W=440,H=262,padL=34,padR=10,padT=16,padB=34,plotW=W-padL-padR,plotH=H-padT-padB;
  const n=weeks.length,slot=plotW/n,bw=Math.min(30,slot*.62);
  const y=v=>padT+plotH-(v-yMin)/(yMax-yMin)*plotH;
  const svg=svgEl('svg',{viewBox:`0 0 ${W} ${H}`,class:'weight-svg wg-svg',role:'img','aria-label':'Tygodniowy przyrost wagi w gramach na dzień'});
  const stepG=(yMax-yMin)>60?20:10;
  for(let g=yMin;g<=yMax;g+=stepG){const yy=y(g);svg.append(svgEl('line',{class:g===0?'wg-zero':'grid',x1:padL,y1:yy,x2:W-padR,y2:yy}));const t=svgEl('text',{class:'axis-txt',x:padL-6,y:yy+3,'text-anchor':'end'});t.textContent=g;svg.append(t)}
  weeks.forEach((w,i)=>{
    const cx=padL+slot*(i+0.5);
    if(w.band){const r=svgEl('rect',{class:'wg-band',x:cx-slot/2+1,y:y(w.band.hi),width:slot-2,height:Math.max(2,y(w.band.lo)-y(w.band.hi)),rx:4});svg.append(r)}
    if(w.val==null){const t=svgEl('text',{class:'wg-none',x:cx,y:y(0)-5,'text-anchor':'middle'});t.textContent='–';svg.append(t)}
    else{
      const cls=wgClass(w.val,w.band),top=Math.min(y(w.val),y(0)),h=Math.max(2,Math.abs(y(w.val)-y(0)));
      const bar=svgEl('rect',{class:'wg-bar wg-'+cls+(w.partial?' wg-part':''),x:cx-bw/2,y:top,width:bw,height:h,rx:Math.min(7,bw/2.4)});
      const ti=svgEl('title',{});ti.textContent=`tydzień ${w.k}: ${w.val} g/dzień`+(w.band?` (norma ${w.band.lo}–${w.band.hi})`:'')+(w.partial?' — tydzień w toku':'');bar.append(ti);svg.append(bar);
      const lt=svgEl('text',{class:'wg-val',x:cx,y:w.val>=0?top-5:top+h+11,'text-anchor':'middle'});lt.textContent=w.val;svg.append(lt);
    }
    const xl=svgEl('text',{class:'axis-txt',x:cx,y:H-16,'text-anchor':'middle'});xl.textContent='t'+w.k;svg.append(xl);
  });
  const cap=svgEl('text',{class:'axis-txt',x:padL,y:H-3});cap.textContent='tydzień życia';svg.append(cap);
  host.append(svg);
  const lg=bcalElem('div','wg-legend');
  const it=(cls,t)=>{const s=bcalElem('span','');s.append(bcalElem('i',cls),document.createTextNode(t));return s};
  lg.append(it('l-ok','w normie'),it('l-low','poniżej normy'),it('l-high','powyżej normy'),it('l-band','zakres normy'));
  host.append(lg);

  // kafelki podsumowania
  const last=pts[pts.length-1],prev=pts.length>1?pts[pts.length-2]:null;
  const sinceBirth=lastDay>0?Math.round((last.g-birthG)/lastDay):null;
  const s14=wgSlope(pts,lastDay-14,lastDay);const r14=s14==null?null:Math.round(s14);
  const bandNow=gainBandPerDay(lastDay);
  const tile=(k,v,sub,cls)=>{const d=bcalElem('div','wg-stat'+(cls?' wg-'+cls:''));d.append(bcalElem('div','wg-k',k),bcalElem('div','wg-v',v),bcalElem('div','wg-s',sub));return d};
  const grid=bcalElem('div','wg-stats');
  grid.append(
    tile('Od urodzenia',sinceBirth==null?'—':`${sinceBirth} g/dzień`,`+${last.g-birthG} g w ${lastDay} dni`,''),
    tile('Ostatnie 2 tygodnie',r14==null?'—':`${r14} g/dzień`,bandNow?`norma ${bandNow.lo}–${bandNow.hi} g`:'trend z ważeń',r14==null?'':wgClass(r14,bandNow)),
    tile('Ostatnie ważenie',`${last.g} g`,prev&&prev.day<last.day?`${last.g-prev.g>=0?'+':''}${last.g-prev.g} g po ${last.day-prev.day} ${last.day-prev.day===1?'dniu':'dniach'}`:'',''));
  if(note){note.append(grid,bcalElem('p','chart-note wg-foot','Pojedyncze ważenia różnią się nawet o 100 g (pora dnia, ubranie), dlatego przyrost pokazujemy jako trend z kilku pomiarów, a nie różnicę dwóch kolejnych.'))}
}

/* ============================================================================
   Formularz karmienia / odciągania
   ============================================================================ */
/* Rodzaj mleka jako 2 niezalezne checkboxy: mozna zaznaczyc oba (=> Mieszane). */
function toggleMilkKind(which){
  if(which==='mother')state.milkMother=!state.milkMother;
  else state.milkModified=!state.milkModified;
  refreshMilkKindButtons();
}
function refreshMilkKindButtons(){
  const bm=$('milkMother'),bx=$('milkModified');
  if(bm){bm.classList.toggle('sel',!!state.milkMother);bm.setAttribute('aria-pressed',state.milkMother?'true':'false')}
  if(bx){bx.classList.toggle('sel',!!state.milkModified);bx.setAttribute('aria-pressed',state.milkModified?'true':'false')}
  refreshMilkAmountUi();
}
/* Przelacza tryb wyboru ilosci:
   - oba rodzaje zaznaczone (mieszane) => dwa male pola (matki/modyfikowane) + popover,
   - jeden rodzaj => pojedynczy suwak (#mlField),
   - brak rodzaju => oba pola ukryte. */
function refreshMilkAmountUi(){
  const both=state.milkMother&&state.milkModified;
  const one=(state.milkMother||state.milkModified)&&!both;
  const mlField=$('mlField'),dual=$('dualMlField');
  if(mlField)mlField.classList.toggle('hidden',!one);
  if(dual)dual.classList.toggle('hidden',!both);
  if(both)syncDualLabels();
}
/* Aktualizuje etykiety dwoch pol + sume dla trybu mieszanego. */
function syncDualLabels(){
  setText('mlMotherVal',`${state.milkMotherMl} ml`);
  setText('mlModifiedVal',`${state.milkModifiedMl} ml`);
  setText('dualMlSum',`Razem ${(+state.milkMotherMl||0)+(+state.milkModifiedMl||0)} ml`);
}
function updateBottle(){const o=state.bottleOpen;$('extraMilkOptions').classList.toggle('hidden',!o);$('bottleToggle').textContent=o?'− Ukryj butelkę':'＋ Dodaj butelkę';if(o)refreshMilkAmountUi()}
function openForm(date){
  state.pumpMode=false;state.editFeedLine=null;state.editMilkHad=false;
  $('milkRemoveBtn').classList.add('hidden');
  ['timeField','nudgeBox','quickNotice','bottleToggle','nursingBox'].forEach(id=>$(id).classList.remove('hidden'));
  setText('extraTitle','Rodzaj mleka (możesz zaznaczyć oba → Mieszane)');$('kindField').classList.remove('hidden');$('mlField').querySelector('label').innerHTML='Ilość mleka';
  setText('formTitle',date?`Karmienie · ${dateLabel(date)}`:'Nowe karmienie');
  $('entryTime').value=date?`${date}T12:00`:dateTimeInput(state.data&&state.data.nowIso);
  const d=state.data||{};const mn=d.milkMinMl||20,mx=d.milkMaxMl||200,st=d.milkStepMl||10,dv=d.milkDefaultMl||60;
  $('milkMl').min=mn;$('milkMl').max=mx;$('milkMl').step=st;$('milkMl').value=dv;
  $('piersL').value=0;$('piersR').value=0;state.bottleOpen=false;
  state.milkMother=true;state.milkModified=false;
  state.milkMotherMl=dv;state.milkModifiedMl=dv;
  refreshMilkKindButtons();updateBottle();
  setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal();
}
/* Edycja istniejacego karmienia: prefill czasem i mlekiem sparowanym (ten sam czas).
   feed = wpis KARMIENIE (z lineIndex), milks = TABLICA sparowanych wierszy MLEKO_* (0..2).
   Mleko mieszane = dwa wiersze (MLEKO_MATKI + MLEKO_MODYFIKOWANE); stare MLEKO_MIESZANE
   (jeden wiersz) mapujemy na oba rodzaje z ta sama iloscia (podpowiedz do rozbicia). */
function openEditFeeding(feed, milks){
  milks=Array.isArray(milks)?milks:(milks?[milks]:[]);
  state.pumpMode=false;state.editFeedLine=feed.lineIndex;state.editMilkHad=milks.length>0;
  // Edytujemy mleko + godzine. Minuty piersi zostaja bez zmian — ukrywamy to pole.
  ['timeField','nudgeBox','bottleToggle'].forEach(id=>$(id).classList.remove('hidden'));
  $('nursingBox').classList.add('hidden');$('quickNotice').classList.add('hidden');
  setText('formTitle','Edytuj karmienie');
  setText('extraTitle','Rodzaj mleka (możesz zaznaczyć oba → Mieszane)');
  $('kindField').classList.remove('hidden');$('mlField').querySelector('label').innerHTML='Ilość mleka';
  // czas z wpisu (dzien z activeDay)
  const day=state.activeDay||isoDaysAgo(0);
  $('entryTime').value=`${day}T${(feed.time||'12:00')}`;
  const d=state.data||{};const mn=d.milkMinMl||20,mx=d.milkMaxMl||200,st=d.milkStepMl||10,dv=d.milkDefaultMl||60;
  $('milkMl').min=mn;$('milkMl').max=mx;$('milkMl').step=st;
  // Rozbij wiersze mleka na dwie ilosci.
  let motherMl=0,modifiedMl=0,mixedMl=0;
  milks.forEach(m=>{const t=m.type;const v=+m.ml||0;
    if(t==='MLEKO_MATKI')motherMl+=v;else if(t==='MLEKO_MODYFIKOWANE')modifiedMl+=v;else if(t==='MLEKO_MIESZANE')mixedMl+=v;else motherMl+=v;});
  if(mixedMl>0){ // stary jednowierszowy zapis: pokaz jako oba rodzaje z ta sama iloscia
    if(motherMl===0)motherMl=mixedMl; if(modifiedMl===0)modifiedMl=mixedMl;
  }
  state.milkMother=motherMl>0;state.milkModified=modifiedMl>0;
  if(!milks.length){state.milkMother=true;state.milkModified=false}
  state.milkMotherMl=motherMl>0?motherMl:dv;
  state.milkModifiedMl=modifiedMl>0?modifiedMl:dv;
  // Suwak pojedynczego rodzaju pokazuje ilosc aktywnego rodzaju.
  $('milkMl').value=state.milkModified&&!state.milkMother?state.milkModifiedMl:state.milkMotherMl;
  // Butelka rozwinieta gdy mleko istnieje; przycisk "Usun mleko" tylko gdy mleko bylo
  state.bottleOpen=milks.length>0;refreshMilkKindButtons();updateBottle();
  $('milkRemoveBtn').classList.toggle('hidden',!milks.length);
  setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal();
}
function nudge(min){const i=$('entryTime');const d=new Date(i.value);if(Number.isNaN(d.getTime()))return;d.setMinutes(d.getMinutes()+min);i.value=dateTimeInput(d)}

/* ============================================================================
   API akcje
   ============================================================================ */
async function refresh(){
  try{render(await request('/api/status'));}
  catch(e){toast(e.message,'err')}
}
async function postEvent(type,ml=0){
  try{await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type,ml:String(ml)})});await refresh();return true}
  catch(e){toast(e.message,'err');return false}
}

/* ---------- Wspomnienia (v4): raport dnia + kalendarz ---------- */
const memFmtMin=m=>{if(!m&&m!==0)return '-';const h=Math.floor(m/60),mm=m%60;return h?`${h}h ${mm}min`:`${mm}min`};
function memAgg(es){
  const s={feedingCount:0,milkMl:0,motherMilkMl:0,modifiedMilkMl:0,mixedMilkMl:0,piersLeftMin:0,piersRightMin:0,vitaminD:false,weightG:0,bathCount:0,sleepDayMin:0,sleepNightMin:0,napCount:0};
  const spans=[];let open=null;
  const toMin=tm=>{const p=tm.split(':');return (+p[0])*60+(+p[1])};
  (es||[]).forEach(e=>{const t=e.type||'';
    if(t==='KARMIENIE'){s.feedingCount++;s.piersLeftMin+=e.piersLeftMin||0;s.piersRightMin+=e.piersRightMin||0}
    else if(t.startsWith('MLEKO')){s.milkMl+=e.ml||0;if(t==='MLEKO_MATKI')s.motherMilkMl+=e.ml||0;else if(t==='MLEKO_MODYFIKOWANE')s.modifiedMilkMl+=e.ml||0;else if(t==='MLEKO_MIESZANE')s.mixedMilkMl+=e.ml||0}
    else if(t==='WITAMINA_D')s.vitaminD=true;
    else if(t==='WAGA')s.weightG=e.ml||0;
    else if(t==='KAPIEL')s.bathCount++;
    else if(t==='SEN_START')open=e.time;
    else if(t==='SEN_STOP'&&open){let d=toMin(e.time)-toMin(open);if(d<0)d+=1440;spans.push({from:open,to:e.time,min:d});open=null}
  });
  spans.forEach(sp=>{const h=+sp.from.slice(0,2);if(h>=21||h<7)s.sleepNightMin+=sp.min;else s.sleepDayMin+=sp.min});
  s.napCount=spans.length;
  return {summary:s,sleepSpans:spans,entries:(es||[]).slice().sort((a,b)=>(a.time||'').localeCompare(b.time||''))};
}
async function openMemory(){
  show('memoryModal');
  const datesHost=$('memoryDates'),rep=$('memoryReport');
  datesHost.replaceChildren();rep.replaceChildren();
  const cal=(state.data&&state.data.calendar)||[];
  const dates=cal.map(c=>c.date).filter(Boolean);
  dates.forEach(ds=>{
    const b=document.createElement('button');b.type='button';b.className='mem-chip';b.dataset.date=ds;b.textContent=dateLabel(ds);
    b.addEventListener('click',()=>renderMemoryReport(ds));
    datesHost.append(b);
  });
  const inp=document.createElement('input');inp.type='date';inp.className='mem-date-in';inp.setAttribute('aria-label','Wybierz dzień');
  inp.addEventListener('change',()=>{if(inp.value)renderMemoryReport(inp.value)});
  datesHost.append(inp);
  renderMemoryReport(dates.length?dates[0]:(state.data&&state.data.nowIso?state.data.nowIso.slice(0,10):''));
}

function memShowPreview(date,r){
  document.querySelectorAll('#memoryDates .mem-chip').forEach(c=>c.classList.toggle('on',c.dataset.date===date));
  const img=$('pcPreview'),sk=$('pcSkel');if(!img)return;
  try{img.src=memPostcard(date,r);img.hidden=false;if(sk)sk.hidden=true}catch(e){img.hidden=true;if(sk){sk.hidden=false;sk.textContent='Nie udało się wygenerować podglądu.'}}
}
async function renderMemoryReport(date){
  const rep=$('memoryReport');if(!rep)return;rep.replaceChildren();
  const wait=document.createElement('p');wait.className='hint';wait.textContent='Ładowanie...';rep.append(wait);
  let r;try{r=memAgg((await request(`/api/entries?date=${encodeURIComponent(date)}`).catch(()=>({entries:[]}))).entries||[]);const ti=(state.data&&state.data.nowIso||'').slice(0,10);r.dayOfLife=ti?((state.data&&state.data.developmentDay||0)-Math.round((new Date(ti)-new Date(date))/86400000)):null}catch(e){rep.replaceChildren();const p=document.createElement('p');p.className='hint';p.textContent=e.message;rep.append(p);return}
  rep.replaceChildren();
  const s=r.summary||{};
  state.memDate=date;state.memReport=r;memShowPreview(date,r);
  const head=document.createElement('div');head.style.cssText='display:flex;justify-content:space-between;align-items:baseline;margin:2px 0 14px';
  const hd=document.createElement('h2');hd.style.cssText='margin:0;font-size:1.35rem;letter-spacing:-.02em';hd.textContent=dateLabel(date);
  const hd2=document.createElement('span');hd2.style.cssText='font-size:.78rem;color:var(--muted);font-weight:800';hd2.textContent='dzień życia '+(r.dayOfLife!=null?r.dayOfLife:'-');
  head.append(hd,hd2);rep.append(head);
  const pill=(k,v,c)=>{const p=document.createElement('div');p.style.cssText='min-width:0;border-radius:16px;padding:10px 6px;text-align:center;box-shadow:var(--sh-1)';p.style.background='color-mix(in srgb,'+c+' 16%,transparent)';p.style.border='1px solid '+c;p.innerHTML='<div style="font-size:1.25rem;font-weight:800;line-height:1.1;color:'+c+'">'+v+'</div><div style="font-size:.62rem;color:var(--muted);font-weight:700;margin-top:3px">'+k+'</div>';return p};
  const wrap=document.createElement('div');wrap.style.cssText='display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;margin-bottom:14px';
  wrap.append(pill('Karmienia',s.feedingCount||0,'var(--feed)'));
  wrap.append(pill('Mleko',(s.milkMl||0)+' ml','var(--milk)'));
  wrap.append(pill('Sen',memFmtMin((s.sleepDayMin||0)+(s.sleepNightMin||0)),'var(--sleep)'));
  wrap.append(pill('Drzemki',s.napCount||0,'var(--sleep)'));
  wrap.append(pill('Kąpiel',s.bathCount?'✓':'–','var(--bath)'));
  wrap.append(pill('Waga',s.weightG?s.weightG+' g':'–','var(--acc)'));
  rep.append(wrap);
  if((r.sleepSpans||[]).length){
    const toMin=t=>{const p=t.split(':');return (+p[0])*60+(+p[1])};
    const sl=document.createElement('div');sl.style.cssText='border-radius:18px;padding:12px 14px;background:var(--card);border:1px solid var(--brd);margin-bottom:14px';
    const slt=document.createElement('div');slt.className='eyebrow';slt.textContent='Sen';sl.append(slt);
    const bar=document.createElement('div');bar.style.cssText='position:relative;height:18px;border-radius:9px;background:var(--card-2);overflow:hidden;margin:8px 6px';
    (r.sleepSpans||[]).forEach(sp=>{const st=toMin(sp.from),en=toMin(sp.to);const seg=document.createElement('div');seg.style.cssText='position:absolute;top:0;bottom:0;background:var(--sleep);border-radius:5px;opacity:.85;left:'+(st/1440*100).toFixed(2)+'%;width:'+Math.max(1,(en-st)/1440*100).toFixed(2)+'%';seg.title=sp.from+'–'+sp.to+' ('+memFmtMin(sp.min)+')';bar.append(seg)});
    sl.append(bar);
    const hh=document.createElement('div');hh.style.cssText='display:flex;justify-content:space-between;font-size:.6rem;color:var(--muted);margin-top:2px;padding:0 6px';['0','6','12','18','24'].forEach(h=>{const s2=document.createElement('span');s2.textContent=h;hh.append(s2)});sl.append(hh);
    rep.append(sl);
  }
  const byTime={};
  (r.entries||[]).forEach(e=>{const t=e.type||'';if(t==='SEN_START'||t==='SEN_STOP')return;(byTime[e.time]=byTime[e.time]||[]).push(e)});
  const times=Object.keys(byTime).sort((a,b)=>a.localeCompare(b));
  if(times.length){
    const tl=document.createElement('div');tl.style.cssText='display:flex;flex-direction:column;gap:8px';
    times.forEach(t=>{
      const group=byTime[t];
      const feed=group.find(e=>e.type==='KARMIENIE');
      const milks=group.filter(e=>(e.type||'').startsWith('MLEKO'));
      const others=group.filter(e=>e.type!=='KARMIENIE'&&!(e.type||'').startsWith('MLEKO'));
      const row=document.createElement('div');row.style.cssText='display:flex;align-items:flex-start;gap:12px;padding:12px 12px;border-radius:16px;background:var(--card);border:1px solid var(--brd);box-shadow:var(--sh-1)';
      const node=document.createElement('div');node.style.cssText='width:12px;height:12px;border-radius:50%;flex-shrink:0;margin-top:'+(feed?'7px':'5px')+';background:'+(feed?'var(--feed)':'var(--muted)');
      const body=document.createElement('div');body.style.cssText='flex:1;min-width:0';
      const tm=document.createElement('div');tm.style.cssText='font-size:.74rem;color:var(--muted);font-weight:700';tm.textContent=t;body.append(tm);
      if(feed){const f=document.createElement('div');f.style.cssText='font-size:1.12rem;font-weight:800;color:var(--feed);letter-spacing:.01em';f.textContent='Karmienie';body.append(f)}
      milks.forEach(m=>{
        const mk=document.createElement('div');mk.style.cssText='display:flex;align-items:center;gap:8px;font-size:.84rem;color:var(--milk);font-weight:700;margin-top:4px';
        const branch=document.createElement('span');branch.style.cssText='width:10px;height:10px;border-radius:3px;background:var(--milk);flex-shrink:0';
        const label=(m.type==='MLEKO_MATKI')?'mleko matki':(m.type==='MLEKO_MODYFIKOWANE')?'mleko modyfikowane':(m.label||m.type);
        mk.append(branch);const tx=document.createElement('span');tx.textContent=label+' · '+(m.ml||0)+' ml';mk.append(tx);body.append(mk);
      });
      others.forEach(o=>{
        const ov=document.createElement('div');ov.style.cssText='font-size:.9rem;font-weight:700;color:var(--ink);margin-top:4px';
        ov.textContent=(o.label||o.type)+(o.ml?(' · '+(o.ml||0)+' ml'):'');body.append(ov);
      });
      row.append(node,body);tl.append(row);
    });
    rep.append(tl);
  } else {
    const em=document.createElement('p');em.className='hint';em.textContent='Brak wpisów w tym dniu.';rep.append(em);
  }
}


function memRoundRect(g,x,y,w,h,r){g.beginPath();g.moveTo(x+r,y);g.lineTo(x+w-r,y);g.arcTo(x+w,y,x+w,y+r,r);g.lineTo(x+w,y+h-r);g.arcTo(x+w,y+h,x+w-r,y+h,r);g.lineTo(x+r,y+h);g.arcTo(x,y+h,x,y+h-r,r);g.lineTo(x,y+r);g.arcTo(x,y,x+r,y,r);g.closePath();g.fill()}
/* Pocztówka 1080×1350: bajkowy leśny wieczór — księżyc, gwiazdy, wzgórza i sosny,
   papierowa karta z podsumowaniem dnia i znaczkiem pocztowym. */
function memPostcard(date,r){
  const W=1080,H=1400;
  const cv=document.createElement('canvas');cv.width=W;cv.height=H;
  const g=cv.getContext('2d');
  const SANS='"Nunito","Trebuchet MS","Segoe UI",Arial,sans-serif',SERIF='Georgia,"Times New Roman",serif';
  const s=r.summary||{};
  const fmtMin=m=>{if(!m&&m!==0)return '–';const h=Math.floor(m/60),mm=m%60;return h?h+' h '+mm+' min':mm+' min'};
  const sleepMin=(s.sleepDayMin||0)+(s.sleepNightMin||0);
  const toMin=t=>{const p=(t||'0:0').split(':');return(+p[0])*60+(+p[1])};
  const txt=(t,x,y,font,col,al)=>{g.font=font;g.fillStyle=col;g.textAlign=al||'center';g.textBaseline='middle';g.fillText(t,x,y)};
  // tło: niebo o zmierzchu
  const sky=g.createLinearGradient(0,0,0,H);sky.addColorStop(0,'#2d3f6b');sky.addColorStop(.38,'#6f7fb4');sky.addColorStop(.62,'#f3cfb8');sky.addColorStop(1,'#fbe9d0');
  g.fillStyle=sky;g.fillRect(0,0,W,H);
  // gwiazdy (deterministyczne)
  let seed=7;const rnd=()=>{seed=(seed*16807)%2147483647;return seed/2147483647};
  for(let i=0;i<46;i++){const x=rnd()*W,y=rnd()*430,rr=1.4+rnd()*3;g.fillStyle='rgba(255,247,220,'+(.35+rnd()*.5).toFixed(2)+')';g.beginPath();g.arc(x,y,rr,0,7);g.fill()}
  const star=(cx,cy,R,col)=>{g.fillStyle=col;g.beginPath();for(let i=0;i<10;i++){const a=-Math.PI/2+i*Math.PI/5,rr=i%2?R*.45:R;g.lineTo(cx+Math.cos(a)*rr,cy+Math.sin(a)*rr)}g.closePath();g.fill()};
  star(170,150,26,'#ffe9a8');star(905,250,20,'#ffe9a8');star(330,60,14,'#fff3cf');star(760,90,16,'#fff3cf');
  // księżyc z poświatą
  const halo=g.createRadialGradient(850,150,20,850,150,200);halo.addColorStop(0,'rgba(255,240,200,.55)');halo.addColorStop(1,'rgba(255,240,200,0)');g.fillStyle=halo;g.fillRect(600,0,480,360);
  g.save();g.beginPath();g.rect(700,0,300,300);g.arc(884,132,66,0,7);g.clip('evenodd');g.fillStyle='#fff1c9';g.beginPath();g.arc(850,150,74,0,7);g.fill();g.restore();
  // chmurki
  const cloud=(x,y,sc,a)=>{g.fillStyle='rgba(255,255,255,'+a+')';[[0,0,46],[52,-14,58],[112,0,44],[56,16,50]].forEach(c=>{g.beginPath();g.arc(x+c[0]*sc,y+c[1]*sc,c[2]*sc,0,7);g.fill()})};
  cloud(120,330,.8,.35);cloud(640,400,.6,.3);
  // wzgórza
  const hill=(y,amp,col,ph)=>{g.fillStyle=col;g.beginPath();g.moveTo(0,H);for(let x=0;x<=W;x+=20)g.lineTo(x,y+Math.sin(x/W*Math.PI*2+ph)*amp);g.lineTo(W,H);g.closePath();g.fill()};
  hill(560,26,'#8fa7a0',0);hill(600,30,'#6f9482',2);
  const pine=(x,y,h,col)=>{g.fillStyle=col;for(let i=0;i<3;i++){const t=y-h+i*h*.28,w=h*(.22+i*.1);g.beginPath();g.moveTo(x,t);g.lineTo(x-w,t+h*.38);g.lineTo(x+w,t+h*.38);g.closePath();g.fill()}g.fillRect(x-h*.04,y-h*.1,h*.08,h*.12)};
  [[70,640,150],[150,655,110],[950,640,160],[1020,660,115],[880,668,100]].forEach(p=>pine(p[0],p[1],p[2],'#2f5a45'));
  hill(660,24,'#4d7a5f',4);
  // papierowa karta
  const cx=70,cy=520,cw=W-140,ch=800;
  g.save();g.shadowColor='rgba(40,40,80,.28)';g.shadowBlur=40;g.shadowOffsetY=16;g.fillStyle='#fffaf0';memRoundRect(g,cx,cy,cw,ch,44);g.restore();
  g.strokeStyle='#e8c9a0';g.lineWidth=3;g.setLineDash([3,12]);g.lineCap='round';g.beginPath();g.moveTo(cx+30,cy+30);g.lineTo(cx+cw-30,cy+30);g.lineTo(cx+cw-30,cy+ch-30);g.lineTo(cx+30,cy+ch-30);g.closePath();g.stroke();g.setLineDash([]);
  // znaczek pocztowy
  g.save();g.translate(cx+cw-140,cy-30);g.rotate(.07);g.fillStyle='#fffdf6';g.shadowColor='rgba(0,0,0,.2)';g.shadowBlur=14;g.shadowOffsetY=6;memRoundRect(g,0,0,150,176,10);g.restore();
  g.save();g.translate(cx+cw-140,cy-30);g.rotate(.07);g.fillStyle='#bcd9c4';memRoundRect(g,14,14,122,148,6);g.fillStyle='#2f5a45';g.beginPath();g.moveTo(75,40);g.lineTo(38,110);g.lineTo(112,110);g.closePath();g.fill();g.fillStyle='#ffe9a8';g.beginPath();g.arc(105,50,14,0,7);g.fill();
  g.font='800 20px '+SANS;g.fillStyle='#2f5a45';g.textAlign='center';g.fillText('Aleks',75,138);g.restore();
  // stempel
  g.save();g.translate(cx+cw-250,cy+60);g.rotate(-.2);g.strokeStyle='rgba(180,90,70,.55)';g.lineWidth=4;g.beginPath();g.arc(0,0,48,0,7);g.stroke();g.beginPath();g.arc(0,0,38,0,7);g.stroke();g.font='800 17px '+SANS;g.fillStyle='rgba(180,90,70,.7)';g.textAlign='center';g.fillText(dateLabel(date).slice(0,5),0,6);g.restore();
  // nagłówek karty
  txt('Wspomnienie z dnia',cx+70,cy+92,'italic 700 34px '+SERIF,'#9a7b5a','left');
  txt(dateLabel(date),cx+70,cy+150,'900 76px '+SANS,'#2d3f6b','left');
  txt('Aleksander · dzień życia '+(r.dayOfLife!=null?r.dayOfLife:'–'),cx+70,cy+208,'700 32px '+SANS,'#6f7fb4','left');
  // kafelki z ikonami
  const tiles=[
    {k:'karmienia',v:String(s.feedingCount||0),c:'#e0742e',ic:'drop'},
    {k:'mleko',v:(s.milkMl||0)+' ml',c:'#3f77d6',ic:'bottle'},
    {k:'sen',v:fmtMin(sleepMin),c:'#7b58d4',ic:'moon'},
    {k:'kąpiel',v:s.bathCount?'tak':'–',c:'#14a89a',ic:'bubble'},
    {k:'waga',v:s.weightG?s.weightG+' g':'–',c:'#2f8a5f',ic:'heart'},
    {k:'drzemki',v:String(s.napCount||0),c:'#9a7ce8',ic:'cloud'}];
  const tw=(cw-140-2*22)/3,th=138,tx0=cx+70,ty0=cy+250;
  const icon=(t,x,y,col)=>{g.fillStyle=col;g.strokeStyle=col;g.lineWidth=4;
    if(t==='drop'){g.beginPath();g.moveTo(x,y-18);g.bezierCurveTo(x+22,y+4,x+18,y+20,x,y+20);g.bezierCurveTo(x-18,y+20,x-22,y+4,x,y-18);g.fill()}
    else if(t==='bottle'){memRoundRect(g,x-11,y-8,22,30,7);memRoundRect(g,x-6,y-18,12,12,3)}
    else if(t==='moon'){g.beginPath();g.arc(x,y,19,0,7);g.fill();g.fillStyle='#fffaf0';g.beginPath();g.arc(x+9,y-6,16,0,7);g.fill()}
    else if(t==='bubble'){g.beginPath();g.arc(x-6,y+4,13,0,7);g.stroke();g.beginPath();g.arc(x+12,y-10,8,0,7);g.stroke()}
    else if(t==='heart'){g.beginPath();g.moveTo(x,y+18);g.bezierCurveTo(x-30,y-2,x-14,y-24,x,y-8);g.bezierCurveTo(x+14,y-24,x+30,y-2,x,y+18);g.fill()}
    else{g.beginPath();g.arc(x-9,y+4,11,0,7);g.arc(x+4,y-4,14,0,7);g.arc(x+17,y+6,10,0,7);g.fill()}};
  tiles.forEach((t,i)=>{const x=tx0+(i%3)*(tw+22),y=ty0+Math.floor(i/3)*(th+20);
    g.fillStyle=t.c+'22';memRoundRect(g,x,y,tw,th,28);
    icon(t.ic,x+tw/2,y+36,t.c);
    txt(t.v,x+tw/2,y+84,'900 '+(t.v.length>7?'34':'42')+'px '+SANS,t.c);
    txt(t.k,x+tw/2,y+119,'700 24px '+SANS,'#8a7b68')});
  // oś dnia
  const ay=ty0+2*th+20+56;
  txt('przebieg dnia',cx+70,ay,'italic 700 28px '+SERIF,'#9a7b5a','left');
  const rx=cx+70,rw=cw-140,ry=ay+44;
  g.fillStyle='#efe3cf';memRoundRect(g,rx,ry-9,rw,18,9);
  (r.sleepSpans||[]).forEach(sp=>{const a=toMin(sp.from),b=toMin(sp.to);g.fillStyle='rgba(123,88,212,.45)';memRoundRect(g,rx+a/1440*rw,ry-9,Math.max(8,(b-a)/1440*rw),18,9)});
  const dot=(m,rr,col)=>{g.fillStyle='#fffaf0';g.beginPath();g.arc(rx+m/1440*rw,ry,rr+3,0,7);g.fill();g.fillStyle=col;g.beginPath();g.arc(rx+m/1440*rw,ry,rr,0,7);g.fill()};
  (r.entries||[]).forEach(e=>{const t=e.type||'';if(t==='KARMIENIE')dot(toMin(e.time),11,'#e0742e');else if(t.startsWith('MLEKO'))dot(toMin(e.time),8,'#3f77d6');else if(t==='KAPIEL')dot(toMin(e.time),9,'#14a89a');else if(t==='WAGA')dot(toMin(e.time),9,'#2f8a5f')});
  ['0','6','12','18','24'].forEach((h,i)=>txt(h,rx+i/4*rw,ry+34,'700 20px '+SANS,'#b3a48e'));
  // podpis
  let hit;
  {const c=[];const f=s.feedingCount||0,mk=s.milkMl||0;
   if(s.bathCount)c.push('Dzień pełen bąbelków','Kąpiel i czysta główka');
   if(s.weightG)c.push('Ważenie: '+s.weightG+' g','Dzień ważenia i mierzenia');
   if(sleepMin>=840)c.push('Śpioch nad śpiochy','Dzień pełen snów');else if(sleepMin>=600)c.push('Dużo snu, dużo wzrostu');else if(sleepMin>0&&sleepMin<300)c.push('Dziś bardziej na czuwaniu');
   if(f>=10)c.push('Mały głodomór, wielki apetyt');else if(f>=7)c.push('Dzielny, głodny maluch');else if(f>0&&f<=4)c.push('Spokojny, syty dzień');
   if(mk>=300)c.push('Butelka na medal');else if(mk>=150)c.push('Porządna porcja mleka');
   if((s.napCount||0)>=4)c.push('Drzemka za drzemką');
   if(!c.length)c.push('Kolejny dobry dzień');
   const hsh=String(date).split('').reduce((a,ch)=>(a*31+ch.charCodeAt(0))>>>0,7);hit=c[hsh%c.length]}
  txt(hit,W/2,cy+ch-70,'italic 700 38px '+SERIF,'#2f5a45');
  txt('Leśny Dziennik Aleksandra',W/2,H-42,'700 24px '+SANS,'rgba(255,248,230,.85)');
  return cv.toDataURL('image/png');
}

async function downloadPostcard(){
  const date=state.memDate;if(!date)return;
  let r=state.memReport;
  if(!r){try{const ed=await request(`/api/entries?date=${encodeURIComponent(date)}`);r=memAgg((ed&&ed.entries)||[]);const ti=(state.data&&state.data.nowIso||'').slice(0,10);r.dayOfLife=ti?((state.data&&state.data.developmentDay||0)-Math.round((new Date(ti)-new Date(date))/86400000)):null}catch(e){toast(e.message,'err');return}}
  try{
    const url=memPostcard(date,r);
    const a=document.createElement('a');a.href=url;a.download='wspomnienie-'+date+'.png';document.body.append(a);a.click();a.remove();
    toast('Pocztówka pobrana (PNG).');
  }catch(e){toast('Błąd generowania pocztówki: '+e.message,'err')}
}

/* ---------- Delegacja kliknięć ---------- */
document.addEventListener('click',async ev=>{
  // Nawigacja podstron (dolny pasek)
  const navEl=ev.target.closest('[data-nav]');
  if(navEl){navTo(navEl.dataset.nav);return}
  // Przełącznik widoków w Statystykach
  const statEl=ev.target.closest('[data-stat]');
  if(statEl){renderStatView(statEl.dataset.stat);return}
  // Przełącznik okresu w Historii
  const histEl=ev.target.closest('[data-hist]');
  if(histEl){state.histPeriod=histEl.dataset.hist;document.querySelectorAll('#histTabs .tab').forEach(t=>t.classList.toggle('active',t.dataset.hist===histEl.dataset.hist));renderHistory(state.histPeriod);return}
  const el=ev.target.closest('[data-action]');if(!el)return;
  const a=el.dataset.action;
  switch(a){
    case 'new-feed':openForm();break;
    case 'sleep-open':navTo('sleep');break;
    case 'sleep':{const t=(state.data&&state.data.sleepInProgress)?'SEN_STOP':'SEN_START';if(await postEvent(t)){toast(t==='SEN_START'?'Zaznaczono zaśnięcie':'Zaznaczono pobudkę');renderSleep()}break;}
    case 'weight':navTo('weight');break;
    case 'calendar':navTo('diary');break;
    case 'today-detail':{navTo('diary');const c=(state.data&&state.data.calendar&&state.data.calendar[0]);openDay(c?c.date:isoDaysAgo(0),c?c.label:null);break;}
    case 'chart':navTo('stats');break;
    case 'chart5':navTo('stats');break;
    case 'home':clearPanels();break;
    case 'back-calendar':{const dd=$('diaryDetail');if(dd)dd.classList.add('hidden');state.activeDay=null;break;}
    case 'day-feed':if(state.activeDay)openForm(state.activeDay);break;
    case 'milk-remove':{state.bottleOpen=false;state.milkMother=false;state.milkModified=false;closeMlPop();refreshMilkKindButtons();updateBottle();$('milkRemoveBtn').classList.add('hidden');setText('formNotice','Mleko zostanie usunięte po zapisaniu.');break;}
    case 'cancel-form':{const rd=state.activeDay;closeFormModal();rd?openDay(rd,state.detailLabel):clearPanels();break;}
    case 'minus5':nudge(-5);break;
    case 'plus5':nudge(5);break;
    case 'vitamin':{const ok=await postEvent('WITAMINA_D');if(ok){toast('Zapisano witaminę D');renderSummary()}break;}
    case 'import':$('importFile').click();break;
    case 'upload-data':$('uploadDataFile').click();break;
    case 'toggle-diag':{const c=$('diagCard');c.classList.toggle('hidden');if(!c.classList.contains('hidden')&&state.data)renderDiag(state.data);break;}
    case 'w-minus':{const w=$('weightG');w.value=Math.max(2000,(Number(w.value)||3700)-10);break;}
    case 'w-plus':{const w=$('weightG');w.value=Math.min(15000,(Number(w.value)||3700)+10);break;}
    case 'w-save':saveWeight();break;
    case 'bath':openBath();break;
    case 'memories':openMemory();break;
    case 'memory-postcard':await downloadPostcard();break;
    case 'bath-save':await saveBath();break;
  }
});

/* ---------- Formularz submit ---------- */
$('milkMl').addEventListener('input',()=>{
  const v=+$('milkMl').value||0;setText('milkAmount',`${v} ml`);
  // Suwak (tryb pojedynczego rodzaju / odciaganie) ustawia ilosc aktywnego rodzaju.
  if(!state.pumpMode){ if(state.milkMother&&!state.milkModified)state.milkMotherMl=v; else if(state.milkModified&&!state.milkMother)state.milkModifiedMl=v; }
});
$('bottleToggle').addEventListener('click',()=>{state.bottleOpen=!state.bottleOpen;updateBottle()});
$('milkMother').addEventListener('click',()=>toggleMilkKind('mother'));
$('milkModified').addEventListener('click',()=>toggleMilkKind('modified'));

/* ---------- Mleko mieszane: pola + popover wyboru ilosci ---------- */
function openMlPop(kind){
  state.mlPopKind=kind;
  const d=state.data||{};const mn=d.milkMinMl||20,mx=d.milkMaxMl||200,st=d.milkStepMl||10;
  const cur=kind==='mother'?state.milkMotherMl:state.milkModifiedMl;
  setText('mlPopTitle',kind==='mother'?'Ilość mleka matki':'Ilość mleka modyfikowanego');
  const r=$('mlPopRange');r.min=mn;r.max=mx;r.step=st;r.value=cur;
  setText('mlPopVal',`${cur} ml`);
  $('mlPop').classList.remove('hidden');
}
function closeMlPop(){$('mlPop').classList.add('hidden');state.mlPopKind=null}
function mlPopApply(){
  const v=+$('mlPopRange').value||0;
  if(state.mlPopKind==='mother')state.milkMotherMl=v; else if(state.mlPopKind==='modified')state.milkModifiedMl=v;
  syncDualLabels();
}
$('mlPickMother').addEventListener('click',()=>openMlPop('mother'));
$('mlPickModified').addEventListener('click',()=>openMlPop('modified'));
$('mlPopRange').addEventListener('input',()=>{setText('mlPopVal',`${$('mlPopRange').value} ml`);mlPopApply()});
$('mlPopMinus').addEventListener('click',()=>{const r=$('mlPopRange');r.value=Math.max(+r.min,(+r.value||0)-(+r.step||10));setText('mlPopVal',`${r.value} ml`);mlPopApply()});
$('mlPopPlus').addEventListener('click',()=>{const r=$('mlPopRange');r.value=Math.min(+r.max,(+r.value||0)+(+r.step||10));setText('mlPopVal',`${r.value} ml`);mlPopApply()});
$('mlPopOk').addEventListener('click',()=>{mlPopApply();closeMlPop()});
$('mlPopClose').addEventListener('click',closeMlPop);
$('entryForm').addEventListener('submit',async ev=>{
  ev.preventDefault();const n=$('formNotice');n.className='notice';n.textContent='Zapisywanie…';
  try{
    // Wylicz osobne ilosci mleka wg zaznaczonych rodzajow (mieszane = obie > 0).
    // Przy jednym rodzaju bierzemy jego pole; suwak juz zsynchronizowal to pole.
    const milkAmounts=()=>({
      mother: state.milkMother ? (+state.milkMotherMl||0) : 0,
      modified: state.milkModified ? (+state.milkModifiedMl||0) : 0,
    });
    let url='/api/entry', body;
    if(state.editFeedLine!=null){
      // EDYCJA istniejacego karmienia -> /api/update-feeding (in-place)
      const bottle=state.bottleOpen;
      if(bottle&&!state.milkMother&&!state.milkModified){n.className='notice error';n.textContent='Zaznacz rodzaj mleka albo zwiń butelkę (bez mleka).';return}
      url='/api/update-feeding';
      body=new URLSearchParams({feedLine:String(state.editFeedLine),when:$('entryTime').value});
      if(bottle){const a=milkAmounts();body.set('milkMotherMl',String(a.mother));body.set('milkModifiedMl',String(a.modified))}
      else{body.set('milkRemove','1')} // butelka zwinieta => brak mleka
    }
    else if(state.pumpMode){body=new URLSearchParams({type:'ODCIAGANIE',when:$('entryTime').value,ml:$('milkMl').value})}
    else{
      const extra=state.bottleOpen;
      if(extra&&!state.milkMother&&!state.milkModified){n.className='notice error';n.textContent='Zaznacz rodzaj mleka (matki i/lub modyfikowane).';return}
      body=new URLSearchParams({type:'KARMIENIE',when:$('entryTime').value,ml:'0',extraMilk:extra?'1':'0',lewaMin:Number($('piersL').value)||0,prawaMin:Number($('piersR').value)||0});
      if(extra){const a=milkAmounts();body.set('milkMotherMl',String(a.mother));body.set('milkModifiedMl',String(a.modified))}
    }
    const r=await request(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    n.className='notice ok';n.textContent=r.message||'Zapisano.';toast(state.editFeedLine!=null?'Zapisano zmiany':(state.pumpMode?'Zapisano odciąganie':'Zapisano karmienie'));
    const wasPump=state.pumpMode;state.editFeedLine=null;
    invalidateEntries();await refresh();
    setTimeout(()=>{const rd=state.activeDay;closeFormModal();if(rd&&!wasPump)openDay(rd,state.detailLabel);else clearPanels()},550);
  }catch(e){n.className='notice error';n.textContent=e.message}
});

/* ---------- Import ---------- */
$('importFile').addEventListener('change',async ev=>{
  const f=ev.target.files&&ev.target.files[0];ev.target.value='';if(!f)return;
  if(!confirm('Import ZASTĄPI wszystkie dane zawartością pliku.\nObecne dane zostaną najpierw zapisane jako kopia.\nKontynuować?'))return;
  try{const text=await f.text();if(text.length>512*1024){toast('Plik za duży (limit 512 KB)','err');return}
    const r=await request('/api/import',{method:'POST',headers:{'Content-Type':'text/csv'},body:text});await refresh();toast(r.message||'Zaimportowano')}
  catch(e){toast(e.message,'err')}
});

/* ---------- Wgranie pliku CSV z danymi (test) — POST /api/upload-data ----------
   Uwaga: wysyłamy jako multipart/form-data (pole "file"), NIE surowy text/csv —
   firewall hostingu (WAF) potrafi blokować surowe body POST (błąd 403). Multipart
   (zwykły formularz z plikiem) przechodzi tak samo jak inne zapisy panelu. */
$('uploadDataFile').addEventListener('change',async ev=>{
  const f=ev.target.files&&ev.target.files[0];ev.target.value='';if(!f)return;
  if(!confirm('Wgranie ZASTĄPI wszystkie dane zawartością pliku.\nDotychczasowe dane zostaną najpierw zapisane jako kopia (.bakap).\nKontynuować?'))return;
  if(f.size>512*1024){toast('Plik za duży (limit 512 KB)','err');return}
  try{
    const fd=new FormData();fd.append('file',f,f.name||'karmienia.csv');
    const r=await request('/api/upload-data',{method:'POST',body:fd});
    invalidateEntries();await refresh();
    toast(r.message||'Przyjęto plik');
  }catch(e){toast(e.message,'err')}
});

/* ---------- ESC / klawiatura ---------- */
document.addEventListener('keydown',ev=>{
  if(ev.key!=='Escape')return;
  if($('formModal').classList.contains('open')){const rd=state.activeDay;closeFormModal();rd?openDay(rd,state.detailLabel):clearPanels()}
  else if(MODALS.some(m=>$(m).classList.contains('open')))clearPanels();
});

/* ---------- Start ---------- */
navTo('start');
refresh();
setInterval(refresh,10000);
