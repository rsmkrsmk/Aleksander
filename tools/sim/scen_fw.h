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
static void scenario_build(const char* sc){
  bool night=strstr(sc,"night");
  nightModeActive=night;
  time_t n=time(nullptr);
  lastFeedingTime=n-135*60;
  lastFeeding=String("18.10.2026  14:20\nKARMIENIE"); lastMilk=String("18.10.2026  12:30\nMLEKO MATKI | 120 ml");
  uiFillLive(M); uiFillCharts(M);
  // wydruk diagnostyczny
  fprintf(stderr,"gaps=%d open=%d rh=%d/%d/%d\n",M.gapN,M.gapOpen,M.rhN[0],M.rhN[1],M.rhN[2]);
  for(int i=0;i<M.gapN;i++)fprintf(stderr," gap %02d:%02d +%d\n",M.gapStart[i]/60,M.gapStart[i]%60,M.gapLen[i]);
  for(int i=0;i<UI_WEEKS;i++)fprintf(stderr," week t%d has=%d val=%d part=%d\n",M.weekNo[i],M.weekHas[i],M.weekVal[i],M.weekPartial[i]);
  for(int i=0;i<UI_NIGHTS;i++)fprintf(stderr," night %s min=%d %02d:%02d-%02d:%02d\n",M.nightLabel[i],M.nightMin[i],M.nightFrom[i]/60,M.nightFrom[i]%60,M.nightTo[i]/60,M.nightTo[i]%60);
  fprintf(stderr,"weight %d %s | bath %s dow=%d days=%d today=%d mask=%x last='%s' next='%s' nextDay=%d\n",M.lastWeightG,M.lastWeightDate,M.bathMonth,M.bathFirstDow,M.bathDays,M.bathToday,M.bathMask,M.bathLastTxt,M.bathNextTxt,M.bathNextDay);
  fprintf(stderr,"feedEntry='%s' bottle='%s' age='%s' date='%s' dress='%s' stat=%s/%s/%s\n",M.lastFeedEntry,M.lastBottleEntry,M.ageText,M.dateLine,M.dress,M.statVal[0],M.statVal[1],M.statVal[2]);
  int pg=0;const char*q=strchr(sc,'_');if(q&&isdigit(q[1]))pg=q[1]-'0';
  uiApplyTheme(night);
  if(strstr(sc,"saver")){uiSaverCreate(night);uiSaverUpdate(M);lv_screen_load(uiSaverScr);}
  else if(strstr(sc,"twice")){uiDashCreate(night);uiDashUpdate(M,false);uiDashShow(1);uiShowSaver();uiDashDestroy();uiDashCreate(!night);uiDashUpdate(M,false);uiDashShow(2);uiSaverCreate(night);uiSaverDestroy();uiDashCreate(night);uiDashUpdate(M,false);uiDashShow(0);uiDashUpdate(M,true);}
  else{uiDashCreate(night);uiDashUpdate(M,false);uiDashShow(pg);uiDashUpdate(M,false);}
}
static void scenario_tick(const char*,uint32_t){}
