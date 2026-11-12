#include "video/surface.h"
#include "utils/allocator.h"
#include "utils/miscmath.h"
#include <stdlib.h>
#ifdef OMF_SURFACE_NOZERO
#include <limits.h>
#endif
#ifdef OMF_LOAD_DIAG
#include "pico/time.h"
#include <stdio.h>
#include <inttypes.h>
static uint64_t load_alloc_us,load_expand_us,load_pixels;
static uint32_t load_calls,load_alloc_max,load_expand_max;
void omf_load_surface_report(void){
    printf("OMF_LOAD_SURFACE n=%lu pixels=%"PRIu64" alloc_us=%"PRIu64" alloc_max_us=%lu expand_us=%"PRIu64" expand_max_us=%lu\n",
           (unsigned long)load_calls,load_pixels,load_alloc_us,(unsigned long)load_alloc_max,
           load_expand_us,(unsigned long)load_expand_max);
    load_alloc_us=load_expand_us=load_pixels=0;
    load_calls=load_alloc_max=load_expand_max=0;
}
#endif

// Each surface is tagged with a unique key. This is then used for texture atlas.
// This keeps track of the last index used.
static unsigned int guid = 0;

void surface_create(surface *sur, int w, int h) {
    sur->data = omf_calloc(w * h, sizeof(vga_pixel));
    sur->guid = guid++;
    sur->w = w;
    sur->h = h;
    sur->render_w = w;
    sur->render_h = h;
    sur->transparent = 0;
}

void surface_create_from_data(surface *sur, int w, int h, const unsigned char *src) {
#ifdef OMF_LOAD_DIAG
    uint32_t start=time_us_32();
#endif
#ifdef OMF_SURFACE_NOZERO
    /* This loop overwrites every pixel before returning. Keep the original
     * allocator path for empty/invalid dimensions; do not change other callers. */
    if(w > 0 && h > 0 && w <= INT_MAX / h &&
       (size_t)w * (size_t)h <= SIZE_MAX / sizeof(vga_pixel)) {
        sur->data = omf_malloc((size_t)w * (size_t)h * sizeof(vga_pixel));
        sur->guid = guid++;
        sur->w = w;
        sur->h = h;
        sur->render_w = w;
        sur->render_h = h;
        sur->transparent = 0;
    } else {
        surface_create(sur, w, h);
    }
#else
    surface_create(sur, w, h);
#endif
#ifdef OMF_LOAD_DIAG
    uint32_t alloc=time_us_32()-start;
    start=time_us_32();
#endif
    for(int i = 0; i < w * h; i++) {
        sur->data[i] = src[i];
    }
#ifdef OMF_LOAD_DIAG
    uint32_t expand=time_us_32()-start;
    load_calls++;load_pixels+=(uint64_t)w*h;
    load_alloc_us+=alloc;load_expand_us+=expand;
    if(alloc>load_alloc_max)load_alloc_max=alloc;
    if(expand>load_expand_max)load_expand_max=expand;
#endif
}

void surface_create_from_data_flip(surface *sur, int w, int h, const unsigned char *src) {
    surface_create(sur, w, h);
    for(int y = 0; y < h; y++) {
        const unsigned char *src_row = src + y * w;
        vga_pixel *dst_row = sur->data + (h - y - 1) * w;
        for(int x = 0; x < w; x++) {
            dst_row[x] = src_row[x];
        }
    }
}

void surface_create_from_vga(surface *sur, const sd_vga_image *src) {
    surface_create(sur, src->w, src->h);
    for(unsigned int i = 0; i < src->w * src->h; i++) {
        sur->data[i] = (unsigned char)src->data[i];
    }
    sur->transparent = -1;
}

void surface_create_from_surface(surface *sur, int w, int h, int src_x, int src_y, const surface *src) {
    surface_create(sur, w, h);
    surface_sub(sur, src, 0, 0, src_x, src_y, w, h, SUB_METHOD_NONE);
    sur->transparent = src->transparent;
}

void surface_create_from_image(surface *sur, image *img) {
    surface_create_from_data(sur, img->w, img->h, img->data);
    sur->transparent = -1;
}

void surface_create_from_flip_scale(surface *sur, const int w, const int h, const uint16_t *src, const float scale) {
    surface_create(sur, w, h);
    for(int y = 0; y < h; y++) {
        const uint16_t *src_row = src + y * w;
        vga_pixel *dst_row = sur->data + (h - y - 1) * w;
        for(int x = 0; x < w; x++) {
            dst_row[x] = (vga_pixel)(src_row[x] * scale + 0.5f);
        }
    }
    sur->transparent = -1;
}

void surface_set_pixel(surface *sur, int x, int y, vga_index color) {
    sur->data[x + y * sur->w] = color;
    sur->guid = guid++;
}

void surface_set_transparency(surface *sur, int index) {
    sur->transparent = index;
}

void surface_free(surface *sur) {
    omf_free(sur->data);
}

void surface_clear(surface *sur) {
    memset(sur->data, 0, sur->w * sur->h * sizeof(vga_pixel));
    sur->guid = guid++;
}

void surface_create_from(surface *dst, const surface *src) {
    surface_create(dst, src->w, src->h);
    memcpy(dst->data, src->data, src->w * src->h * sizeof(vga_pixel));
    dst->transparent = src->transparent;
}

void surface_multiply_decal(surface *src, const surface *decal, int dst_x, int dst_y) {
    for(int y = 0; y < decal->h; y++) {
        if((dst_y + y) >= src->h) {
            continue;
        }
        for(int x = 0; x < decal->w; x++) {
            if((dst_x + x) >= src->w) {
                continue;
            }
            const int src_offset = (dst_x + x + (dst_y + y) * src->w);
            const int decal_offset = (x + y * decal->w);
            if(src->data[src_offset] == 0) {
                continue;
            }
            if(decal->data[decal_offset] == 0) {
                continue;
            }
            const int color = src->data[src_offset] & 0xf0;
            int value = src->data[src_offset] & 0x0f;
            value = (value * decal->data[decal_offset]) >> 4;
            if(value > 15) {
                value = 15;
            }
            src->data[src_offset] = color | value;
        }
    }
    src->guid = guid++;
}

// Copies a an area of old surface to an entirely new surface
void surface_sub(surface *dst, const surface *src, int dst_x, int dst_y, int src_x, int src_y, int w, int h,
                 int method) {
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            const int src_offset = (src_x + x + (src_y + y) * src->w);
            int dst_offset;
            switch(method) {
                case SUB_METHOD_MIRROR:
                    dst_offset = (dst_x + (w - x - 1) + (dst_y + y) * dst->w);
                    break;
                default:
                    dst_offset = (dst_x + x + (dst_y + y) * dst->w);
                    break;
            }
            dst->data[dst_offset] = src->data[src_offset];
        }
    }
    dst->guid = guid++;
}

void surface_flatten_to_mask(surface *sur, uint8_t value) {
    for(int i = 0; i < sur->w * sur->h; i++) {
        const vga_index idx = sur->data[i];
        if(idx == sur->transparent) {
            continue;
        }
        sur->data[i] = value;
    }
    sur->guid = guid++;
}

void surface_convert_har_to_grayscale(surface *sur, uint8_t brightness) {
    for(int i = 0; i < sur->w * sur->h; i++) {
        const vga_index idx = sur->data[i];
        if(idx != sur->transparent && idx < 0x60) {
            sur->data[i] = 0xD0 + brightness * (idx % 0x10) / 0x0F;
        }
    }
    sur->guid = guid++;
}

void surface_compress_index_blocks(surface *sur, int range_start, int range_end, int block_size, int amount) {
    for(int i = 0; i < sur->w * sur->h; i++) {
        const vga_index idx = sur->data[i];
        if(idx >= range_start && idx < range_end) {
            const int real_start = idx - range_start;
            const int old_idx = real_start % block_size;
            const int new_idx = max2(0, old_idx - amount);
            sur->data[i] = idx - old_idx + new_idx;
        }
    }
    sur->guid = guid++;
}

void surface_compress_remap(surface *sur, int range_start, int range_end, int remap_to, int amount) {
    for(int i = 0; i < sur->w * sur->h; i++) {
        const vga_index idx = sur->data[i];
        if(idx >= range_start && idx < range_end) {
            const int real_start = idx - range_start;
            if(real_start - amount < range_start) {
                const int d = abs(real_start - amount);
                sur->data[i] = remap_to - d;
            }
        }
    }
    sur->guid = guid++;
}

static vga_index find_closest_gray(const vga_palette *pal, const vga_index range_start, const vga_index range_end,
                                   const int ref) {
    vga_index closest = 0;
    int closest_dist = 256;

    for(int i = range_start; i <= range_end; i++) {
        const uint8_t current = pal->colors[i].r;
        int dist = current - ref;
        if(dist < 0) {
            dist = -dist;
        }
        if(dist > closest_dist) {
            break;
        }
        if(dist < closest_dist) {
            closest_dist = dist;
            closest = i;
        }
    }

    return closest;
}

void surface_to_grayscale(const surface *src, surface *dst, const vga_palette *pal, const vga_index range_start,
                          const vga_index range_end, const int ignore_below) {
    vga_index mapping[VGA_PALETTE_SIZE];
    for(int i = 0; i < VGA_PALETTE_SIZE; i++) {
        if(i < ignore_below) {
            mapping[i] = i;
            continue;
        }
        const float r = pal->colors[i].r * 0.3;
        const float g = pal->colors[i].g * 0.59;
        const float b = pal->colors[i].b * 0.11;
        mapping[i] = find_closest_gray(pal, range_start, range_end, r + g + b);
    }

    surface_create(dst, src->w, src->h);
    for(int i = 0; i < src->w * src->h; i++) {
        const vga_index idx = src->data[i];
        if(idx >= 0 && idx < VGA_PALETTE_SIZE) {
            dst->data[i] = mapping[idx];
        }
    }
    surface_set_transparency(dst, -1);
}
