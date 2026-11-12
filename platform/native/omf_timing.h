/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OMF_TIMING_H
#define OMF_TIMING_H
#include "pico/time.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    OMF_TIMING_DRAW,
    OMF_TIMING_VIDEO_SUBMIT,
    OMF_TIMING_MUSIC,
    OMF_TIMING_MIX,
    OMF_TIMING_PCM_SUBMIT,
    OMF_TIMING_SCENE,
    OMF_TIMING_PALETTE,
    OMF_TIMING_RENDER_COPY,
#ifdef OMF_SCENE_TIMING
    OMF_TIMING_BACKGROUND,
    OMF_TIMING_SPRITES,
    OMF_TIMING_SHADOWS,
    OMF_TIMING_HUD,
#endif
    OMF_TIMING_COUNT
} omf_timing_phase;
void omf_timing_draw_begin(void);
uint32_t omf_timing_draw_end(void);
void omf_timing_scene_end(void);
#ifdef OMF_SCENE_TIMING
void omf_timing_scene_part(omf_timing_phase phase, uint32_t start);
#endif
void omf_timing_add(omf_timing_phase phase, uint32_t start);
void omf_timing_pcm_submitted(uint32_t start);
void omf_timing_report(void);
#ifdef OMF_PALETTE_FAST
void omf_timing_palette_fast(uint32_t hits, uint32_t pixels);
#endif
#ifdef OMF_FAST_VIDEO
void omf_timing_background(bool hit, uint32_t restored_pixels);
#endif
#ifdef __cplusplus
}
#endif
#endif
