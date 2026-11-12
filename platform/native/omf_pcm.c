/* SPDX-License-Identifier: BSD-3-Clause
 * Producer runs on main core; only completed PCM crosses to HDMI consumer. */
#include "audio/backends/native/native_backend.h"
#include "native_hal.h"
#include "omf_loop_diag.h"
#ifdef OMF_TIMING_DIAG
#include "omf_timing.h"
#endif
#include <assert.h>
static omf_pcm_callback callback;
static void *context;
static unsigned locked;
static bool active;
bool omf_pcm_open(unsigned rate,omf_pcm_callback render,void *ctx){
    if(rate!=44100||!render)return false;callback=render;context=ctx;return true;
}
void omf_pcm_close(void){callback=NULL;context=NULL;}
void omf_pcm_lock(void){locked++;}
void omf_pcm_unlock(void){assert(locked);locked--;}
void omf_native_audio_service(void){
    if(!callback||locked||active)return;
    OMF_LOOP_SCOPE(OMF_LOOP_AUDIO);
#ifdef OMF_LOOP_DIAG
    omf_loop_queue(native_audio_queued());
#endif
    active=true;int16_t pcm[441*2];
    /* Return to game even if decoding cannot keep up with the live consumer. */
    for(unsigned block=0;block<8&&native_audio_queued()<3528;block++){
        callback(context,pcm,441);
#ifdef OMF_TIMING_DIAG
        uint32_t start=time_us_32();
#endif
        native_audio_submit(pcm,441);
#ifdef OMF_TIMING_DIAG
        omf_timing_pcm_submitted(start);
#endif
    }
    active=false;
}
