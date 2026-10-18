#include <ctype.h>
#include "ui_modern.h"
static UiModel M;
static void fillDemo(UiModel&m,bool night){
  m.night=night; strcpy(m.clockHH,"15"); strcpy(m.clockMM,"44");
  strcpy(m.dateLine,"niedziela, 18 października"); strcpy(m.ageText,"10 tyg. i 1 dni | 2 mies.");
  m.hasLastFeed=true; m.sinceFeedMin=135; strcpy(m.nextFeedClock,"18:30"); strcpy(m.lastFeedTxt,"2 h 15 min temu");
  strcpy(m.napLine,"Czas na drzemkę"); m.napState=2;
  strcpy(m.statVal[0],"5"); strcpy(m.statVal[1],"300 ml"); strcpy(m.statVal[2],"2h 45m");
  m.weatherValid=true; m.wmo=2; m.temp=14; m.feels=12; m.tmin=8; m.tmax=16; strcpy(m.weatherDesc,"Częściowe zachmurzenie");
  strcpy(m.dress,"Chłodno: body, pajacyk, sweterek, czapka, kocyk do wózka");
  m.hours[0]={true,16,0,14}; m.hours[1]={true,17,0,13}; m.hours[2]={true,18,0,11};
  strcpy(m.tip,"Dziecko coraz sprawniej dostraja się do karmienia zgodnego z własnymi sygnałami głodu i sytości.");
}
static void fillCharts(UiModel&m){
  strcpy(m.lastFeedEntry,"15:20 · piersi L8/P6"); strcpy(m.lastBottleEntry,"12:30 · 120 ml");
  m.todayFeed=5; m.todayMl=420; m.todayDiapers=6; m.sleepTodayDay=170; m.sleepTodayNight=390;
  const char*lb[5]={"14.10","15.10","16.10","17.10","dziś"}; int mo[5]={300,420,360,480,320}, mm[5]={420,240,300,180,260}, mx[5]={0,0,120,0,60}, fe[5]={9,8,9,10,5};
  for(int i=0;i<5;i++){strcpy(m.days[i].label,lb[i]);m.days[i].milkMother=mo[i];m.days[i].milkModified=mm[i];m.days[i].milkMixed=mx[i];m.days[i].feed=fe[i];}
  m.gapN=6; int gs[6]={60,230,420,560,720,880}, gl[6]={170,190,140,160,150,200}; for(int i=0;i<6;i++){m.gapStart[i]=gs[i];m.gapLen[i]=gl[i];} m.gapOpen=true;
  const char*rl[3]={"dziś","17.10","16.10"}; for(int d=0;d<3;d++){strcpy(m.rhLabel[d],rl[d]);m.rhN[d]=7+d;for(int k=0;k<m.rhN[d];k++)m.rhT[d][k]=60+k*170+d*20;}
  for(int i=0;i<10;i++){m.weekHas[i]=i>0;m.weekVal[i]=20+((i*37)%40)-(i==5?50:0);m.weekNo[i]=i+1;m.weekPartial[i]=(i==9);} m.lastWeightG=5120; strcpy(m.lastWeightDate,"14.10.2026");
  {int dd[]={0,2,9,16,24,31,45,57,70}, gg[]={3080,2850,3010,3290,3500,3800,4300,4750,5340}; m.wpN=9; for(int i=0;i<9;i++){m.wpDay[i]=dd[i];m.wpG[i]=gg[i];}
   m.wBirthG=3080; m.wLastDay=70; m.wTodayDay=71; m.wGainTotal=2260; m.wHasSince=true; m.wSince=32; m.wHas14=true; m.w14=25;}
  for(int i=0;i<14;i++){m.nightMin[i]=(i%5==3)?-1:(300+((i*53)%200));m.nightFrom[i]=20*60+30;m.nightTo[i]=2*60+10;sprintf(m.nightLabel[i],"%d.10",4+i);}
  strcpy(m.bathMonth,"Październik 2026"); m.bathFirstDow=3; m.bathDays=31; m.bathToday=18; m.bathMask=(1u<<1)|(1u<<4)|(1u<<7)|(1u<<10)|(1u<<13)|(1u<<16); m.bathNextDay=18;
  strcpy(m.bathLastTxt,"17.10 · wczoraj"); strcpy(m.bathNextTxt,"18.10 · dziś");
  m.wifiOk=true;m.storageOk=true;m.timeOk=true;
}
static void scenario_build(const char* sc){
  bool night=strstr(sc,"night")!=nullptr;
  uiApplyTheme(night);
  if(!strncmp(sc,"boot",4)) uiBootCreate();
  else if(!strncmp(sc,"dash",4)){
    fillDemo(M,night); fillCharts(M); uiDashCreate(night); uiDashUpdate(M,false);
    int pg=0; const char*q=strchr(sc,'_'); if(q&&isdigit(q[1])) pg=q[1]-'0';
    uiDashShow(pg); uiDashUpdate(M,false);
  }
  else if(!strncmp(sc,"saver",5)){
    fillDemo(M,night);
    if(strstr(sc,"rain")) M.wmo=63; if(strstr(sc,"snow")) M.wmo=73; if(strstr(sc,"storm")) M.wmo=95; if(strstr(sc,"sun")) M.wmo=0; if(strstr(sc,"fog")) M.wmo=45; if(strstr(sc,"cloud")) M.wmo=3;
    if(strstr(sc,"alarm")){M.sinceFeedMin=255;strcpy(M.lastFeedTxt,"4 h 15 min temu");}
    uiSaverCreate(night); uiSaverUpdate(M); lv_screen_load(uiSaverScr);
  }
}
static void scenario_tick(const char* sc,uint32_t t){
  if(!strncmp(sc,"boot",4)){
    static int st=0;
    struct E{uint32_t t;int a,b;}; static const E ev[]={{400,0,0},{1300,0,1},{1350,1,0},{2300,1,1},{2350,2,0},{3000,2,1},{3050,3,0},{3600,3,2},{3650,4,0},{5000,4,1}};
    while(st<10&&t>=ev[st].t){uiBootStep(ev[st].a,ev[st].b);st++;}
    if(t==1008)uiBootProgress(30); if(t==3008)uiBootProgress(70); if(t==4992)uiBootProgress(100);
    if(t==5008)uiBootSetStatus("Gotowe!");
  }
}
