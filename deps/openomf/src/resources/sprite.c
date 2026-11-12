#include "formats/sprite.h"
#include "resources/sprite.h"
#include "utils/allocator.h"
#include <stdlib.h>
#ifdef OMF_SPRITE_AUDIO
#include "pico/time.h"
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
extern void omf_native_audio_service(void);
static uint32_t converted_count, convert_max_us, service_max_us;
static uint64_t convert_total_us, service_total_us;
#ifdef OMF_LOAD_DIAG
static uint64_t load_stage_us[4];
static uint32_t load_stage_max[4];
static void load_stage(unsigned stage,uint32_t start){
    uint32_t dt=time_us_32()-start;
    load_stage_us[stage]+=dt;
    if(dt>load_stage_max[stage])load_stage_max[stage]=dt;
}
extern void omf_load_surface_report(void);
#endif
void omf_sprite_audio_report(void) {
    printf("OMF_SPRITE_AUDIO n=%lu convert_us=%"PRIu64" convert_max_us=%lu service_us=%"PRIu64" service_max_us=%lu\n",
           (unsigned long)converted_count,convert_total_us,(unsigned long)convert_max_us,
           service_total_us,(unsigned long)service_max_us);
#ifdef OMF_LOAD_DIAG
    printf("OMF_LOAD_SPRITE n=%lu alloc=%"PRIu64"/%lu decode=%"PRIu64"/%lu surface=%"PRIu64"/%lu free=%"PRIu64"/%lu\n",
           (unsigned long)converted_count,load_stage_us[0],(unsigned long)load_stage_max[0],
           load_stage_us[1],(unsigned long)load_stage_max[1],load_stage_us[2],(unsigned long)load_stage_max[2],
           load_stage_us[3],(unsigned long)load_stage_max[3]);
    for(unsigned i=0;i<4;i++){
        load_stage_us[i]=0;load_stage_max[i]=0;
    }
    omf_load_surface_report();
#endif
    converted_count=convert_max_us=service_max_us=0;
    convert_total_us=service_total_us=0;
}
#endif

void sprite_create_custom(sprite *sp, vec2i pos, surface *data) {
    sp->id = -1;
    sp->pos = pos;
    sp->data = data;
}

void sprite_create(sprite *sp, void *src, int id) {
#ifdef OMF_SPRITE_AUDIO
    uint32_t convert_start=time_us_32();
#endif
    sd_sprite *sdsprite = (sd_sprite *)src;
    sp->id = id;
    sp->pos = sdsprite->pos;

    if(sdsprite->width == 0 || sdsprite->height == 0) {
        sp->data = NULL;
        sp->owned = false;
        return;
    }

    // Load data
#ifdef OMF_LOAD_DIAG
    uint32_t load_start=time_us_32();
#endif
    sp->data = omf_calloc(1, sizeof(surface));
    sp->owned = true;
    sd_vga_image raw;
#ifdef OMF_LOAD_DIAG
    load_stage(0,load_start);load_start=time_us_32();
#endif
    sd_sprite_vga_decode(&raw, sdsprite);
#ifdef OMF_LOAD_DIAG
    load_stage(1,load_start);load_start=time_us_32();
#endif
    surface_create_from_data(sp->data, raw.w, raw.h, (unsigned char *)raw.data);
#ifdef OMF_LOAD_DIAG
    load_stage(2,load_start);
#endif
    sp->data->render_w = sdsprite->render_width;
    sp->data->render_h = sdsprite->render_height;
#ifdef OMF_LOAD_DIAG
    load_start=time_us_32();
#endif
    sd_vga_image_free(&raw);
#ifdef OMF_LOAD_DIAG
    load_stage(3,load_start);
#endif
#ifdef OMF_SPRITE_AUDIO
    uint32_t elapsed=time_us_32()-convert_start;
    converted_count++;convert_total_us+=elapsed;
    if(elapsed>convert_max_us)convert_max_us=elapsed;
    int saved_errno=errno;
    uint32_t service_start=time_us_32();
    omf_native_audio_service();
    errno=saved_errno;
    elapsed=time_us_32()-service_start;
    service_total_us+=elapsed;
    if(elapsed>service_max_us)service_max_us=elapsed;
#endif
}

void sprite_create_reference(sprite *sp, void *src, int id, void *data) {
    sd_sprite *sdsprite = (sd_sprite *)src;
    sp->id = id;
    sp->pos = sdsprite->pos;
    sp->data = data;
    sp->owned = false;
}

int sprite_clone(sprite *src, sprite *dst) {
    memcpy(dst, src, sizeof(sprite));
    dst->data = omf_calloc(1, sizeof(surface));
    surface_create_from(dst->data, src->data);
    return 0;
}

void sprite_free(sprite *sp) {
    if(sp->owned) {
        surface_free(sp->data);
        omf_free(sp->data);
    }
}

vec2i sprite_get_size(sprite *sp) {
    if(sp->data != NULL) {
        return vec2i_create(sp->data->w, sp->data->h);
    }
    return vec2i_create(0, 0);
}

sprite *sprite_copy(sprite *src) {
    if(src == NULL) {
        return NULL;
    }

    sprite *new = omf_calloc(1, sizeof(sprite));
    new->pos = src->pos;
    new->id = src->id;

    // Copy surface
    new->data = omf_calloc(1, sizeof(surface));
    surface_create_from(new->data, src->data);
    new->owned = true;
    return new;
}
