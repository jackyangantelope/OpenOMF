/* SPDX-License-Identifier: BSD-3-Clause */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t native_millis(void);
void native_sleep(unsigned ms);
/* Source pixels are RGBA bytes, unscaled 320x200. */
void native_present(const uint32_t *pixels, unsigned width, unsigned height, int offset);
/* HID usage + modifiers; returns one newly pressed/repeated USB key. */
int native_key(uint8_t *usage, uint8_t *modifiers);
void native_key_repeat(unsigned delay_ms, unsigned interval_ms);
uint16_t native_pad(void);
/* Current USB HID boot-keyboard state, including modifier usages 224..231. */
void native_keys(uint8_t state[256]);
int native_quit(void);
size_t native_audio_queued(void);
void native_audio_submit(const int16_t *stereo, size_t frames);
void xu4_native_audio_service(void);
#ifdef __cplusplus
}
#endif
