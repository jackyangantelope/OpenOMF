/* SPDX-License-Identifier: BSD-3-Clause */
#include "engine.h"
#include "game/utils/settings.h"
#include "resources/resource_paths.h"
#include "resources/resource_files.h"
#include "utils/log.h"
#include "utils/random.h"
#include "native_hal.h"
#include <SDL.h>
#include <string.h>
int omf_game_main(int argc,char **argv){
    (void)argc;(void)argv;int result=1;log_init();log_set_colors(false);log_set_level(LOG_INFO);log_add_stderr(LOG_INFO,true);
    if(!resource_path_init())goto done;
    resource_path_create_dirs();path cfg=get_config_filename();
    if(settings_init(path_c(&cfg)))goto done;
    settings_load();settings *s=settings_get();
    s->video.screen_w=320;s->video.screen_h=200;s->video.fb_scale=1;
    s->video.aspect=1;s->video.vsync=0;s->video.framerate_limit=60;
    s->sound.sample_rate=44100;s->sound.music_type=0;
    rand_seed(native_millis());
    engine_init_flags flags={0};flags.speed=-1;
    strcpy(flags.force_renderer,"Software");strcpy(flags.force_audio_backend,"Native PCM");
    log_info("Native original-game mode: keyboard and physical pad; network unavailable.");
    if(engine_init(&flags)==0){engine_run(&flags);engine_close();result=0;}
    settings_save();settings_free();
done:
    log_close();return result;
}
