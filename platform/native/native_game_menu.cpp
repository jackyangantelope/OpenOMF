/* SPDX-License-Identifier: BSD-3-Clause
 * Dataset picker for engines whose games consist of multiple resource files. */
#include "native_game_menu.h"
#include "jack_wave1_runtime.h"
#include "ff.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr uint16_t black=0,white=0x7fff,mint=0x3f70;
const char *labels[]={"Ultima IV","One Must Fall 2097","Star Control II"};
const char *titles[]={"xu4 - ROM LIST","OpenOMF - ROM LIST","UQM - ROM LIST"};
bool kind_valid(native_game_kind k){return k>=NATIVE_GAME_XU4&&k<=NATIVE_GAME_UQM;}
bool join(const char *root,const char *leaf,char *out,size_t cap){
    int n=std::snprintf(out,cap,"%s/%s",root,leaf);return n>=0&&static_cast<size_t>(n)<cap;
}
bool resource(const char *root,const char *file){
    char path[JACK_WAVE1_PATH_MAX];FILINFO info{};
    return join(root,file,path,sizeof path)&&f_stat(path,&info)==FR_OK&&!(info.fattrib&AM_DIR)&&info.fsize>0;
}
bool valid(native_game_kind kind,const char *root){
    switch(kind){
    case NATIVE_GAME_XU4:return resource(root,"conf/config.xml")&&resource(root,"ultima4/AVATAR.EXE")&&resource(root,"ultima4/WORLD.MAP");
    case NATIVE_GAME_OMF:return resource(root,"resources/openomf.bk")&&resource(root,"resources/SOUNDS.DAT")&&resource(root,"resources/ENGLISH.DAT");
    case NATIVE_GAME_UQM:return resource(root,"content/packages/uqm-0.8.0-content.uqm");
    default:return false;
    }
}
int compare(const char *a,const char *b){
    for(;;a++,b++){
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;
        if(x!=y||!x||!y)return x<y?-1:x>y?1:0;
    }
}
void drain_keys(){jack_wave1_key_event_t event;while(jack_wave1_usb_pop_key(&event)){};}
void draw(native_game_kind kind,const char *root,const native_game_list &list,unsigned selection){
    jack_wave1_clear(black);jack_wave1_draw_text(8,8,titles[kind],mint,black);
    jack_wave1_draw_text(8,24,root,white,black);
    const unsigned first=selection>=18?selection-17:0;
    for(unsigned i=first;i<list.count&&i<first+18;i++){
        unsigned y=48+(i-first)*8;bool active=i==selection;
        if(active)jack_wave1_fill_rect(4,y,312,8,white);
        jack_wave1_draw_text(8,y,!std::strcmp(list.folders[i],".")?labels[kind]:list.folders[i],active?black:white,active?white:black);
    }
    char count[40];std::snprintf(count,sizeof count,"%u / %u",selection+1,list.count);
    jack_wave1_draw_text(8,208,count,white,black);
    jack_wave1_draw_text(8,224,"ARROWS:SELECT X/S/ENTER:OPEN",white,black);
}
}
extern "C" bool native_game_path(const native_game_list *list,unsigned i,const char *root,char *out,size_t cap){
    if(!list||i>=list->count||!root||!out||!cap)return false;
    if(!std::strcmp(list->folders[i],".")){
        size_t n=std::strlen(root);if(n>=cap)return false;std::memcpy(out,root,n+1);return true;
    }
    return join(root,list->folders[i],out,cap);
}
extern "C" bool native_game_scan(native_game_kind kind,const char *root,native_game_list *list){
    if(!kind_valid(kind)||!root||!list||!*root||std::strlen(root)>=JACK_WAVE1_PATH_MAX)return false;
    list->count=0;DIR dir;FILINFO info{};FRESULT result=f_opendir(&dir,root);
    if(result!=FR_OK)return false;
    if(valid(kind,root))std::strcpy(list->folders[list->count++],".");
    while(list->count<NATIVE_GAME_LIMIT){
        result=f_readdir(&dir,&info);if(result!=FR_OK||!info.fname[0])break;
        if(!(info.fattrib&AM_DIR)||info.fname[0]=='.'||std::strlen(info.fname)>=sizeof list->folders[0])continue;
        char path[JACK_WAVE1_PATH_MAX];
        if(join(root,info.fname,path,sizeof path)&&valid(kind,path))std::strcpy(list->folders[list->count++],info.fname);
    }
    FRESULT closed=f_closedir(&dir);if(result!=FR_OK||closed!=FR_OK){list->count=0;return false;}

    unsigned first=list->count&&!std::strcmp(list->folders[0],".")?1:0;
    for(unsigned i=first+1;i<list->count;i++)for(unsigned j=i;j>first&&compare(list->folders[j-1],list->folders[j])>0;j--){
        char temp[128];std::memcpy(temp,list->folders[j],128);std::memcpy(list->folders[j],list->folders[j-1],128);std::memcpy(list->folders[j-1],temp,128);
    }
    return true;
}
extern "C" bool native_game_select(native_game_kind kind,const char *root,char *out,size_t cap){
    native_game_list list{};
    if(!out||!cap||!native_game_scan(kind,root,&list)||!list.count){
        jack_wave1_show_error("ROM LIST","No compatible game data");std::printf("NATIVE_PICKER empty root=%s\n",root?root:"(null)");return false;
    }
    std::printf("NATIVE_PICKER ready core=%d root=%s entries=%u auto=0\n",int(kind),root,list.count);
    unsigned selected=0;uint16_t previous=0;
    draw(kind,root,list,selected);

    jack_wave1_wait_buttons_released();drain_keys();
    for(;;){
        draw(kind,root,list,selected);jack_wave1_wait_vsync();jack_wave1_usb_service();
        uint16_t buttons=jack_wave1_poll_buttons(),pressed=buttons&~previous;previous=buttons;
        if(pressed&JACK_BUTTON_UP)selected=selected?selected-1:list.count-1;
        if(pressed&JACK_BUTTON_DOWN)selected=(selected+1)%list.count;
        if(pressed&(JACK_BUTTON_A|JACK_BUTTON_START)){
            if(!native_game_path(&list,selected,root,out,cap)||!valid(kind,out)){
                jack_wave1_show_error("ROM LIST","Game data missing or path too long");return false;
            }
            jack_wave1_wait_buttons_released();drain_keys();jack_wave1_clear(black);
            std::printf("NATIVE_PICKER selected=%s\n",out);return true;
        }
    }
}
