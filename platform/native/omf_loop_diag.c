/* SPDX-License-Identifier: BSD-3-Clause */
#include "omf_loop_diag.h"
#ifdef OMF_FAULT_DIAG
#include "omf_fault_diag.h"
#endif
#include "pico/time.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint64_t us;uint32_t calls,max_wall;} loop_stat;
typedef struct {uint32_t us,end;bool open;uint64_t phase[OMF_LOOP_COUNT];} gap_stat;
static const char *const names[OMF_LOOP_COUNT]={
    "other","input","keys","static","dynamic","statepal","draw","output",
    "audio","limit","hit","wait","log","file","load"
#ifdef OMF_DYNAMIC_DIAG
    ,"d_ctrl","d_scene","d_input","d_clean","d_anim","d_move","d_coll","d_apply","d_free","d_shake"
#endif
};
_Static_assert(OMF_LOOP_COUNT%5==0,"gap report groups must contain five phases");
/* Main-core-only probes. Nested audio / I/O time is excluded from its parent. */
static loop_stat phases[OMF_LOOP_COUNT];
static omf_loop_phase current;
static bool started,pcm_seen,frame_active;
static uint32_t last_account,window_start,pcm_last,frame_start;
static uint64_t gap_phase[OMF_LOOP_COUNT],frame_total;
static gap_stat worst_gap;
static uint32_t frames,frame_max,frame_bins[6],long_gaps,queue_min=UINT32_MAX,read_bytes;
static int configured_limit;
static bool configured_vsync;

static void account(uint32_t now) {
    if(!started){started=true;last_account=window_start=now;return;}
    uint32_t dt=now-last_account;
    phases[current].us+=dt;
    if(pcm_seen)gap_phase[current]+=dt;
    last_account=now;
}
omf_loop_scope omf_loop_begin(omf_loop_phase phase) {
    uint32_t now=time_us_32();account(now);
    omf_loop_scope scope={current,phase,now};
    current=phase;++phases[phase].calls;
#ifdef OMF_FAULT_DIAG
    omf_fault_mark((uint32_t)phase);
#endif
    return scope;
}
void omf_loop_end(omf_loop_scope *scope) {
    uint32_t now=time_us_32();account(now);
    uint32_t wall=now-scope->start;
    if(wall>phases[scope->phase].max_wall)phases[scope->phase].max_wall=wall;
    current=scope->previous;
#ifdef OMF_FAULT_DIAG
    omf_fault_mark((uint32_t)current);
#endif
}
omf_loop_phase omf_loop_wait_phase(void) {
    return current==OMF_LOOP_LIMIT || current==OMF_LOOP_HIT ? current : OMF_LOOP_WAIT;
}
void omf_loop_frame_begin(void){frame_start=time_us_32();frame_active=true;}
void omf_loop_frame_end(void) {
    if(!frame_active)return;
    uint32_t elapsed=time_us_32()-frame_start;frame_active=false;
    ++frames;frame_total+=elapsed;if(elapsed>frame_max)frame_max=elapsed;
    unsigned bin=elapsed<=17000?0:elapsed<=20000?1:elapsed<=33333?2:elapsed<=50000?3:elapsed<=100000?4:5;
    ++frame_bins[bin];
}
void omf_loop_pcm(uint32_t now) {
    account(now);
    if(pcm_seen){
        uint32_t elapsed=now-pcm_last;
        if(elapsed>50000)++long_gaps;
        if(elapsed>worst_gap.us){
            worst_gap.us=elapsed;worst_gap.end=now;worst_gap.open=false;
            memcpy(worst_gap.phase,gap_phase,sizeof gap_phase);
        }
    }
    pcm_seen=true;pcm_last=now;memset(gap_phase,0,sizeof gap_phase);
}
void omf_loop_queue(unsigned level){if(level<queue_min)queue_min=level;}
void omf_loop_read(unsigned bytes){read_bytes+=bytes;}
void omf_loop_config(int limit,bool vsync){configured_limit=limit;configured_vsync=vsync;}

void omf_loop_report(void) {
#ifdef OMF_FILE_DIAG
    extern void omf_file_diag_report(void);
    omf_file_diag_report();
#endif
#ifdef OMF_SPRITE_AUDIO
    extern void omf_sprite_audio_report(void);
    omf_sprite_audio_report();
#endif
    uint32_t now=time_us_32();account(now);
    /* Snapshot before printing; printing is charged to the next window. A scope
     * or PCM interval may cross reports: do not clear the active phase or gap. */
    loop_stat saved[OMF_LOOP_COUNT];memcpy(saved,phases,sizeof saved);
    gap_stat gap=worst_gap;
    if(pcm_seen && now-pcm_last>gap.us){
        gap.us=now-pcm_last;gap.end=now;gap.open=true;
        memcpy(gap.phase,gap_phase,sizeof gap_phase);
    }
    uint32_t duration=now-window_start,n=frames,max=frame_max,bins[6],long_n=long_gaps;
    uint32_t minq=queue_min,bytes=read_bytes;
    uint64_t total=frame_total;
    memcpy(bins,frame_bins,sizeof bins);
    memset(phases,0,sizeof phases);memset(&worst_gap,0,sizeof worst_gap);
    memset(frame_bins,0,sizeof frame_bins);frames=frame_max=long_gaps=read_bytes=0;frame_total=0;
    queue_min=UINT32_MAX;window_start=now;
    printf("OMF_LOOP window_us=%"PRIu32" loops=%"PRIu32" avg_us=%"PRIu64" max_us=%"PRIu32" limit=%d vsync=%u pcm_min=%"PRIu32" read_bytes=%"PRIu32"\n",
           duration,n,n?total/n:0,max,configured_limit,(unsigned)configured_vsync,minq,bytes);
    printf("OMF_LOOP_HIST le17=%"PRIu32" le20=%"PRIu32" le33=%"PRIu32" le50=%"PRIu32" le100=%"PRIu32" gt100=%"PRIu32"\n",
           bins[0],bins[1],bins[2],bins[3],bins[4],bins[5]);
    for(unsigned p=0;p<OMF_LOOP_COUNT;p++)
        printf("OMF_LOOP_PHASE name=%s us=%"PRIu64" n=%"PRIu32" max_wall_us=%"PRIu32"\n",
               names[p],saved[p].us,saved[p].calls,saved[p].max_wall);
#ifdef OMF_DYNAMIC_DIAG
    uint64_t dynamic_total=saved[OMF_LOOP_DYNAMIC].us;
    for(unsigned p=OMF_DYN_CTRL;p<OMF_LOOP_COUNT;p++)dynamic_total+=saved[p].us;
    printf("OMF_DYN_DIAG active=1 total_us=%"PRIu64" remainder_us=%"PRIu64"\n",
           dynamic_total,saved[OMF_LOOP_DYNAMIC].us);
#endif
    printf("OMF_LOOP_GAP end_us=%"PRIu32" gap_us=%"PRIu32" open=%u over50=%"PRIu32"\n",
           gap.end,gap.us,(unsigned)gap.open,long_n);
    for(unsigned group=0;group<OMF_LOOP_COUNT/5;group++){
        unsigned p=group*5;
        printf("OMF_LOOP_GAP_PART group=%u %s=%"PRIu64" %s=%"PRIu64" %s=%"PRIu64" %s=%"PRIu64" %s=%"PRIu64"\n",
               group,names[p],gap.phase[p],names[p+1],gap.phase[p+1],names[p+2],gap.phase[p+2],names[p+3],gap.phase[p+3],names[p+4],gap.phase[p+4]);
    }
}
