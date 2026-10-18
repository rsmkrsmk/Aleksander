// Profil animacji: ile pikseli odrysowuje LVGL na klatke (DIRECT) i ile trwa render na PC.
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
static uint16_t fb[480*480];
static uint32_t g_ms=0;
static uint64_t px=0; static int areas=0;
static void flush_cb(lv_display_t*d,const lv_area_t*a,uint8_t*p){px+=(uint64_t)lv_area_get_width(a)*lv_area_get_height(a);areas++;lv_display_flush_ready(d);}
static uint32_t tick_cb(){return g_ms;}
#include "scen_ui.h"
int main(int argc,char**argv){
  lv_init();lv_tick_set_cb(tick_cb);
  lv_display_t*d=lv_display_create(480,480);lv_display_set_flush_cb(d,flush_cb);
  lv_display_set_buffers(d,fb,NULL,sizeof fb,LV_DISPLAY_RENDER_MODE_DIRECT);
  scenario_build(argv[1]);
  int T=atoi(argv[2]);int frames=0;double sumpx=0,maxpx=0,tms=0,maxt=0;
  for(g_ms=0;g_ms<=(uint32_t)T;g_ms+=16){
    scenario_tick(argv[1],g_ms);px=0;areas=0;
    struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);lv_timer_handler();clock_gettime(CLOCK_MONOTONIC,&b);
    double t=(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;
    if(g_ms<1500) continue; // pomijamy animacje wejsciowe
    if(getenv("V")&&(t>0.6||px>0.12*230400))printf("  t=%u ms: %.2f ms, %.1f%%, %d areas\n",g_ms,t,px/2304.0,areas);
    frames++;sumpx+=px;if(px>maxpx)maxpx=px;tms+=t;if(t>maxt)maxt=t;
  }
  printf("%-16s srednio %5.1f%% ekranu/klatke (max %5.1f%%), render PC %.2f ms (max %.2f)\n",argv[1],sumpx/frames/2304,maxpx/2304,tms/frames,maxt);
}
