#include "fw_shim.h"
#include "ui_modern.h"
#include "config.h"
#include "dressing_guide.h"
// ----- atrapy stanu/funkcji firmware -----
enum WeatherKind : uint8_t { W_SUN, W_PARTLY, W_CLOUD, W_FOG, W_RAIN, W_SNOW, W_STORM };
struct WeatherState{bool valid;int tempNow,feelsLike,codeNow,tempMinDay,tempMaxDay;struct{uint8_t hour,minute;int temp;bool has;}next[3];};
struct DaySummary{int feedingCount,milkCount,milkMl,motherMilkMl,modifiedMilkMl,mixedMilkMl,piersLeftMin,piersRightMin,diaperWet,diaperDirty,pumpingMl;bool vitaminD;int weightG,sleepDayMin,sleepNightMin,napCount,bathCount;};
struct WakeWindow{int minMin,maxMin;};
enum {WL_CONNECTED=3}; struct {int status(){return 3;}} WiFi;
#define FILE_READ "r"
#define DATA_FILE_PATH "/tmp/fw_data.csv"
struct File{FILE*f=nullptr;int pk=-2;operator bool()const{return f;}
 bool available(){if(!f)return false;int c=fgetc(f);if(c==EOF)return false;ungetc(c,f);return true;}
 size_t readBytesUntil(char t,char*b,size_t n){size_t i=0;int c;while(i<n&&(c=fgetc(f))!=EOF){if(c==t)break;b[i++]=c;}return i;}
 void close(){if(f)fclose(f);f=nullptr;}};
struct {File open(const char*p,const char*){File x;x.f=fopen(p,"r");return x;}} LittleFS;
static bool nightModeActive=false,storageReady=true,timeIsValid=true,sleepInProgress=false;
static time_t lastFeedingTime,sleepStartedTime,lastWakeTime;
static String lastFeeding,lastMilk;
static int longestFeedingGapMin,avgFeedingGapMin;
static bool currentLocalTime(struct tm&t){time_t n=time(nullptr);localtime_r(&n,&t);return true;}
static time_t beginningOfDay(time_t v){struct tm d;localtime_r(&v,&d);d.tm_hour=d.tm_min=d.tm_sec=0;d.tm_isdst=-1;return mktime(&d);}
static String dateIso(time_t v){struct tm d;localtime_r(&v,&d);char b[11];strftime(b,sizeof b,"%Y-%m-%d",&d);return String(b);}
static time_t dayOffsetFromToday(uint8_t back){time_t n=time(nullptr);struct tm d;localtime_r(&n,&d);d.tm_mday-=back;d.tm_hour=12;d.tm_min=d.tm_sec=0;d.tm_isdst=-1;return beginningOfDay(mktime(&d));}
static String calculateAgeText(){return String("Aleksander ma 71 dni\n10 tyg. i 1 dni | 2 mies.");}
static long calculateAgeDays(){return 71;}
static String nextFeedingClock(){if(!lastFeedingTime)return String();time_t n=lastFeedingTime+240*60;struct tm t;localtime_r(&n,&t);char b[8];strftime(b,8,"%H:%M",&t);return String(b);}
static WakeWindow wakeWindowMinutes(long){return {60,90};}
static void sleepNeedMinutes(long,int&n,int&d){n=600;d=300;}
static void recomputeFeedingRhythm(){avgFeedingGapMin=165;longestFeedingGapMin=200;}
static void dayStats(time_t,DaySummary&o){memset(&o,0,sizeof o);o.feedingCount=6;o.milkMl=300;o.motherMilkMl=120;o.modifiedMilkMl=100;o.mixedMilkMl=80;o.diaperWet=4;o.diaperDirty=2;o.sleepDayMin=210;o.sleepNightMin=420;o.bathCount=0;}
static WeatherState snapshotWeather(){WeatherState w={};w.valid=true;w.tempNow=14;w.feelsLike=12;w.codeNow=2;w.tempMinDay=8;w.tempMaxDay=16;w.next[0]={16,0,14,true};w.next[1]={17,0,13,true};w.next[2]={18,0,11,true};return w;}
static WeatherKind weatherKindOf(int c){return c==0?W_SUN:c<3?W_PARTLY:W_CLOUD;}
static String developmentTipForToday(){return String("Dzień 71: tip testowy.");}
static const char* formatEntryStub(){return "";}
struct LineReader{File&f;explicit LineReader(File&x):f(x){} bool more(){return f.available();}
 size_t nextBuf(char*d,size_t cap){size_t n=0;int c;while((c=fgetc(f.f))!=EOF&&c!='\n'){if(n+1<cap)d[n++]=c;}d[n]=0;return n;}};
#include "glue_extract.h"
static UiModel M;
static void uiShowSaver(){uiSaverCreate(false);uiSaverUpdate(M);lv_screen_load(uiSaverScr);}

static bool gPress=false; static int gX=0,gY=0; static bool pendExit=false,pendEnter=false; static bool inSaver=false; static unsigned rngs=12345;
static unsigned rnd(){rngs=rngs*1103515245u+12345u;return (rngs>>16)&0x7fff;}
static void rd(lv_indev_t*,lv_indev_data_t*d){d->point.x=gX;d->point.y=gY;d->state=gPress?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
static int nH=0,nS=0; static void fwCreateHome(){ ++nH; uiDashCreate(nightModeActive); uiFillLive(M); uiFillCharts(M); uiDashUpdate(M,false); uiDashShow(0); uiDashUpdate(M,false); uiSaverDestroy(); inSaver=false;}
static void fwEnterSaver(){ ++nS; uiSaverCreate(nightModeActive); uiFillLive(M); uiSaverUpdate(M); lv_screen_load(uiSaverScr); uiDashDestroy(); inSaver=true;}
static void scenario_build(const char* sc){
  time_t n=time(nullptr); lastFeedingTime=n-135*60;
  lastFeeding=String("x"); lastMilk=String("y");
  uiApplyTheme(false);
  uiOnBackToSaver=[](){pendEnter=true;};
  uiOnDiagnostics=[](){};
  lv_indev_t*in=lv_indev_create(); lv_indev_set_type(in,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(in,rd);
  uiFillLive(M); fwEnterSaver();
}
static void scenario_tick(const char*,uint32_t ms){
  static uint32_t nextEv=200; static int phase=0; if(ms%60000==0) fprintf(stderr,"t=%u home=%d saver=%d page=%d\n",ms,nH,nS,uiDashCur);
  if(pendExit){pendExit=false; fwCreateHome();}
  if(pendEnter){pendEnter=false; if(!inSaver) fwEnterSaver();}
  if(ms%5000==0){ if(inSaver){uiFillLive(M);uiSaverUpdate(M);} }
  if(ms%1000==0 && !inSaver){ uiFillLive(M); bool ch=(ms%4000==0); if(ch) uiFillCharts(M); uiDashUpdate(M,ch);}
  if(ms>=nextEv){
    if(phase==0){ gX=rnd()%480; gY=rnd()%480; gPress=true; if(inSaver){pendExit=true;} phase=1; nextEv=ms+16*(1+rnd()%6);}
    else { // drag sometimes
      if(rnd()%3==0){ gX+= (rnd()%2?1:-1)*(60+rnd()%120); if(gX<0)gX=0; if(gX>479)gX=479; }
      gPress=false; phase=0; nextEv=ms+16*(2+rnd()%40);
      if(rnd()%17==0){ nightModeActive=!nightModeActive; if(inSaver) fwEnterSaver(); else fwCreateHome(); }
      if(rnd()%11==0 && !inSaver) pendEnter=true;
    }
  }
}
