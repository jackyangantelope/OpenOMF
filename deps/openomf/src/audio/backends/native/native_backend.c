/* SPDX-License-Identifier: BSD-3-Clause
 * Portable stereo mixer: original PSM source plus three original DAT voices.
 * Keeps compact U8 source samples instead of expanded per-voice stereo PCM. */
#include "audio/backends/native/native_backend.h"
#include "utils/allocator.h"
#ifdef OMF_TIMING_DIAG
#include "omf_timing.h"
#endif
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint8_t *data;size_t length,position;
    unsigned frequency,phase;
    int volume,pan_left,pan_right,gain,fade_from,fade_to;
    uint32_t fade_length,fade_position;
    bool active;
} voice;
typedef struct {
    voice voices[SOUND_CHANNEL_COUNT];
    music_source music;
    unsigned rate,resampler;
    int sound_volume;
    float music_volume;
} native_audio;
static int clamp_int(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static float unit(float v){return v<0?0:v>1?1:v;}
static bool available(void){return true;}
static const char *name(void){return "Native PCM";}
static const char *description(void){return "Original PSM and DAT, portable stereo mixer";}
static unsigned rates(const audio_sample_rate **out){static const audio_sample_rate list[]={{44100,true,"44100Hz"}};*out=list;return 1;}
static void create(audio_backend *b){b->ctx=omf_calloc(1,sizeof(native_audio));}
static void destroy(audio_backend *b){omf_free(b->ctx);b->ctx=NULL;}
static void information(void *ctx,unsigned *rate,unsigned *channels,unsigned *resampler){
    native_audio *a=ctx;if(rate)*rate=a->rate;if(channels)*channels=2;if(resampler)*resampler=a->resampler;
}
static bool channel_valid(int i){return i>=0&&i<SOUND_CHANNEL_COUNT;}
static void pan(voice *v,int p){p=clamp_int(p,-100,100);v->pan_left=p>0?100-p:100;v->pan_right=p<0?100+p:100;}
static void voice_stop(voice *v){omf_free(v->data);memset(v,0,sizeof *v);}
static void fade(voice *v,int target,int ms,unsigned rate){
    v->fade_from=v->gain;v->fade_to=target;v->fade_position=0;
    v->fade_length=ms>0?(uint64_t)(unsigned)ms*rate/1000:0;
    if(!v->fade_length){v->gain=target;if(!target)v->active=false;}
}
static void render(void *context,int16_t *out,size_t frames){
    native_audio *a=context;memset(out,0,frames*4);
#ifdef OMF_TIMING_DIAG
    uint32_t start=time_us_32();
#endif
    music_source_render(&a->music,(char*)out,frames*4);
#ifdef OMF_TIMING_DIAG
    omf_timing_add(OMF_TIMING_MUSIC,start);
    start=time_us_32();
#endif
    for(size_t n=0;n<frames;n++){
        int left=out[n*2],right=out[n*2+1];
        for(int i=0;i<SOUND_CHANNEL_COUNT;i++){
            voice *v=&a->voices[i];if(!v->active)continue;
            if(v->fade_length){
                v->gain=v->fade_from+(int)((int64_t)(v->fade_to-v->fade_from)*++v->fade_position/v->fade_length);
                if(v->fade_position>=v->fade_length){v->gain=v->fade_to;v->fade_length=0;if(!v->gain)v->active=false;}
            }
            size_t next=v->position+1<v->length?v->position+1:v->position;
            int sample=((int)v->data[v->position]-128)*256;
            int next_sample=((int)v->data[next]-128)*256;
            sample+=(int)((int64_t)(next_sample-sample)*v->phase/a->rate);
            sample=(int)((int64_t)sample*v->volume*a->sound_volume*v->gain/(128LL*128*65536));
            left+=sample*v->pan_left/100;right+=sample*v->pan_right/100;
            uint64_t phase=(uint64_t)v->phase+v->frequency;
            v->position+=phase/a->rate;v->phase=phase%a->rate;
            if(v->position>=v->length)v->active=false;
        }
        out[n*2]=(int16_t)clamp_int(left,-32768,32767);out[n*2+1]=(int16_t)clamp_int(right,-32768,32767);
    }
#ifdef OMF_TIMING_DIAG
    omf_timing_add(OMF_TIMING_MIX,start);
#endif
}
static bool setup(void *ctx,unsigned rate,bool mono,int resampler,float music_vol,float sound_vol){
    (void)rate;(void)mono;native_audio *a=ctx;if(!a)return false;
    a->rate=44100;a->resampler=resampler;a->music_volume=unit(music_vol);a->sound_volume=(int)(unit(sound_vol)*128);
    return omf_pcm_open(a->rate,render,a);
}
static void close_context(void *ctx){
    native_audio *a=ctx;omf_pcm_close();music_source_close(&a->music);
    for(int i=0;i<SOUND_CHANNEL_COUNT;i++)voice_stop(&a->voices[i]);
}
static void sound_volume(void *ctx,float vol){omf_pcm_lock();((native_audio*)ctx)->sound_volume=(int)(unit(vol)*128);omf_pcm_unlock();}
static void music_volume(void *ctx,float vol){omf_pcm_lock();native_audio *a=ctx;a->music_volume=unit(vol);music_source_set_volume(&a->music,a->music_volume);omf_pcm_unlock();}
static bool play(void *ctx,int channel,const sound_source *src,int volume,int panning,int fade_in){
    native_audio *a=ctx;if(!channel_valid(channel)||!src||!src->buf||!src->len||src->freq<=0)return false;
    uint8_t *copy=omf_malloc(src->len);if(!copy)return false;memcpy(copy,src->buf,src->len);
    omf_pcm_lock();voice *v=&a->voices[channel];voice_stop(v);
    v->data=copy;v->length=src->len;v->frequency=src->freq;v->volume=clamp_int(volume,0,128);v->active=true;
    pan(v,panning);v->gain=fade_in>0?0:65536;if(fade_in>0)fade(v,65536,fade_in,a->rate);
    omf_pcm_unlock();return true;
}
static bool is_playing(void *ctx,int channel){
    if(!channel_valid(channel))return false;
    omf_pcm_lock();
    bool active=((native_audio*)ctx)->voices[channel].active;
    omf_pcm_unlock();
    return active;
}
static void stop(void *ctx,int channel){if(channel_valid(channel)){omf_pcm_lock();voice_stop(&((native_audio*)ctx)->voices[channel]);omf_pcm_unlock();}}
static void fade_out(void *ctx,int channel,int ms){if(channel_valid(channel)){omf_pcm_lock();native_audio *a=ctx;fade(&a->voices[channel],0,ms,a->rate);omf_pcm_unlock();}}
static void set_pan(void *ctx,int channel,int p){if(channel_valid(channel)){omf_pcm_lock();pan(&((native_audio*)ctx)->voices[channel],p);omf_pcm_unlock();}}
static void play_music(void *ctx,const music_source *src){
    omf_pcm_lock();native_audio *a=ctx;music_source_close(&a->music);a->music=*src;music_source_set_volume(&a->music,a->music_volume);omf_pcm_unlock();
}
static void stop_music(void *ctx){omf_pcm_lock();music_source_close(&((native_audio*)ctx)->music);omf_pcm_unlock();}
void native_audio_backend_set_callbacks(audio_backend *b){
    memset(b,0,sizeof *b);b->is_available=available;b->get_name=name;b->get_description=description;b->get_sample_rates=rates;
    b->create=create;b->destroy=destroy;b->get_info=information;b->setup_context=setup;b->close_context=close_context;
    b->set_music_volume=music_volume;b->set_sound_volume=sound_volume;b->play_pcm_sound=play;b->is_channel_playing=is_playing;
    b->stop_channel=stop;b->fade_out_channel=fade_out;b->set_channel_panning=set_pan;b->play_music=play_music;b->stop_music=stop_music;
}
