/* SPDX-License-Identifier: BSD-3-Clause */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <sys/stat.h>
#include <cerrno>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/psram.h"
#include "jack_wave1_runtime.h"
#include "jack_wave1_audio.h"
#include "native_hal.h"
#ifdef OMF_RGB555_DIRECT
#include "omf_rgb555.h"
#endif
#ifdef OMF_TIMING_DIAG
#include "omf_timing.h"
#endif
#include "native_game_menu.h"
#include "omf_log_async.h"
#include "omf_loop_diag.h"
#ifdef OMF_FAULT_DIAG
#include "omf_fault_diag.h"
#endif
extern "C" unsigned native_hw_audio_level(void);
extern "C" bool native_usb_key_held(uint8_t key);
static uint8_t repeat_key;
static uint32_t repeat_at;
static unsigned repeat_delay=350,repeat_interval=120;
extern "C" int omf_game_main(int,char**);
extern "C" void omf_native_audio_service(void);
extern "C" uint32_t native_millis(){return to_ms_since_boot(get_absolute_time());}
extern "C" void native_sleep(unsigned ms){
    OMF_LOOP_SCOPE(omf_loop_wait_phase());
    uint32_t start=native_millis();
    do{jack_wave1_usb_service();omf_native_audio_service();omf_log_service();sleep_us(500);}while(native_millis()-start<ms);
}
extern "C" int native_key(uint8_t *usage,uint8_t *mod){
    omf_log_service();
    jack_wave1_usb_service();jack_wave1_key_event_t key;
    if(jack_wave1_usb_pop_key(&key)){
        *usage=repeat_key=key.keycode;*mod=key.modifier;repeat_at=native_millis()+repeat_delay;return 1;
    }
    if(repeat_key&&!native_usb_key_held(repeat_key))repeat_key=0;
    if(repeat_key&&repeat_delay&&repeat_interval&&int32_t(native_millis()-repeat_at)>=0){
        *usage=repeat_key;*mod=jack_wave1_usb_current_modifiers();repeat_at=native_millis()+repeat_interval;return 1;
    }
    return 0;
}
extern "C" void native_key_repeat(unsigned delay,unsigned interval){repeat_delay=delay;repeat_interval=interval;repeat_key=0;}
extern "C" uint16_t native_pad(){return jack_wave1_poll_physical_buttons();}
extern "C" void native_keys(uint8_t state[256]){
    OMF_LOOP_SCOPE(OMF_LOOP_KEYS);
    omf_log_service();
    jack_wave1_usb_service();
    for(unsigned i=0;i<256;i++)state[i]=native_usb_key_held(uint8_t(i));
    uint8_t mod=jack_wave1_usb_current_modifiers();
    for(unsigned i=0;i<8;i++)state[224+i]=(mod>>i)&1;
}
extern "C" int native_quit(){return 0;}
extern "C" size_t native_audio_queued(){return native_hw_audio_level();}
extern "C" void native_audio_submit(const int16_t *samples,size_t frames){jack_wave1_audio_submit(samples,frames);}
static void report_present();
extern "C" void native_present(const uint32_t *pixels,unsigned w,unsigned h,int offset){
    if(w!=320||h!=200)return;
#ifdef OMF_TIMING_DIAG
    uint32_t submit_start=omf_timing_draw_end();
#endif
    uint16_t *out=jack_wave1_framebuffer();
    if(offset<0)offset=0;if(offset>199)offset=199;
    for(unsigned y=0;y<200;y++){
        const unsigned char *src=(const unsigned char*)pixels+(y>=unsigned(offset)?(y-offset)*320*4:0);
        for(unsigned x=0;x<320;x++){
            out[(y+20)*320+x]=y<unsigned(offset)?0:uint16_t((src[x*4]>>3)<<10|(src[x*4+1]>>3)<<5|(src[x*4+2]>>3));
        }
    }
#ifdef OMF_TIMING_DIAG
    omf_timing_add(OMF_TIMING_VIDEO_SUBMIT,submit_start);
#endif
    report_present();
}
#ifdef OMF_RGB555_DIRECT
extern "C" void omf_present_half_argb(const uint32_t *pixels, int dx, int dy) {
    uint32_t submit_start=omf_timing_draw_end();
    {
    OMF_LOOP_SCOPE(OMF_LOOP_OUTPUT);
    omf_write_rgb555(jack_wave1_framebuffer()+20*320,pixels,dx,dy);
    omf_timing_add(OMF_TIMING_VIDEO_SUBMIT,submit_start);
    }
    report_present();
    omf_log_service();
    omf_native_audio_service();
}
#endif

static void report_present() {
    static uint32_t report_at;uint32_t now=native_millis();
    if(now-report_at>5000){report_at=now;
        OMF_LOOP_SCOPE(OMF_LOOP_LOG);
        struct mallinfo heap=mallinfo();
        printf("OMF_NATIVE ms=%lu heap_used=%lu heap_reserved=%lu\n",(unsigned long)now,(unsigned long)heap.uordblks,(unsigned long)heap.arena);
        jack_wave1_audio_report();
#ifdef OMF_TIMING_DIAG
        omf_timing_report();
#endif
#ifdef OMF_LOOP_DIAG
        omf_loop_report();
#endif
#ifdef OMF_FAULT_DIAG
        omf_fault_report();
#endif
        omf_log_report();
    }
}
int main(){
#ifdef OMF_FAULT_DIAG
    omf_fault_init();
#endif
    if(!jack_wave1_init("OpenOMF native development"))return 1;
    omf_log_init();
#ifdef OMF_LOOP_DIAG
    printf("OMF_LOOP_DIAG active=1 exclusive=1 gap_threshold_us=50000\n");
#endif
    if(!psram_is_available()||psram_get_size()<8u*1024u*1024u){jack_wave1_show_error("PSRAM","8 MiB required");return 2;}
    if(!jack_wave1_mount_sd()){jack_wave1_show_error("SD","Cannot mount SD card");return 3;}
    char selected[JACK_WAVE1_PATH_MAX];
    if(!native_game_select(NATIVE_GAME_OMF,"/roms/OPENOMF",selected,sizeof selected))return 6;
    if(chdir(selected)){jack_wave1_show_error("GAME","Cannot open selected directory");return 7;}
    if(mkdir("/SAVES",0755)&&errno!=EEXIST){jack_wave1_show_error("SAVES","Cannot create save directory");return 5;}
    if(!jack_wave1_audio_begin(44100))return 4;
    char name[]="openomf";
    char *args[]={name};
#ifdef OMF_FAULT_DIAG
    omf_fault_arm();
#endif
    int result=omf_game_main(1,args);
    printf("OMF_EXIT result=%d\n",result);
    while(true){jack_wave1_usb_service();omf_log_service();sleep_ms(10);}
}
