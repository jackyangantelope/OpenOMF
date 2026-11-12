/* SPDX-License-Identifier: BSD-3-Clause
 * Native 320x200 CPU renderer. Preserves indexed VGA blending semantics from
 * the upstream palette.frag/rgba.frag pipeline, without a texture atlas or GL.
 * SDL only presents the finished pixels; rasterization has no GPU dependency.
 */
#include "video/renderers/software/software_renderer.h"
#ifdef OMF_LOOP_DIAG
#include "omf_loop_diag.h"
#else
#define OMF_LOOP_SCOPE(phase) ((void)0)
#endif
#include "video/enums.h"
#include "video/vga_state.h"
#include "utils/allocator.h"
#include "utils/log.h"
#ifdef OMF_TIMING_DIAG
#include "omf_timing.h"
#endif
#include <SDL.h>
#include <math.h>
#include <string.h>
#ifdef OMF_RGB555_DIRECT
extern void omf_present_half_argb(const uint32_t *pixels, int dx, int dy);
#endif

#define WIDTH 320
#define HEIGHT 200
#ifdef OMF_FAST_VIDEO
#define HALF_WIDTH (WIDTH / 2)
#define HALF_HEIGHT (HEIGHT / 2)
#endif
typedef struct { uint16_t index, remap, tint, add; } sw_pixel;
typedef struct {
#ifdef OMF_SRAM_WORK
#ifdef OMF_CAPTURE_INDEX
    /* Mutually exclusive area/normal storage. Keep full allocation for
     * secondary contexts using the normal fallback renderer. */
    union {
        sw_pixel full_pixels[WIDTH * HEIGHT];
        uint16_t capture_indices[WIDTH * HEIGHT];
    };
#else
    sw_pixel full_pixels[WIDTH * HEIGHT];
#endif
    sw_pixel *pixels, *screen_pixels;
#else
    sw_pixel pixels[WIDTH * HEIGHT];
#endif
#ifdef OMF_SRAM_ARGB
    uint32_t full_rgba[WIDTH * HEIGHT];
    uint32_t *rgba, *screen_rgba;
#else
    uint32_t rgba[WIDTH * HEIGHT];
#endif
#ifdef OMF_FAST_VIDEO
    sw_pixel background[HALF_WIDTH * HALF_HEIGHT];
    const vga_pixel *background_source;
#ifdef OMF_ROW_DIRTY
    uint8_t dirty_left[HALF_HEIGHT], dirty_right[HALF_HEIGHT];
#else
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
#endif
    bool area_active, first_draw, cached_frame, pending_restore;
#endif
    vga_palette *palette;
    vga_remap_tables *remaps;
    SDL_Window *window;
    SDL_Renderer *presenter;
    SDL_Texture *texture;
    SDL_Rect area;
    int width, height, aspect, move_x, move_y, limit;
    bool fullscreen, vsync;
    unsigned options;
    Uint32 last_tick;
    video_screenshot_signal screenshot;
} sw_context;

static int bounded(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static bool available(void) { return true; }
static const char *name(void) { return "Software"; }
static const char *description(void) { return "Native indexed VGA CPU renderer"; }
#ifdef OMF_SRAM_WORK
/* One native renderer owns this ordinary .bss allocation in on-chip SRAM. */
static sw_pixel sram_pixels[HALF_WIDTH * HALF_HEIGHT];
static sw_context *sram_owner;
#endif
#ifdef OMF_SRAM_ARGB
#ifndef OMF_SRAM_WORK
#error OMF_SRAM_ARGB requires OMF_SRAM_WORK
#endif
static uint32_t sram_rgba[HALF_WIDTH * HALF_HEIGHT];
#endif

static void create(renderer *r) {
    r->ctx = omf_calloc(1, sizeof(sw_context));
#ifdef OMF_SRAM_WORK
    sw_context *c = r->ctx;
    c->screen_pixels = c->full_pixels;
    if (!sram_owner) {
        sram_owner = c;
        c->screen_pixels = sram_pixels;
    }
    c->pixels = c->screen_pixels;
    log_info("OMF_SRAM_WORK active=%u bytes=%u buffer=%p readback=%p",
             (unsigned)(sram_owner == c), (unsigned)sizeof sram_pixels,
             (void *)c->screen_pixels, (void *)c->full_pixels);
#ifdef OMF_SRAM_ARGB
    c->screen_rgba = sram_owner == c ? sram_rgba : c->full_rgba;
    c->rgba = c->screen_rgba;
    log_info("OMF_SRAM_ARGB active=%u bytes=%u buffer=%p readback=%p",
             (unsigned)(sram_owner == c), (unsigned)sizeof sram_rgba,
             (void *)c->screen_rgba, (void *)c->full_rgba);
#endif
#endif
}
static void destroy(renderer *r) {
#ifdef OMF_SRAM_WORK
    if (sram_owner == r->ctx) sram_owner = NULL;
#endif
    omf_free(r->ctx);
}
static void close_context(void *data) {
    sw_context *c=data;
    SDL_DestroyTexture(c->texture); c->texture=NULL;
    SDL_DestroyRenderer(c->presenter); c->presenter=NULL;
    SDL_DestroyWindow(c->window); c->window=NULL;
}
static bool setup(void *data, int w, int h, bool fs, bool vs, int aspect, int limit, int scale, int mode) {
#ifdef OMF_NATIVE
    w=WIDTH;h=HEIGHT;aspect=1;scale=1;
#endif
    sw_context *c=data;
#ifdef OMF_FAST_VIDEO
    c->background_source = NULL;
    c->cached_frame = c->pending_restore = false;
#endif
    c->width=w; c->height=h; c->fullscreen=fs;c->vsync=vs;c->aspect=aspect;c->limit=limit;
#ifdef OMF_LOOP_DIAG
    omf_loop_config(limit,vs);
#endif
    c->window=SDL_CreateWindow("OpenOMF software",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,
                               w,h,fs?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
    if(c->window) c->presenter=SDL_CreateRenderer(c->window,-1,SDL_RENDERER_SOFTWARE);
    if(c->presenter) c->texture=SDL_CreateTexture(c->presenter,SDL_PIXELFORMAT_ARGB8888,
                                                  SDL_TEXTUREACCESS_STREAMING,
#ifdef OMF_FAST_VIDEO
                                                  HALF_WIDTH,HALF_HEIGHT);
#else
                                                  WIDTH,HEIGHT);
#endif
    if(!c->texture) {log_error("Software video init: %s",SDL_GetError());close_context(c);return false;}
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    c->last_tick=SDL_GetTicks();
    vga_state_mark_dirty();
    log_info("Software VGA renderer initialized (320x200, no OpenGL)");
    return true;
}
static void state(void *data,int *w,int *h,bool *fs,bool *vs,int *aspect,int *scale) {
    sw_context *c=data;
    if(w) *w=c->width;
    if(h) *h=c->height;
    if(fs) *fs=c->fullscreen;
    if(vs) *vs=c->vsync;
    if(aspect) *aspect=c->aspect;
    if(scale) *scale=1;
}
static bool reset_with(void *data,int w,int h,bool fs,bool vs,int aspect,int limit,int scale,int mode) {
    close_context(data);return setup(data,w,h,fs,vs,aspect,limit,scale,mode);
}
static void reset(void *data) {vga_state_mark_dirty();}
static void sync_tables(sw_context *c) {
    vga_palette *p;vga_remap_tables *r;
    if(vga_state_is_palette_dirty(&p,NULL,NULL)) c->palette=p;
    if(vga_state_is_remap_dirty(&r)) c->remaps=r;
}
static int remap_index(sw_context *c,int row,int index) {
    index=bounded(index,0,VGA_PALETTE_SIZE-1);
    return c->remaps ? c->remaps->tables[bounded(row,0,VGA_REMAP_COUNT-1)].data[index] : index;
}
#ifdef OMF_FAST_VIDEO
static void clear_dirty(sw_context *c) {
#ifdef OMF_ROW_DIRTY
    memset(c->dirty_right, 0, sizeof c->dirty_right);
#else
    c->dirty_x0 = c->dirty_y0 = c->dirty_x1 = c->dirty_y1 = 0;
#endif
}

static void restore_background(sw_context *c) {
#ifdef OMF_ROW_DIRTY
    uint32_t restored_pixels = 0;
    if (c->pending_restore) {
        for (int y = 0; y < HALF_HEIGHT; y++) {
            const unsigned right = c->dirty_right[y];
            if (!right) continue;
            const unsigned left = c->dirty_left[y];
            const unsigned width = right - left;
            int offset = y * HALF_WIDTH + left;
            memcpy(c->pixels + offset, c->background + offset, width * sizeof(sw_pixel));
            restored_pixels += width;
        }
    } else {
        memcpy(c->pixels, c->background, sizeof c->background);
        restored_pixels = HALF_WIDTH * HALF_HEIGHT;
    }
    omf_timing_background(true, restored_pixels);
#else
#ifdef OMF_TIMING_DIAG
    uint32_t restored_pixels = c->pending_restore ?
        (c->dirty_x1 - c->dirty_x0) * (c->dirty_y1 - c->dirty_y0) : HALF_WIDTH * HALF_HEIGHT;
    omf_timing_background(true, restored_pixels);
#endif
    if (c->pending_restore) {
        size_t bytes = (size_t)(c->dirty_x1 - c->dirty_x0) * sizeof(sw_pixel);
        for (int y = c->dirty_y0; y < c->dirty_y1; y++) {
            int offset = y * HALF_WIDTH + c->dirty_x0;
            memcpy(c->pixels + offset, c->background + offset, bytes);
        }
    } else {
        memcpy(c->pixels, c->background, sizeof c->background);
    }
#endif
    c->pending_restore = false;
    c->cached_frame = true;
    clear_dirty(c);
}

static void clear_pending_background(sw_context *c) {
    if (c->pending_restore) {
        memset(c->pixels, 0, sizeof c->background);
        c->pending_restore = false;
    }
}

static void mark_dirty(sw_context *c, int x0, int y0, int x1, int y1) {
#ifdef OMF_ROW_DIRTY
    for (int y = y0; y < y1; y++) {
        const unsigned right = c->dirty_right[y];
        if (!right || x0 < c->dirty_left[y]) c->dirty_left[y] = x0;
        if (x1 > right) c->dirty_right[y] = x1;
    }
#else
    if (c->dirty_y0 == c->dirty_y1) {
        c->dirty_x0 = x0;
        c->dirty_y0 = y0;
        c->dirty_x1 = x1;
        c->dirty_y1 = y1;
        return;
    }
    if (x0 < c->dirty_x0) c->dirty_x0 = x0;
    if (y0 < c->dirty_y0) c->dirty_y0 = y0;
    if (x1 > c->dirty_x1) c->dirty_x1 = x1;
    if (y1 > c->dirty_y1) c->dirty_y1 = y1;
#endif
}
#endif

static void prepare(void *data,unsigned options) {
    sw_context *c=data;c->options=options;
#ifdef OMF_FAST_VIDEO
#ifdef OMF_SRAM_WORK
    c->pixels = c->screen_pixels;
#ifdef OMF_SRAM_ARGB
    c->rgba = c->screen_rgba;
#endif
#endif
    c->area_active = false;
    c->first_draw = true;
    c->pending_restore = c->cached_frame;
    c->cached_frame = false;
    if (!c->pending_restore) memset(c->pixels, 0, sizeof c->background);
#else
    memset(c->pixels,0,sizeof c->pixels);
#endif
    sync_tables(c);
}
static void draw(void *data,const surface *s,SDL_Rect *r,int remap,int rounds,int offset,int limit,int opacity,
                 unsigned flip,unsigned options) {
    sw_context *c=data;
    if(!s || !s->data || s->w<=0 || s->h<=0 || r->w<=0 || r->h<=0 || opacity<=0) return;
    sync_tables(c);
    int x0=bounded(r->x,0,WIDTH),x1=bounded(r->x+r->w,0,WIDTH);
    int y0=bounded(r->y,0,HEIGHT),y1=bounded(r->y+r->h,0,HEIGHT);
#ifdef OMF_FAST_VIDEO
    /* Readback coordinates have a bottom-left origin; raster coordinates are top-left. */
    if (c->area_active) {
        int ax0=bounded(c->area.x,0,WIDTH), ax1=bounded(c->area.x+c->area.w,0,WIDTH);
        int ay0=HEIGHT-bounded(c->area.y+c->area.h,0,HEIGHT);
        int ay1=HEIGHT-bounded(c->area.y,0,HEIGHT);
        if(x0<ax0)x0=ax0; if(x1>ax1)x1=ax1;
        if(y0<ay0)y0=ay0; if(y1>ay1)y1=ay1;
    }
    const int shift = c->area_active ? 0 : 1;
    const int stride = WIDTH >> shift;
    const int align = (1 << shift) - 1;
    x0 = (x0 + align) >> shift; x1 = (x1 + align) >> shift;
    y0 = (y0 + align) >> shift; y1 = (y1 + align) >> shift;
    if (x0 >= x1 || y0 >= y1) return;

    /* Only the scene-owned base layer is cacheable. Scene changes invalidate it. */
    const bool background = shift && c->first_draw && options == SPRITE_STATIC_BACKGROUND &&
                            r->x == 0 && r->y == 0 && r->w == WIDTH && r->h == HEIGHT &&
                            s->w == WIDTH && s->h == HEIGHT;
    c->first_draw = false;
    if (background && c->background_source == s->data) {
        restore_background(c);
        return;
    }
    clear_pending_background(c);
    if (c->cached_frame) mark_dirty(c, x0, y0, x1, y1);
#endif
#ifdef OMF_XSTEP
    if (x0 >= x1 || y0 >= y1) return;
    int source_x[WIDTH];
    uint32_t width = (uint32_t)r->w;
    uint32_t source_width = (uint32_t)s->w;
#ifdef OMF_FAST_VIDEO
    const uint32_t increment = source_width << shift;
#else
    const uint32_t increment = source_width;
#endif
    uint32_t step = increment / width;
    uint32_t step_rem = increment % width;
    /* Pixel-center sampling: floor((offset * source_width + source_width / 2) / width). */
#ifdef OMF_FAST_VIDEO
    const int first_x = x0 << shift;
#else
    const int first_x = x0;
#endif
    uint64_t first = (uint64_t)((int64_t)first_x - r->x) * source_width + source_width / 2;
    uint32_t column = (uint32_t)(first / width);
    uint32_t rem = (uint32_t)(first % width);
    for (int x = x0; x < x1; x++) {
        source_x[x] = flip & FLIP_HORIZONTAL ? s->w - 1 - (int)column : (int)column;
        column += step;
        rem += step_rem;
        if (rem >= width) {
            rem -= width;
            column++;
        }
    }
#endif
#ifdef OMF_CAPTURE_ROW
    /* Main-core synchronous rasterizer: one row of scratch SRAM, never a
     * persistent image. Copy all pixel fields to preserve blending semantics. */
    static sw_pixel capture_row[WIDTH];
    const bool preserve_row = opacity<255 || s->transparent>=0 || rounds>0 ||
        (options&(SPRITE_DARK_TINT|SPRITE_SHADOW|SPRITE_INDEX_ADD));
#endif
    for(int y=y0;y<y1;y++) {
#ifdef OMF_CAPTURE_ROW
        sw_pixel *row_pixels=c->pixels+y*stride;
        if(c->area_active){
            /* A fully opaque overwrite needs no previous destination row. */
            if(preserve_row)
                memcpy(capture_row+x0,row_pixels+x0,(size_t)(x1-x0)*sizeof(sw_pixel));
            row_pixels=capture_row;
        }
#endif
#ifdef OMF_FAST_VIDEO
        const int screen_y = y << shift;
#else
        const int screen_y = y;
#endif
        /* Same native pixel-center coordinates as the shader's gl_FragCoord. */
        float noise_base=0;
        if(opacity<255) noise_base=tanf(10.0f*1.6180339887498948482f*(HEIGHT-screen_y-0.5f));
        int sy=(int)(((int64_t)(2*(screen_y-r->y)+1)*s->h)/(2*r->h));
        if(flip&FLIP_VERTICAL) sy=s->h-1-sy;
        sy=bounded(sy,0,s->h-1);
        for(int x=x0;x<x1;x++) {
            if(opacity<255) {
#ifdef OMF_FAST_VIDEO
                const int screen_x = x << shift;
#else
                const int screen_x = x;
#endif
                float v=noise_base+107.0f*(screen_x+0.5f)/256.0f;
                if(v-floorf(v)>opacity/255.0f) continue;
            }
#ifdef OMF_XSTEP
            int sx=source_x[x];
#else
            int sx=(int)(((int64_t)(2*(x-r->x)+1)*s->w)/(2*r->w));
            if(flip&FLIP_HORIZONTAL)sx=s->w-1-sx;
            sx=bounded(sx,0,s->w-1);
#endif
            int index=s->data[sy*s->w+sx];
            if(options&SPRITE_SHADOW) {
                index=0;
                for(int k=-1;k<=2;k++) {
                    int yy=sy+k;
                    if(yy>=0 && yy<s->h && s->data[yy*s->w+sx]!=s->transparent)index++;
                }
                if(!index)continue;
            } else {
                if(index==s->transparent)continue;
                if(index<=limit) index=bounded(index+offset,0,limit);
                if((options&SPRITE_REMAP) && !((options&SPRITE_HAR_QUIRKS)&&index>0x30))
                    index=remap_index(c,remap,index);
            }
#ifdef OMF_CAPTURE_INDEX
            if(c->area_active){
                /* area_finish reads only index. The other branches never
                 * modify index, nor feed it into a later index write. */
                if(!(options&(SPRITE_DARK_TINT|SPRITE_SHADOW|SPRITE_INDEX_ADD))&&rounds<=0)
                    c->capture_indices[y*WIDTH+x]=(uint16_t)index;
                continue;
            }
#endif
#ifdef OMF_CAPTURE_ROW
            sw_pixel *p=&row_pixels[x];
#elif defined(OMF_FAST_VIDEO)
            sw_pixel *p=&c->pixels[y*stride+x];
#else
            sw_pixel *p=&c->pixels[y*WIDTH+x];
#endif
            if(options&SPRITE_DARK_TINT) {
                p->remap=bounded(remap+12*19,0,255);p->tint=index;p->add=0;
            } else if(options&SPRITE_SHADOW) {
                int value=bounded(remap+rounds*19+index,0,255);
                if(value>p->remap)p->remap=value;
            } else if(rounds>0) p->remap=bounded(remap+rounds*19+index,0,255);
            else if(options&SPRITE_INDEX_ADD)p->add=bounded(index*60,0,1023);
            else {*p=(sw_pixel){(uint16_t)index,0,0,0};}
        }
#ifdef OMF_CAPTURE_ROW
        if(c->area_active)
            memcpy(c->pixels+y*stride+x0,capture_row+x0,(size_t)(x1-x0)*sizeof(sw_pixel));
#endif
    }
#ifdef OMF_FAST_VIDEO
    if (background) {
#ifdef OMF_TIMING_DIAG
        omf_timing_background(false, 0);
#endif
        memcpy(c->background, c->pixels, sizeof c->background);
        c->background_source = s->data;
        c->cached_frame = true;
        clear_dirty(c);
    }
#endif
}
static int resolve(sw_context *c,const sw_pixel *p) {
    int index=p->index+p->add;
    if(c->options&FBUFOPT_CREDITS) return bounded(index,0,VGA_PALETTE_SIZE-1);
    int row=p->remap%19,rounds=p->remap/19,tint=p->tint;
    if(tint>0 && rounds!=12)index=tint;
    else if(tint>=0x60){index=tint;rounds=0;}
    else if(tint>0){int behind=1+bounded(remap_index(c,4,index)-0xa8,0,7)*2;
        index=(tint&0xf0)+((tint&0x0f)*3+behind*2)/5;}
    for(int n=0;n<rounds;n++)index=remap_index(c,row,index);
    return bounded(index,0,VGA_PALETTE_SIZE-1);
}
#ifdef OMF_PALETTE_FAST
static unsigned resolve_palette_fast(sw_context *c, unsigned pixel_count) {
    uint32_t argb[VGA_PALETTE_SIZE];
    for (unsigned i = 0; i < VGA_PALETTE_SIZE; i++) {
        vga_color color = c->palette ? c->palette->colors[i] : (vga_color){0,0,0};
        argb[i] = 0xff000000u | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    }

    unsigned hits = 0;
    for (unsigned i = 0; i < pixel_count; i++) {
        const sw_pixel *pixel = &c->pixels[i];
        if ((pixel->remap | pixel->tint | pixel->add) == 0 && pixel->index < VGA_PALETTE_SIZE) {
            c->rgba[i] = argb[pixel->index];
            hits++;
        } else {
            int index = resolve(c, pixel);
            vga_color color = c->palette ? c->palette->colors[index] : (vga_color){0,0,0};
            c->rgba[i] = 0xff000000u | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
        }
    }
    return hits;
}
#endif

static void finish(void *data) {
#ifdef OMF_TIMING_DIAG
    omf_timing_scene_end();
#endif
    sw_context *c=data;sync_tables(c);
#ifdef OMF_FAST_VIDEO
    clear_pending_background(c);
    const int render_width = HALF_WIDTH, render_height = HALF_HEIGHT;
#else
    const int render_width = WIDTH, render_height = HEIGHT;
#endif
#ifdef OMF_TIMING_DIAG
    uint32_t palette_start=time_us_32();
#endif
#ifdef OMF_PALETTE_FAST
    const unsigned pixel_count = render_width * render_height;
    const unsigned palette_hits = resolve_palette_fast(c, pixel_count);
#else
    for(int i=0;i<render_width*render_height;i++) {
        int index=resolve(c,&c->pixels[i]);
        vga_color color=c->palette?c->palette->colors[index]:(vga_color){0,0,0};
        c->rgba[i]=0xff000000u|((uint32_t)color.r<<16)|((uint32_t)color.g<<8)|color.b;
    }
#endif
#ifdef OMF_TIMING_DIAG
    omf_timing_add(OMF_TIMING_PALETTE,palette_start);
#ifdef OMF_PALETTE_FAST
    omf_timing_palette_fast(palette_hits, pixel_count);
#endif
#endif
    vga_state_mark_palette_flushed();vga_state_mark_remaps_flushed();
#ifndef OMF_RGB555_DIRECT
    SDL_UpdateTexture(c->texture,NULL,c->rgba,render_width*4);
    SDL_SetRenderDrawColor(c->presenter,0,0,0,255);SDL_RenderClear(c->presenter);
#endif
    int w,h;SDL_GetRendererOutputSize(c->presenter,&w,&h);
    SDL_Rect dst={0,0,w,h};
    int aw=4,ah=3;
    if(c->aspect==0) {
        if(w*ah>h*aw){dst.w=h*aw/ah;dst.x=(w-dst.w)/2;}
        else{dst.h=w*ah/aw;dst.y=(h-dst.h)/2;}
    }
    dst.x+=c->move_x*dst.w/WIDTH;dst.y-=c->move_y*dst.w/WIDTH;
#ifndef OMF_RGB555_DIRECT
#ifdef OMF_TIMING_DIAG
    uint32_t copy_start=time_us_32();
#endif
    SDL_RenderCopy(c->presenter,c->texture,NULL,&dst);
#ifdef OMF_TIMING_DIAG
    omf_timing_add(OMF_TIMING_RENDER_COPY,copy_start);
#endif
#endif
    if(c->screenshot) {
        unsigned char *rgb=omf_malloc(WIDTH*HEIGHT*3);
        for(int y=0;y<HEIGHT;y++) for(int x=0;x<WIDTH;x++) {
            int i=y*WIDTH+x;
            uint32_t color=c->rgba[(y*render_height/HEIGHT)*render_width+x*render_width/WIDTH];
            rgb[i*3]=color>>16;rgb[i*3+1]=color>>8;rgb[i*3+2]=color;
        }
        SDL_Rect whole={0,0,WIDTH,HEIGHT};c->screenshot(&whole,rgb,false);omf_free(rgb);c->screenshot=NULL;
    }
#ifdef OMF_RGB555_DIRECT
    omf_present_half_argb(c->rgba,dst.x,dst.y);
#else
    SDL_RenderPresent(c->presenter);
#endif
    unsigned limit=c->limit>0?1000u/c->limit:(c->vsync?16u:0u);
    unsigned delta=SDL_GetTicks()-c->last_tick;
    if(delta<limit){
        OMF_LOOP_SCOPE(OMF_LOOP_LIMIT);
        SDL_Delay(limit-delta);
    }
    c->last_tick=SDL_GetTicks();
}
static void area_prepare(void *data,const SDL_Rect *area) {
    sw_context *c=data;c->area=*area;prepare(data,0);
#ifdef OMF_FAST_VIDEO
    c->area_active = true;
#ifdef OMF_SRAM_ARGB
    c->rgba = c->full_rgba;
#endif
    c->pending_restore = false;
#ifdef OMF_SRAM_WORK
    c->pixels = c->full_pixels;
    int ax0=bounded(area->x,0,WIDTH), ax1=bounded(area->x+area->w,0,WIDTH);
    int ay0=HEIGHT-bounded(area->y+area->h,0,HEIGHT), ay1=HEIGHT-bounded(area->y,0,HEIGHT);
    if(ax1>ax0) for(int y=ay0;y<ay1;y++)
#ifdef OMF_CAPTURE_INDEX
        memset(c->capture_indices+y*WIDTH+ax0,0,(size_t)(ax1-ax0)*sizeof(uint16_t));
#else
        memset(c->pixels+y*WIDTH+ax0,0,(size_t)(ax1-ax0)*sizeof(sw_pixel));
#endif
#else
    memset(c->pixels, 0, sizeof c->pixels);
#endif
#endif
}
static void area_finish(void *data,surface *dst) {
    sw_context *c=data;
    int x0=bounded(c->area.x,0,WIDTH),x1=bounded(c->area.x+c->area.w,0,WIDTH);
    int y0=bounded(c->area.y,0,HEIGHT),y1=bounded(c->area.y+c->area.h,0,HEIGHT);
    surface_create(dst,x1-x0,y1-y0);
    /* Upstream readback area uses bottom-left coordinates. */
    for(int y=0;y<dst->h;y++)for(int x=0;x<dst->w;x++)
#ifdef OMF_CAPTURE_INDEX
        dst->data[y*dst->w+x]=c->capture_indices[(HEIGHT-y1+y)*WIDTH+x0+x];
#else
        dst->data[y*dst->w+x]=c->pixels[(HEIGHT-y1+y)*WIDTH+x0+x].index;
#endif
    dst->transparent=-1;
}
static void move(void *data,int x,int y){sw_context *c=data;c->move_x=x;c->move_y=y;}
static void capture(void *data,video_screenshot_signal cb){((sw_context*)data)->screenshot=cb;}
static void scene(void *data) {
#ifdef OMF_FAST_VIDEO
    sw_context *c=data;
    c->background_source = NULL;
#else
    (void)data;
#endif
    vga_state_mark_dirty();
}
void software_renderer_set_callbacks(renderer *r) {
    memset(r,0,sizeof *r);r->is_available=available;r->get_name=name;r->get_description=description;
    r->create=create;r->destroy=destroy;r->setup_context=setup;r->get_context_state=state;
    r->reset_context_with=reset_with;r->reset_context=reset;r->close_context=close_context;
    r->draw_surface=draw;r->move_target=move;r->render_prepare=prepare;r->render_finish=finish;
    r->render_area_prepare=area_prepare;r->render_area_finish=area_finish;r->capture_screen=capture;
    r->signal_scene_change=scene;
}
