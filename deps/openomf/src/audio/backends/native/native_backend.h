/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include "audio/backends/audio_backend.h"
#include <stdint.h>
void native_audio_backend_set_callbacks(audio_backend *backend);
typedef void (*omf_pcm_callback)(void *context,int16_t *output,size_t frames);
bool omf_pcm_open(unsigned rate,omf_pcm_callback callback,void *context);
void omf_pcm_close(void);
void omf_pcm_lock(void);
void omf_pcm_unlock(void);
