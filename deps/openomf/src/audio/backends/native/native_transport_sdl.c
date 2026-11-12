/* SPDX-License-Identifier: BSD-3-Clause
 * Desktop transport for testing the portable PCM mixer. */
#ifndef OMF_PICO
#include "audio/backends/native/native_backend.h"
#include <SDL.h>
static SDL_AudioDeviceID device;
static omf_pcm_callback callback;
static void *context;
static void fill(void *unused,Uint8 *out,int bytes){(void)unused;callback(context,(int16_t*)out,bytes/4);}
bool omf_pcm_open(unsigned rate,omf_pcm_callback render,void *ctx){
    if(SDL_InitSubSystem(SDL_INIT_AUDIO))return false;
    callback=render;context=ctx;SDL_AudioSpec spec={0};spec.freq=rate;spec.format=AUDIO_S16SYS;
    spec.channels=2;spec.samples=512;spec.callback=fill;
    device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
    if(!device){SDL_QuitSubSystem(SDL_INIT_AUDIO);return false;}
    SDL_PauseAudioDevice(device,0);return true;
}
void omf_pcm_close(void){if(device)SDL_CloseAudioDevice(device);device=0;SDL_QuitSubSystem(SDL_INIT_AUDIO);}
void omf_pcm_lock(void){if(device)SDL_LockAudioDevice(device);}
void omf_pcm_unlock(void){if(device)SDL_UnlockAudioDevice(device);}
#endif
