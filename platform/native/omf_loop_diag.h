/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OMF_LOOP_DIAG_H
#define OMF_LOOP_DIAG_H
#ifdef OMF_LOOP_DIAG
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    OMF_LOOP_OTHER, OMF_LOOP_INPUT, OMF_LOOP_KEYS, OMF_LOOP_STATIC,
    OMF_LOOP_DYNAMIC, OMF_LOOP_STATEPAL, OMF_LOOP_DRAW, OMF_LOOP_OUTPUT,
    OMF_LOOP_AUDIO, OMF_LOOP_LIMIT, OMF_LOOP_HIT, OMF_LOOP_WAIT,
    OMF_LOOP_LOG, OMF_LOOP_FILE, OMF_LOOP_LOAD,
#ifdef OMF_DYNAMIC_DIAG
    OMF_DYN_CTRL, OMF_DYN_SCENE, OMF_DYN_INPUT, OMF_DYN_CLEAN,
    OMF_DYN_ANIM, OMF_DYN_MOVE, OMF_DYN_COLLIDE, OMF_DYN_APPLY,
    OMF_DYN_EVENTS, OMF_DYN_SHAKE,
#endif
    OMF_LOOP_COUNT
} omf_loop_phase;
typedef struct {omf_loop_phase previous, phase; uint32_t start;} omf_loop_scope;
omf_loop_scope omf_loop_begin(omf_loop_phase phase);
void omf_loop_end(omf_loop_scope *scope);
omf_loop_phase omf_loop_wait_phase(void);
void omf_loop_frame_begin(void);
void omf_loop_frame_end(void);
void omf_loop_pcm(uint32_t now);
void omf_loop_queue(unsigned frames);
void omf_loop_read(unsigned bytes);
void omf_loop_config(int limit, bool vsync);
void omf_loop_report(void);
#ifdef __cplusplus
}
#endif
#define OMF_LOOP_JOIN_(a,b) a##b
#define OMF_LOOP_JOIN(a,b) OMF_LOOP_JOIN_(a,b)
/* GCC cleanup restores the parent on every normal return / goto out of scope. */
#define OMF_LOOP_SCOPE(phase) omf_loop_scope OMF_LOOP_JOIN(omf_scope_,__LINE__) \
    __attribute__((cleanup(omf_loop_end))) = omf_loop_begin(phase)
#else
#define OMF_LOOP_SCOPE(phase) ((void)0)
#endif
#ifdef OMF_DYNAMIC_DIAG
#define OMF_DYN_SCOPE(phase) OMF_LOOP_SCOPE(phase)
#else
#define OMF_DYN_SCOPE(phase) ((void)0)
#endif
#endif
