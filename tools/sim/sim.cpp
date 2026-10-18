// Symulator PC: renderuje UI LVGL 9.3 (ta sama konfiguracja co firmware) do plikow RAW RGB565.
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <math.h>
#include <time.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
static uint16_t fb[SCREEN_WIDTH*SCREEN_HEIGHT];
static uint32_t g_ms=0;
static void flush_cb(lv_display_t*d,const lv_area_t*a,uint8_t*px){lv_display_flush_ready(d);}
static uint32_t tick_cb(){return g_ms;}
#ifdef SIM_SCENARIOS
#include SIM_SCENARIOS
#endif
static void dump(const char*prefix,uint32_t ms){
  char fn[256];snprintf(fn,sizeof fn,"%s_%05u.raw",prefix,ms);
  FILE*f=fopen(fn,"wb");fwrite(fb,2,SCREEN_WIDTH*SCREEN_HEIGHT,f);fclose(f);
}
int main(int argc,char**argv){
  if(argc<4){fprintf(stderr,"uzycie: sim <scenariusz> <prefix> <ms,ms,...> [step]\n");return 1;}
  std::string sc=argv[1];const char*prefix=argv[2];
  std::vector<uint32_t> shots;{char*s=strdup(argv[3]);for(char*t=strtok(s,",");t;t=strtok(NULL,","))shots.push_back(atoi(t));}
  int step=argc>4?atoi(argv[4]):16;
  lv_init();lv_tick_set_cb(tick_cb);
  lv_display_t*d=lv_display_create(SCREEN_WIDTH,SCREEN_HEIGHT);
  lv_display_set_flush_cb(d,flush_cb);
  lv_display_set_buffers(d,fb,NULL,sizeof fb,LV_DISPLAY_RENDER_MODE_DIRECT);
  memset(fb,0,sizeof fb);
  scenario_build(sc.c_str());
  uint32_t last=shots.back();size_t si=0;
  for(g_ms=0;g_ms<=last+step;g_ms+=step){
    scenario_tick(sc.c_str(),g_ms);
    lv_timer_handler();
    while(si<shots.size()&&shots[si]<=g_ms){dump(prefix,shots[si]);si++;}
  }
  return 0;
}
