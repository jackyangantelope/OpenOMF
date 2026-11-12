/* SPDX-License-Identifier: BSD-3-Clause */
#include "omf_timing.h"
#include "omf_loop_diag.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t total;
    uint32_t max, count;
} timing_stat;

/* All probes and the reporter run on the OpenOMF main core. */
static timing_stat stats[OMF_TIMING_COUNT];
static uint32_t draw_start, last_pcm, max_pcm_gap, window_start;
static bool drawing, have_pcm, have_window;
#ifdef OMF_LOOP_DIAG
static omf_loop_scope draw_scope;
#endif
#ifdef OMF_SCENE_TIMING
static uint32_t scene_parts[OMF_TIMING_HUD - OMF_TIMING_BACKGROUND + 1];
#endif

static void sample(omf_timing_phase phase, uint32_t elapsed) {
    timing_stat *stat = &stats[phase];
    stat->total += elapsed;
    if (elapsed > stat->max) stat->max = elapsed;
    stat->count++;
}

void omf_timing_draw_begin(void) {
#ifdef OMF_LOOP_DIAG
    draw_scope=omf_loop_begin(OMF_LOOP_DRAW);
#endif
    draw_start = time_us_32();
    drawing = true;
#ifdef OMF_SCENE_TIMING
    memset(scene_parts, 0, sizeof scene_parts);
#endif
}

void omf_timing_scene_end(void) {
    if (drawing) omf_timing_add(OMF_TIMING_SCENE, draw_start);
#ifdef OMF_SCENE_TIMING
    if (drawing) {
        for (int phase = OMF_TIMING_BACKGROUND; phase <= OMF_TIMING_HUD; phase++)
            sample((omf_timing_phase)phase, scene_parts[phase - OMF_TIMING_BACKGROUND]);
    }
#endif
}
#ifdef OMF_SCENE_TIMING
void omf_timing_scene_part(omf_timing_phase phase, uint32_t start) {
    scene_parts[phase - OMF_TIMING_BACKGROUND] += time_us_32() - start;
}
#endif

uint32_t omf_timing_draw_end(void) {
    uint32_t now = time_us_32();
    if (drawing) sample(OMF_TIMING_DRAW, now - draw_start);
#ifdef OMF_LOOP_DIAG
    if (drawing) omf_loop_end(&draw_scope);
#endif
    drawing = false;
    return now;
}

void omf_timing_add(omf_timing_phase phase, uint32_t start) {
    sample(phase, time_us_32() - start);
}

void omf_timing_pcm_submitted(uint32_t start) {
    uint32_t now = time_us_32();
    sample(OMF_TIMING_PCM_SUBMIT, now - start);
#ifdef OMF_LOOP_DIAG
    omf_loop_pcm(now);
#endif
    if (have_pcm && now - last_pcm > max_pcm_gap) max_pcm_gap = now - last_pcm;
    last_pcm = now;
    have_pcm = true;
}

static uint32_t average(omf_timing_phase phase) {
    const timing_stat *stat = &stats[phase];
    return stat->count ? (uint32_t)(stat->total / stat->count) : 0;
}

#ifdef OMF_FAST_VIDEO
static uint32_t background_hits, background_misses, background_pixels;
#ifdef OMF_ROW_DIRTY
static uint32_t background_pixels_max;
#endif

void omf_timing_background(bool hit, uint32_t restored_pixels) {
    if (hit) background_hits++;
    else background_misses++;
    background_pixels += restored_pixels;
#ifdef OMF_ROW_DIRTY
    if (restored_pixels > background_pixels_max) background_pixels_max = restored_pixels;
#endif
}
#endif

#ifdef OMF_PALETTE_FAST
static uint32_t palette_hits, palette_pixels;

void omf_timing_palette_fast(uint32_t hits, uint32_t pixels) {
    palette_hits += hits;
    palette_pixels += pixels;
}
#endif

void omf_timing_report(void) {
    uint32_t now = time_us_32();
    uint32_t elapsed = now - window_start;
    if (have_window && elapsed) {
        uint32_t frames = stats[OMF_TIMING_VIDEO_SUBMIT].count;
        uint32_t fps_milli = (uint32_t)(UINT64_C(1000000000) * frames / elapsed);
        uint32_t open_gap = have_pcm ? now - last_pcm : 0;
        uint32_t pcm_gap = open_gap > max_pcm_gap ? open_gap : max_pcm_gap;
        printf("OMF_TIMING_VIDEO window_us=%" PRIu32 " frames=%" PRIu32
               " fps_milli=%" PRIu32 " draw_n=%" PRIu32
               " draw_avg_us=%" PRIu32 " draw_max_us=%" PRIu32
               " submit_avg_us=%" PRIu32 " submit_max_us=%" PRIu32 "\n",
               elapsed, frames, fps_milli, stats[OMF_TIMING_DRAW].count,
               average(OMF_TIMING_DRAW), stats[OMF_TIMING_DRAW].max,
               average(OMF_TIMING_VIDEO_SUBMIT), stats[OMF_TIMING_VIDEO_SUBMIT].max);
        printf("OMF_TIMING_DRAW scene_n=%" PRIu32
               " scene_avg_us=%" PRIu32 " scene_max_us=%" PRIu32
               " palette_n=%" PRIu32 " palette_avg_us=%" PRIu32 " palette_max_us=%" PRIu32
               " render_copy_n=%" PRIu32 " render_copy_avg_us=%" PRIu32 " render_copy_max_us=%" PRIu32 "\n",
               stats[OMF_TIMING_SCENE].count, average(OMF_TIMING_SCENE), stats[OMF_TIMING_SCENE].max,
               stats[OMF_TIMING_PALETTE].count, average(OMF_TIMING_PALETTE), stats[OMF_TIMING_PALETTE].max,
               stats[OMF_TIMING_RENDER_COPY].count, average(OMF_TIMING_RENDER_COPY), stats[OMF_TIMING_RENDER_COPY].max);
#ifdef OMF_SCENE_TIMING
        printf("OMF_TIMING_SCENE frames=%" PRIu32
               " bg_avg_us=%" PRIu32 " bg_max_us=%" PRIu32
               " sprite_avg_us=%" PRIu32 " sprite_max_us=%" PRIu32
               " shadow_avg_us=%" PRIu32 " shadow_max_us=%" PRIu32
               " hud_avg_us=%" PRIu32 " hud_max_us=%" PRIu32 "\n",
               stats[OMF_TIMING_BACKGROUND].count,
               average(OMF_TIMING_BACKGROUND), stats[OMF_TIMING_BACKGROUND].max,
               average(OMF_TIMING_SPRITES), stats[OMF_TIMING_SPRITES].max,
               average(OMF_TIMING_SHADOWS), stats[OMF_TIMING_SHADOWS].max,
               average(OMF_TIMING_HUD), stats[OMF_TIMING_HUD].max);
#endif
        printf("OMF_TIMING_AUDIO blocks=%" PRIu32
               " music_avg_us=%" PRIu32 " music_max_us=%" PRIu32
               " mix_avg_us=%" PRIu32 " mix_max_us=%" PRIu32
               " pcm_submit_avg_us=%" PRIu32 " pcm_submit_max_us=%" PRIu32
               " pcm_gap_max_us=%" PRIu32 "\n",
               stats[OMF_TIMING_PCM_SUBMIT].count,
               average(OMF_TIMING_MUSIC), stats[OMF_TIMING_MUSIC].max,
               average(OMF_TIMING_MIX), stats[OMF_TIMING_MIX].max,
               average(OMF_TIMING_PCM_SUBMIT), stats[OMF_TIMING_PCM_SUBMIT].max, pcm_gap);
#ifdef OMF_PALETTE_FAST
        uint32_t hit_bp = palette_pixels ? (uint32_t)((uint64_t)palette_hits * 10000 / palette_pixels) : 0;
        printf("OMF_PALETTE_FAST hits=%" PRIu32 " pixels=%" PRIu32 " hit_bp=%" PRIu32 "\n",
               palette_hits, palette_pixels, hit_bp);
#endif
#ifdef OMF_RGB555_DIRECT
        printf("OMF_RGB555_DIRECT output_avg_us=%" PRIu32 " output_max_us=%" PRIu32 "\n",
               average(OMF_TIMING_VIDEO_SUBMIT), stats[OMF_TIMING_VIDEO_SUBMIT].max);
#endif
#ifdef OMF_FAST_VIDEO
        printf("OMF_FAST_VIDEO width=160 height=100 bg_hits=%" PRIu32
               " bg_misses=%" PRIu32 " bg_restore_pixels=%" PRIu32 "\n",
               background_hits, background_misses, background_pixels);
#ifdef OMF_ROW_DIRTY
        printf("OMF_ROW_DIRTY frames=%" PRIu32 " bg_restore_avg_px=%" PRIu32
               " bg_restore_max_px=%" PRIu32 "\n",
               frames, frames ? background_pixels / frames : 0, background_pixels_max);
#endif
#endif
    }
#ifdef OMF_FAST_VIDEO
    background_hits = background_misses = background_pixels = 0;
#ifdef OMF_ROW_DIRTY
    background_pixels_max = 0;
#endif
#endif
#ifdef OMF_PALETTE_FAST
    palette_hits = palette_pixels = 0;
#endif
    have_window = true;
    window_start = now;
    memset(stats, 0, sizeof stats);
    max_pcm_gap = 0;
}
