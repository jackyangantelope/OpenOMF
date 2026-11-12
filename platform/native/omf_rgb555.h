/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OMF_RGB555_H
#define OMF_RGB555_H

#include <stdint.h>
#include <string.h>

static inline uint16_t omf_argb_to_rgb555(uint32_t argb) {
    return (uint16_t)(((argb >> 9) & 0x7c00u) |
                      ((argb >> 6) & 0x03e0u) | ((argb >> 3) & 0x001fu));
}

/* Expand a 160x100 ARGB frame to the 320x200 active part of the RP2350 buffer. */
static inline void omf_write_rgb555(uint16_t *out, const uint32_t *argb, int dx, int dy) {
    if (dx == 0 && dy == 0) {
        for (int y = 0; y < 100; y++) {
            uint16_t *row = out + y * 640;
            for (int x = 0; x < 160; x++) {
                uint16_t color = omf_argb_to_rgb555(argb[y * 160 + x]);
                row[2*x] = row[2*x+1] = row[320+2*x] = row[321+2*x] = color;
            }
        }
        return;
    }
    if (dx <= -320 || dx >= 320 || dy <= -200 || dy >= 200) {
        memset(out, 0, 320 * 200 * sizeof *out);
        return;
    }

    const int left = dx > 0 ? dx : 0;
    const int right = dx < 0 ? 320 + dx : 320;
    for (int y = 0; y < 200; y++) {
        uint16_t *row = out + y * 320;
        int sy = y - dy;
        if (sy < 0 || sy >= 200) {
            memset(row, 0, 320 * sizeof *row);
            continue;
        }
        const uint32_t *source = argb + (sy / 2) * 160;
        memset(row, 0, left * sizeof *row);
        for (int x = left; x < right; x++) {
            row[x] = omf_argb_to_rgb555(source[(x - dx) / 2]);
        }
        memset(row + right, 0, (320 - right) * sizeof *row);
    }
}

#endif
