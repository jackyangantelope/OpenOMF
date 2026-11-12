/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum native_game_kind { NATIVE_GAME_XU4, NATIVE_GAME_OMF, NATIVE_GAME_UQM };
#define NATIVE_GAME_LIMIT 32
typedef struct native_game_list {
    unsigned count;
    char folders[NATIVE_GAME_LIMIT][128]; /* "." denotes the existing root dataset */
} native_game_list;
bool native_game_scan(enum native_game_kind kind,const char *root,native_game_list *list);
bool native_game_path(const native_game_list *list,unsigned index,const char *root,char *out,size_t capacity);
/* Uses the shared RP2350 drawing/input API. Always waits for an explicit choice. */
bool native_game_select(enum native_game_kind kind,const char *root,char *out,size_t capacity);
#ifdef __cplusplus
}
#endif
