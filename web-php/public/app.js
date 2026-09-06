/* ============================================================================
   Leśny Dziennik Aleksandra — logika panelu WWW (redesign)
   Konsumuje ten sam kontrakt API co poprzednio (bez zmian w backendzie).
   ============================================================================ */
'use strict';

const state = { data:null, view:'home', activeDay:null, detailLabel:null,
  milkType:'MLEKO_MATKI', bottleOpen:false, pumpMode:false, summaryExtra:0 };
const $ = id => document.getElementById(id);
const MODALS = ['formModal','calendarModal','detailModal','chartModal','chart5Modal','otherModal','weightModal','sleepModal','diaperModal'];

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

/* ---------- Modale ---------- */
function show(view){
  state.view=view||'home';
  MODALS.forEach(m=>{const el=$(m);const on=(m===view);el.classList.toggle('open',on);el.setAttribute('aria-hidden',on?'false':'true')});
  document.body.classList.toggle('modal-open',!!view&&view!=='home');
  if(!view||view==='home')window.scrollTo({top:0,behavior:'smooth'});
}
function openFormModal(){$('formModal').classList.add('open');$('formModal').setAttribute('aria-hidden','false');document.body.classList.add('modal-open')}
function closeFormModal(){$('formModal').classList.remove('open');$('formModal').setAttribute('aria-hidden','true');document.body.classList.remove('modal-open');state.pumpMode=false}
function clearPanels(){state.activeDay=null;show('home')}

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

  // licznik + pierścień
  const ageMin=data.lastFeedingAgeMin;
  const ring=$('ringProg');const C=2*Math.PI*86;
  ring.setAttribute('stroke-dasharray',C.toFixed(1));
  if(ageMin>=0){
    const frac=Math.min(ageMin/240,1);
    ring.setAttribute('stroke-dashoffset',(C*(1-frac)).toFixed(1));
    const h=Math.floor(ageMin/60),m=ageMin%60;
    setText('counterBig',h>0?`${h}:${pad(m)}`:`${m}`);
    setText('counterLbl',h>0?'godz. temu':'min temu');
  }else{ring.setAttribute('stroke-dashoffset',C.toFixed(1));setText('counterBig','—');setText('counterLbl','brak wpisu')}
  setText('nextTime',data.nextFeedingIso?data.nextFeedingIso.slice(11,16):'—');
  setText('avgGap',fmtGap(data.avgFeedingGapMin));
  const dot=$('pillDot');const pill=$('pillNext');dot.className='state-dot';pill.className='pill';
  if(ageMin>=240){dot.classList.add('danger');pill.classList.add('danger')}
  else if(ageMin>=180){dot.classList.add('warn');pill.classList.add('warn')}

  twoLine($('lastFeed'),compact(data.lastFeeding,data.lastFeedingAgo,false));
  twoLine($('lastMilk'),compact(data.lastMilk,null,true));

  // motyw wg pory (o ile użytkownik nie wybrał ręcznie)
  if(!themeManual)applyTheme(data.night?'night':'day');

  // status
  const sdot=ok=>`<span class="dot ${ok?'':'err'}"></span>`;
  $('statusline').innerHTML=
    `<span class="s">${sdot(data.storage)}Pamięć ${data.storage?'OK':'błąd'}</span>`+
    `<span class="s">${sdot(data.timeValid)}Czas ${data.timeValid?'OK':'—'}</span>`;
  setText('sleepQaLbl',data.sleepInProgress?'Sen (śpi)':'Sen');

  renderCalendarPreview(data.calendar);
  renderDayBand();
  renderDiag(data);
  if($('sleepModal').classList.contains('open'))renderSleep();
}
function compact(value,ago,bottle){
  if(!value||value.startsWith('Brak'))return 'Brak wpisu';
  const parts=value.split('\n');
  const time=(parts[0].split('  ').pop()||parts[0]);
  if(ago)return `${time}\n${ago}`;
  if(bottle){const detail=(parts[1]||'').replace(/MLEKO |Mleko | \| /g,'');return `${time}\n${detail}`}
  return `${time}\nzapisano`;
}
/* val ma \n -> zamień na <small> dla ładnej hierarchii */
function twoLine(el,txt){const p=(txt||'').split('\n');el.innerHTML=`${p[0]||'—'}${p[1]?`<small>${p[1]}</small>`:''}`}



/* ---------- Kalendarz (podgląd = pełny modal) ---------- */
function calStatChip(svg,txt){return `<span class="stat"><svg viewBox="0 0 24 24" fill="none">${svg}</svg>${txt}</span>`}
function buildCalCard(day,onClick){
  const p=(day.date||'').split('-');
  const card=document.createElement('article');card.className='card cal-day';
  if(day.label&&day.label.startsWith('DZISIAJ'))card.classList.add('today');
  const months=['sty','lut','mar','kwi','maj','cze','lip','sie','wrz','paź','lis','gru'];
  const mIdx=Math.max(0,Math.min(11,(parseInt(p[1],10)||1)-1));
  card.innerHTML=
    `<div class="cal-daynum"><span class="d">${p[2]||''}</span><span class="m">${months[mIdx]}</span></div>`+
    `<div class="cal-body"><div class="name">${day.label||''}</div><div class="cal-stats">`+
      calStatChip('<path d="M8 3h8l-1 5a4 4 0 0 1-6 0L8 3Z" fill="currentColor"/>',`${day.feedingCount} karm.`)+
      calStatChip('<path d="M9 2h6v3l1 3v12a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2V8l1-3V2Z" fill="currentColor"/>',`${day.milkMl} ml`)+
      calStatChip('<path d="M4 6h16v5a8 8 0 0 1-16 0V6Z" fill="currentColor"/>',`${(day.diaperWet||0)+(day.diaperDirty||0)} piel.`)+
      (day.vitaminD?calStatChip('<circle cx="12" cy="12" r="8" stroke="currentColor" stroke-width="1.6"/>','Wit.D'):'')+
    `</div></div>`+
    `<span class="cal-arrow"><svg viewBox="0 0 24 24" width="20" height="20" fill="none"><path d="M9 6l6 6-6 6" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg></span>`;
  card.addEventListener('click',onClick);
  return card;
}
function renderCalendarPreview(days){
  const list=$('calendarList');if(!list||!days)return;list.replaceChildren();
  days.forEach(day=>list.append(buildCalCard(day,()=>openDay(day.date,day.label))));
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
async function openDay(date,label){
  state.activeDay=date;state.detailLabel=label;
  setText('detailTitle',label||dateLabel(date));
  const list=$('detailList');list.replaceChildren();
  const sk=document.createElement('div');sk.className='skeleton';sk.style.height='60px';list.append(sk);
  show('detailModal');
  try{
    const data=await request(`/api/entries?date=${encodeURIComponent(date)}`);
    list.replaceChildren();
    if(!data.entries.length){const p=document.createElement('p');p.className='empty';p.textContent='Brak wpisów tego dnia';list.append(p);return}
    data.entries.forEach(en=>list.append(buildEntry(en,()=>openDay(state.activeDay,state.detailLabel))));
  }catch(e){setText('detailList',e.message)}
}

/* ---------- Podsumowanie (2 dni + starsze) ---------- */
async function renderSummary(){
  const root=$('summaryList');root.replaceChildren();
  const sk=document.createElement('div');sk.className='skeleton';sk.style.height='80px';root.append(sk);
  const cal=state.data&&state.data.calendar?state.data.calendar:[];
  const days=[cal[0],cal[1]].filter(Boolean);
  let lists;try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).catch(()=>({entries:[]}))))}catch(e){root.replaceChildren();setText('summaryList',e.message);return}
  root.replaceChildren();
  days.forEach((d,i)=>{
    const block=document.createElement('div');block.className='day-block';
    const h=document.createElement('div');h.className='dbh';h.textContent=(i===0?'Dzisiaj · ':'Wczoraj · ')+dateLabel(d.date).slice(0,5);block.append(h);
    const es=(lists[i]&&lists[i].entries)||[];
    if(!es.length){const p=document.createElement('p');p.className='empty';p.textContent='Brak wpisów';block.append(p)}
    es.forEach(en=>block.append(buildEntry(en,()=>renderSummary())));
    root.append(block);
  });
  for(let k=2;k<2+(state.summaryExtra||0);k++){
    const ds=isoDaysAgo(k);
    try{const data=await request(`/api/entries?date=${encodeURIComponent(ds)}`);const es=data.entries||[];
      const block=document.createElement('div');block.className='day-block';
      const h=document.createElement('div');h.className='dbh';h.textContent=dateLabel(ds).slice(0,5);block.append(h);
      if(!es.length){const p=document.createElement('p');p.className='empty';p.textContent='Brak wpisów';block.append(p)}
      es.forEach(en=>block.append(buildEntry(en,()=>renderSummary())));root.append(block);
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
    const stack=document.createElement('div');stack.className='bar-stack';stack.style.height=((d.milkMl/MAX)*140+4)+'px';
    if(d.motherMilkMl>0){const s=document.createElement('div');s.className='bar-seg mother';s.style.height=(d.motherMilkMl/Math.max(d.milkMl,1)*100)+'%';s.title=d.motherMilkMl+' ml matki';stack.append(s)}
    if(d.modifiedMilkMl>0){const s=document.createElement('div');s.className='bar-seg modified';s.style.height=(d.modifiedMilkMl/Math.max(d.milkMl,1)*100)+'%';s.title=d.modifiedMilkMl+' ml modyf.';stack.append(s)}
    const lab=document.createElement('div');lab.className='lab';lab.textContent=(d.label||'').split(' - ').pop().slice(0,5);
    const feeds=document.createElement('div');feeds.className='feeds';feeds.textContent='● '+d.feedingCount;
    col.append(v,stack,feeds,lab);host.append(col);
  });
  let html='<table class="data-table"><tr><th>Dzień</th><th>Karm.</th><th>Matki</th><th>Mod.</th><th>Suma</th></tr>';
  cal.forEach(d=>{const label=(d.label||'').split(' - ')[0];const suma=(d.motherMilkMl||0)+(d.modifiedMilkMl||0);html+=`<tr><td>${label}</td><td>${d.feedingCount}</td><td>${d.motherMilkMl} ml</td><td>${d.modifiedMilkMl} ml</td><td class="ok">${suma} ml</td></tr>`});
  html+='</table>';$('extraTable').innerHTML=html;
}
async function renderAnalysis(){
  const gapHost=$('gapChart'),rh=$('rhythm3');gapHost.replaceChildren();rh.replaceChildren();
  const cal=(state.data&&state.data.calendar)||[];const days=cal.slice(0,3);
  let lists=[];try{lists=await Promise.all(days.map(d=>request(`/api/entries?date=${encodeURIComponent(d.date)}`).then(r=>r.entries||[]).catch(()=>[])))}catch(e){lists=days.map(()=>[])}
  const toMin=t=>{const p=t.split(':');return(+p[0])*60+(+p[1])};
  const todayFeeds=(lists[0]||[]).filter(e=>e.type==='KARMIENIE').map(e=>e.time).sort();
  if(todayFeeds.length<2){const p=document.createElement('p');p.className='empty';p.textContent=todayFeeds.length?'Tylko jedno karmienie dziś.':'Brak karmień dziś.';gapHost.append(p);setText('gapSummary','')}
  else{let gaps=[],maxGap=0;for(let i=1;i<todayFeeds.length;i++){const g=toMin(todayFeeds[i])-toMin(todayFeeds[i-1]);gaps.push({from:todayFeeds[i-1],to:todayFeeds[i],min:g});if(g>maxGap)maxGap=g}
    const avg=Math.round(gaps.reduce((a,b)=>a+b.min,0)/gaps.length);
    setText('gapSummary',`Karmień: ${todayFeeds.length} · średni odstęp ${fmtGap(avg)}`);
    gaps.forEach(g=>{const row=document.createElement('div');row.className='gap-row';row.innerHTML=`<div class="gap-time">${g.from} → ${g.to}</div><div class="gap-track"><div class="gap-fill" style="width:${maxGap?Math.max(6,g.min/maxGap*100):6}%"></div></div><div class="gap-val">${fmtGap(g.min)}</div>`;gapHost.append(row)});
  }
  days.forEach((d,i)=>{
    const all=(lists[i]||[]).map(e=>{const f=e.type==='KARMIENIE',mk=(e.type||'').startsWith('MLEKO');if(!f&&!mk)return null;const p=e.time.split(':');return{mins:(+p[0])*60+(+p[1]),milk:mk,time:e.time,label:e.label||e.type,ml:e.ml||0}}).filter(Boolean).sort((a,b)=>a.mins-b.mins);
    const feeds=all.filter(f=>!f.milk);const gaps=[];for(let j=1;j<feeds.length;j++)gaps.push(feeds[j].mins-feeds[j-1].mins);
    const avg=gaps.length?Math.round(gaps.reduce((a,b)=>a+b,0)/gaps.length):0;
    const wrap=document.createElement('div');wrap.className='rhythm-day';
    wrap.innerHTML=`<div class="rh-head"><span class="rh-name">${i===0?'Dziś':i===1?'Wczoraj':'2 dni temu'} · ${dateLabel(d.date).slice(0,5)}</span><span class="rh-stat"><b>${feeds.length}</b> karmień${gaps.length?` · śr. ${fmtGap(avg)}`:''}</span></div>`;
    if(!all.length){const em=document.createElement('div');em.className='empty';em.textContent='Brak karmień';wrap.append(em);rh.append(wrap);return}
    let prev=null;
    all.forEach(f=>{const it=document.createElement('div');it.className='rh-item'+(f.milk?' milk':'');
      const sub=f.milk?'butelka':(prev!=null?'po '+fmtGap(f.mins-prev):'pierwsze');if(!f.milk)prev=f.mins;
      it.innerHTML=`<div class="rh-time">${f.time}</div><div class="rh-node"></div><div class="rh-info"><div class="t">${f.milk?(f.label+(f.ml?` · ${f.ml} ml`:'')):'Karmienie'}</div><div class="s">${sub}</div></div>`;
      wrap.append(it)});
    rh.append(wrap);
  });
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
function openWeight(){const d=state.data||{};$('weightG').value=(d.lastWeightG&&d.lastWeightG>0)?d.lastWeightG:3700;setText('weightNotice','');show('weightModal');renderWeightChart()}
async function saveWeight(){
  const grams=Number($('weightG').value)||0;const n=$('weightNotice');n.className='notice';
  if(grams<2000||grams>15000){n.className='notice error';n.textContent='Podaj wagę 2000–15000 g.';return}
  n.textContent='Zapisywanie…';
  try{const r=await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type:'WAGA',ml:String(grams)})});n.className='notice ok';n.textContent=r.message||'Zapisano.';toast('Zapisano wagę');await refresh();renderWeightChart()}
  catch(e){n.className='notice error';n.textContent=e.message}
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

/* ============================================================================
   Formularz karmienia / odciągania
   ============================================================================ */
function setMilkType(t){state.milkType=t;$('milkMother').classList.toggle('sel',t==='MLEKO_MATKI');$('milkModified').classList.toggle('sel',t==='MLEKO_MODYFIKOWANE')}
function updateBottle(){const o=state.bottleOpen;$('extraMilkOptions').classList.toggle('hidden',!o);$('bottleToggle').textContent=o?'− Ukryj butelkę':'＋ Dodaj butelkę'}
function openForm(date){
  state.pumpMode=false;
  ['timeField','nudgeBox','quickNotice','bottleToggle','nursingBox'].forEach(id=>$(id).classList.remove('hidden'));
  setText('extraTitle','Rodzaj mleka');$('kindField').classList.remove('hidden');$('mlField').querySelector('label').innerHTML='Ilość: <span id="milkAmount">30 ml</span>';
  setText('formTitle',date?`Karmienie · ${dateLabel(date)}`:'Nowe karmienie');
  $('entryTime').value=date?`${date}T12:00`:dateTimeInput(state.data&&state.data.nowIso);
  const d=state.data||{};$('milkMl').value=d.defaultMl||30;$('milkMl').min=d.minMl||10;$('milkMl').max=d.maxMl||120;
  $('piersL').value=0;$('piersR').value=0;state.bottleOpen=false;setMilkType('MLEKO_MATKI');updateBottle();
  setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal();
}
function openPumping(){
  state.pumpMode=true;
  ['timeField','nudgeBox','quickNotice','bottleToggle','nursingBox'].forEach(id=>$(id).classList.add('hidden'));
  $('extraMilkOptions').classList.remove('hidden');$('kindField').classList.add('hidden');
  setText('formTitle','Odciąganie mleka');
  $('mlField').querySelector('label').innerHTML='Ilość: <span id="milkAmount">30 ml</span>';
  const d=state.data||{};$('milkMl').value=d.defaultMl||30;$('milkMl').min=d.minMl||10;$('milkMl').max=d.maxMl||120;
  // przywracamy pole czasu dla odciągania (musimy wysłać when)
  $('timeField').classList.remove('hidden');$('nudgeBox').classList.add('hidden');
  $('entryTime').value=dateTimeInput(state.data&&state.data.nowIso);
  setText('milkAmount',`${$('milkMl').value} ml`);setText('formNotice','');openFormModal();
}
function nudge(min){const i=$('entryTime');const d=new Date(i.value);if(Number.isNaN(d.getTime()))return;d.setMinutes(d.getMinutes()+min);i.value=dateTimeInput(d)}

/* ============================================================================
   API akcje
   ============================================================================ */
async function refresh(){
  try{render(await request('/api/status'));}
  catch(e){$('statusline').innerHTML=`<span class="s"><span class="dot err"></span>${e.message}</span>`}
}
async function postEvent(type,ml=0){
  try{await request('/api/event',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({type,ml:String(ml)})});await refresh();return true}
  catch(e){toast(e.message,'err');return false}
}

/* ---------- Delegacja kliknięć ---------- */
document.addEventListener('click',async ev=>{
  const el=ev.target.closest('[data-action]');if(!el)return;
  const a=el.dataset.action;
  switch(a){
    case 'new-feed':openForm();break;
    case 'sleep-open':show('sleepModal');renderSleep();break;
    case 'sleep':{const t=(state.data&&state.data.sleepInProgress)?'SEN_STOP':'SEN_START';if(await postEvent(t)){toast(t==='SEN_START'?'Zaznaczono zaśnięcie':'Zaznaczono pobudkę');renderSleep()}break;}
    case 'weight':openWeight();break;
    case 'other':show('otherModal');break;
    case 'calendar':show('calendarModal');break;
    case 'chart':show('chartModal');state.summaryExtra=0;renderSummary();break;
    case 'chart5':show('chart5Modal');if(state.data&&state.data.calendar)renderChart(state.data.calendar);renderAnalysis();break;
    case 'home':clearPanels();break;
    case 'back-calendar':show('calendarModal');break;
    case 'day-feed':if(state.activeDay)openForm(state.activeDay);break;
    case 'cancel-form':{const rd=state.activeDay;closeFormModal();rd?openDay(rd,state.detailLabel):clearPanels();break;}
    case 'minus5':nudge(-5);break;
    case 'plus5':nudge(5);break;
    case 'diaper':show('diaperModal');break;
    case 'diaper-wet':if(await postEvent('PIELUCHA_MOKRA')){toast('Zapisano: pielucha mokra');clearPanels()}break;
    case 'diaper-dirty':if(await postEvent('PIELUCHA_BRUDNA')){toast('Zapisano: pielucha brudna');clearPanels()}break;
    case 'pumping':openPumping();break;
    case 'vitamin':{const ok=await postEvent('WITAMINA_D');if(ok){toast('Zapisano witaminę D');renderSummary()}break;}
    case 'import':$('importFile').click();break;
    case 'toggle-diag':{const c=$('diagCard');c.classList.toggle('hidden');if(!c.classList.contains('hidden')&&state.data)renderDiag(state.data);break;}
    case 'w-minus':{const w=$('weightG');w.value=Math.max(2000,(Number(w.value)||3700)-10);break;}
    case 'w-plus':{const w=$('weightG');w.value=Math.min(15000,(Number(w.value)||3700)+10);break;}
    case 'w-save':saveWeight();break;
  }
});

/* ---------- Formularz submit ---------- */
$('milkMl').addEventListener('input',()=>setText('milkAmount',`${$('milkMl').value} ml`));
$('bottleToggle').addEventListener('click',()=>{state.bottleOpen=!state.bottleOpen;updateBottle()});
$('milkMother').addEventListener('click',()=>setMilkType('MLEKO_MATKI'));
$('milkModified').addEventListener('click',()=>setMilkType('MLEKO_MODYFIKOWANE'));
$('entryForm').addEventListener('submit',async ev=>{
  ev.preventDefault();const n=$('formNotice');n.className='notice';n.textContent='Zapisywanie…';
  try{
    let body;
    if(state.pumpMode){body=new URLSearchParams({type:'ODCIAGANIE',when:$('entryTime').value,ml:$('milkMl').value})}
    else{const extra=state.bottleOpen;body=new URLSearchParams({type:'KARMIENIE',when:$('entryTime').value,ml:'0',extraMilk:extra?'1':'0',lewaMin:Number($('piersL').value)||0,prawaMin:Number($('piersR').value)||0});if(extra){body.set('milkType',state.milkType);body.set('milkMl',$('milkMl').value)}}
    const r=await request('/api/entry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
    n.className='notice ok';n.textContent=r.message||'Zapisano.';toast(state.pumpMode?'Zapisano odciąganie':'Zapisano karmienie');
    await refresh();
    setTimeout(()=>{const rd=state.activeDay;closeFormModal();if(rd&&!state.pumpMode)openDay(rd,state.detailLabel);else clearPanels()},550);
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

/* ---------- ESC / klawiatura ---------- */
document.addEventListener('keydown',ev=>{
  if(ev.key!=='Escape')return;
  if($('formModal').classList.contains('open')){const rd=state.activeDay;closeFormModal();rd?openDay(rd,state.detailLabel):clearPanels()}
  else if(MODALS.some(m=>$(m).classList.contains('open')))clearPanels();
});

/* ---------- Start ---------- */
refresh();
setInterval(refresh,10000);
