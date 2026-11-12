/* SPDX-License-Identifier: BSD-3-Clause
 * SDL ABI subset for OpenOMF's single-threaded native backend. This is not an
 * SDL implementation for arbitrary apps: unused joystick/haptic capabilities
 * explicitly report unavailable. Physical pads share the keyboard mapping.
 */
#include "omf_sdl_names.h"
#include <SDL.h>
#include "native_hal.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
struct SDL_Window {int w,h;};
struct SDL_Renderer {SDL_Window *window;uint32_t *frame;};
struct SDL_Texture {const uint32_t *pixels;int w,h,pitch;};
struct SDL_mutex {unsigned depth;};
static char error[128];
static uint8_t keyboard[SDL_NUM_SCANCODES], previous[SDL_NUM_SCANCODES];
static int text_input, pending_text, repeat_code;
static uint32_t repeat_at;
static SDL_Keymod modifiers;
static int quit_delivered;
extern void omf_native_audio_service(void);
static int fail(const char *msg){snprintf(error,sizeof error,"%s",msg);return -1;}
int SDL_Init(Uint32 flags){return SDL_InitSubSystem(flags);}
int SDL_InitSubSystem(Uint32 flags){return flags&SDL_INIT_HAPTIC?fail("Haptics unavailable"):0;}
void SDL_Quit(void){}
void SDL_QuitSubSystem(Uint32 flags){(void)flags;}
const char *SDL_GetError(void){return error;}
void SDL_GetVersion(SDL_version *v){SDL_VERSION(v);}
const char *SDL_GetPlatform(void){return "RP2350 native OpenOMF adapter";}
Uint32 SDL_GetTicks(void){omf_native_audio_service();return native_millis();}
Uint64 SDL_GetTicks64(void){static uint32_t last;static uint64_t high;uint32_t now=SDL_GetTicks();if(now<last)high+=UINT64_C(1)<<32;last=now;return high|now;}
void SDL_Delay(Uint32 ms){native_sleep(ms);}
SDL_Window *SDL_CreateWindow(const char *title,int x,int y,int w,int h,Uint32 flags){
    (void)title;(void)x;(void)y;(void)flags;SDL_Window *v=calloc(1,sizeof *v);if(v){v->w=320;v->h=200;}return v;
}
void SDL_DestroyWindow(SDL_Window *w){free(w);}
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w,int index,Uint32 flags){
    (void)index;(void)flags;if(!w)return NULL;SDL_Renderer *r=calloc(1,sizeof *r);
    if(r){r->window=w;r->frame=calloc(320*200,4);if(!r->frame){free(r);return NULL;}}return r;
}
void SDL_DestroyRenderer(SDL_Renderer *r){if(r){free(r->frame);free(r);}}
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r,Uint32 format,int access,int w,int h){
    (void)r;(void)access;
    bool supported = w==320 && h==200;
#ifdef OMF_FAST_VIDEO
    supported |= w==160 && h==100;
#endif
    if(format!=SDL_PIXELFORMAT_ARGB8888||!supported){fail("Unsupported texture format");return NULL;}
    SDL_Texture *t=calloc(1,sizeof *t);if(t){t->w=w;t->h=h;}return t;
}
void SDL_DestroyTexture(SDL_Texture *t){free(t);}
int SDL_UpdateTexture(SDL_Texture *t,const SDL_Rect *r,const void *pixels,int pitch){
    if(!t||r||pitch<t->w*4)return fail("Only complete native frames supported");
    /* The CPU renderer owns this stable frame until RenderPresent. */
    t->pixels=pixels;t->pitch=pitch;return 0;
}
int SDL_SetRenderDrawColor(SDL_Renderer *r,Uint8 a,Uint8 b,Uint8 c,Uint8 d){(void)r;(void)a;(void)b;(void)c;(void)d;return 0;}
int SDL_RenderClear(SDL_Renderer *r){memset(r->frame,0,320*200*4);return 0;}
int SDL_GetRendererOutputSize(SDL_Renderer *r,int *w,int *h){(void)r;if(w)*w=320;if(h)*h=200;return 0;}
int SDL_RenderCopy(SDL_Renderer *r,SDL_Texture *t,const SDL_Rect *src,const SDL_Rect *dst){
    if(!r||!t||!t->pixels||src)return fail("Invalid native frame");
    /* Full unscaled game area; the transport supplies display aspect ratio.
       Convert the renderer's ARGB word to the HAL's RGBA bytes. */
    int dx=dst?dst->x:0,dy=dst?dst->y:0;
#ifdef OMF_FAST_VIDEO
    if (t->w == 160 && dx == 0 && dy == 0) {
        for (int y = 0; y < 100; y++) {
            const uint32_t *src_row = (const uint32_t *)((const uint8_t *)t->pixels + y*t->pitch);
            uint32_t *row = r->frame + y*640;
            for (int x = 0; x < 160; x++) {
                uint32_t p = src_row[x];
                uint32_t rgba = (p&0xff00ff00u)|((p>>16)&255)|((p&255)<<16);
                row[2*x] = row[2*x+1] = row[320+2*x] = row[321+2*x] = rgba;
            }
        }
        return 0;
    }
    const unsigned shift = t->w == 160;
#endif
    for(int y=0;y<200;y++)for(int x=0;x<320;x++){
        int sx=x-dx,sy=y-dy;
        if(sx<0||sx>=320||sy<0||sy>=200)continue;
#ifdef OMF_FAST_VIDEO
        sx >>= shift;
        sy >>= shift;
#endif
        uint32_t p=*(const uint32_t*)((const uint8_t*)t->pixels+sy*t->pitch+sx*4);
        r->frame[y*320+x]=(p&0xff00ff00u)|((p>>16)&255)|((p&255)<<16);
    }
    return 0;
}
void SDL_RenderPresent(SDL_Renderer *r){native_present(r->frame,320,200,0);omf_native_audio_service();}
SDL_bool SDL_SetHint(const char *name,const char *value){(void)name;(void)value;return SDL_TRUE;}
static void sample_keys(void){
    uint8_t usb[256];native_keys(usb);memcpy(keyboard,usb,256);
    uint16_t p=native_pad();
    keyboard[SDL_SCANCODE_UP]|=!!(p&16);keyboard[SDL_SCANCODE_DOWN]|=!!(p&32);
    keyboard[SDL_SCANCODE_LEFT]|=!!(p&64);keyboard[SDL_SCANCODE_RIGHT]|=!!(p&128);
    keyboard[SDL_SCANCODE_RETURN]|=!!(p&1);keyboard[SDL_SCANCODE_RSHIFT]|=!!(p&2);
    keyboard[SDL_SCANCODE_ESCAPE]|=!!(p&8);keyboard[SDL_SCANCODE_SPACE]|=!!(p&4);
    modifiers=KMOD_NONE;
    static const SDL_Keymod map[]={KMOD_LCTRL,KMOD_LSHIFT,KMOD_LALT,KMOD_LGUI,KMOD_RCTRL,KMOD_RSHIFT,KMOD_RALT,KMOD_RGUI};
    for(int i=0;i<8;i++)if(usb[224+i])modifiers|=map[i];
}
const Uint8 *SDL_GetKeyboardState(int *count){sample_keys();if(count)*count=SDL_NUM_SCANCODES;return keyboard;}
static SDL_Keycode keycode(int sc){
    if(sc>=SDL_SCANCODE_A&&sc<=SDL_SCANCODE_Z)return 'a'+sc-SDL_SCANCODE_A;
    if(sc>=SDL_SCANCODE_1&&sc<=SDL_SCANCODE_9)return '1'+sc-SDL_SCANCODE_1;
    if(sc==SDL_SCANCODE_0)return '0';
    switch(sc){case SDL_SCANCODE_RETURN:return '\r';case SDL_SCANCODE_ESCAPE:return 27;case SDL_SCANCODE_BACKSPACE:return '\b';
    case SDL_SCANCODE_TAB:return '\t';case SDL_SCANCODE_SPACE:return ' ';case SDL_SCANCODE_MINUS:return '-';
    case SDL_SCANCODE_EQUALS:return '=';case SDL_SCANCODE_LEFTBRACKET:return '[';case SDL_SCANCODE_RIGHTBRACKET:return ']';
    case SDL_SCANCODE_BACKSLASH:return '\\';case SDL_SCANCODE_SEMICOLON:return ';';case SDL_SCANCODE_APOSTROPHE:return '\'';
    case SDL_SCANCODE_GRAVE:return '`';case SDL_SCANCODE_COMMA:return ',';case SDL_SCANCODE_PERIOD:return '.';case SDL_SCANCODE_SLASH:return '/';
    default:return SDL_SCANCODE_TO_KEYCODE(sc);}
}
static void key_event(SDL_Event *e,int sc,int down,int repeat){
    memset(e,0,sizeof *e);e->type=down?SDL_KEYDOWN:SDL_KEYUP;e->key.state=down?SDL_PRESSED:SDL_RELEASED;
    e->key.keysym.scancode=sc;e->key.keysym.sym=keycode(sc);e->key.keysym.mod=modifiers;e->key.repeat=repeat;
    if(text_input&&down&&!(modifiers&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){
        int ch=e->key.keysym.sym;
        if(modifiers&KMOD_SHIFT){
            if(ch>='a'&&ch<='z')ch=toupper(ch);
            else {const char *base="1234567890-=[]\\;'`,./",*shift="!@#$%^&*()_+{}|:\"~<>?";const char *at=ch<128?strchr(base,ch):NULL;if(at)ch=shift[at-base];}
        }
        if(ch>=32&&ch<127)pending_text=ch;
    }
}
int SDL_PollEvent(SDL_Event *e){
    if(!e)return 0;omf_native_audio_service();sample_keys();
    if(native_quit()&&!quit_delivered){quit_delivered=1;memset(e,0,sizeof *e);e->type=SDL_QUIT;return 1;}
    if(pending_text){memset(e,0,sizeof *e);e->type=SDL_TEXTINPUT;e->text.text[0]=pending_text;pending_text=0;return 1;}
    for(int sc=0;sc<256;sc++)if(keyboard[sc]!=previous[sc]){
        previous[sc]=keyboard[sc];key_event(e,sc,keyboard[sc],0);
        if(keyboard[sc]){repeat_code=sc;repeat_at=native_millis()+350;}else if(repeat_code==sc)repeat_code=0;
        return 1;
    }
    if(repeat_code&&keyboard[repeat_code]&&(int32_t)(native_millis()-repeat_at)>=0){
        key_event(e,repeat_code,1,1);repeat_at=native_millis()+80;return 1;
    }
    return 0;
}
typedef struct {SDL_Scancode sc;const char *name;} named_key;
static const named_key key_names[]={
    {SDL_SCANCODE_RETURN,"Return"},{SDL_SCANCODE_ESCAPE,"Escape"},{SDL_SCANCODE_SPACE,"Space"},
    {SDL_SCANCODE_BACKSPACE,"Backspace"},{SDL_SCANCODE_TAB,"Tab"},{SDL_SCANCODE_GRAVE,"`"},
    {SDL_SCANCODE_UP,"Up"},{SDL_SCANCODE_DOWN,"Down"},{SDL_SCANCODE_LEFT,"Left"},{SDL_SCANCODE_RIGHT,"Right"},
    {SDL_SCANCODE_HOME,"Home"},{SDL_SCANCODE_END,"End"},{SDL_SCANCODE_PAGEUP,"PageUp"},{SDL_SCANCODE_PAGEDOWN,"PageDown"},
    {SDL_SCANCODE_LSHIFT,"Left Shift"},{SDL_SCANCODE_RSHIFT,"Right Shift"},{SDL_SCANCODE_LCTRL,"Left Ctrl"},{SDL_SCANCODE_RCTRL,"Right Ctrl"},
    {SDL_SCANCODE_LALT,"Left Alt"},{SDL_SCANCODE_RALT,"Right Alt"},
    {SDL_SCANCODE_INSERT,"Insert"},{SDL_SCANCODE_DELETE,"Delete"},
    {SDL_SCANCODE_KP_ENTER,"Keypad Enter"},{SDL_SCANCODE_KP_0,"Keypad 0"},{SDL_SCANCODE_KP_1,"Keypad 1"},
    {SDL_SCANCODE_KP_2,"Keypad 2"},{SDL_SCANCODE_KP_3,"Keypad 3"},{SDL_SCANCODE_KP_4,"Keypad 4"},
    {SDL_SCANCODE_KP_5,"Keypad 5"},{SDL_SCANCODE_KP_6,"Keypad 6"},{SDL_SCANCODE_KP_7,"Keypad 7"},
    {SDL_SCANCODE_KP_8,"Keypad 8"},{SDL_SCANCODE_KP_9,"Keypad 9"}};
const char *SDL_GetScancodeName(SDL_Scancode sc){
    static char names[36][2];static char fs[12][4];
    if(sc>=4&&sc<=39){int i=sc-4;names[i][0]=i<26?'A'+i:(i==35?'0':'1'+i-26);return names[i];}
    if(sc>=SDL_SCANCODE_F1&&sc<=SDL_SCANCODE_F12){int i=sc-SDL_SCANCODE_F1;snprintf(fs[i],4,"F%d",i+1);return fs[i];}
    for(size_t i=0;i<sizeof key_names/sizeof *key_names;i++)if(sc==key_names[i].sc)return key_names[i].name;
    return "";
}
SDL_Scancode SDL_GetScancodeFromName(const char *name){if(!name||!*name)return SDL_SCANCODE_UNKNOWN;for(int sc=1;sc<256;sc++)if(!strcasecmp(name,SDL_GetScancodeName(sc)))return sc;return SDL_SCANCODE_UNKNOWN;}
void SDL_StartTextInput(void){text_input=1;}
void SDL_StopTextInput(void){text_input=pending_text=0;}
SDL_bool SDL_IsTextInputActive(void){return text_input?SDL_TRUE:SDL_FALSE;}
SDL_bool SDL_HasClipboardText(void){return SDL_FALSE;}
char *SDL_GetClipboardText(void){return strdup("");}
int SDL_ShowCursor(int toggle){(void)toggle;return 0;}
int SDL_ShowSimpleMessageBox(Uint32 flags,const char *title,const char *message,SDL_Window *window){(void)flags;(void)window;fprintf(stderr,"%s: %s\n",title,message);return 0;}
char *SDL_GetBasePath(void){
    char path[512];if(!getcwd(path,sizeof path-1))return NULL;
    size_t n=strlen(path);if(n&&path[n-1]!='/'){path[n++]='/';path[n]=0;}
    return strdup(path);
}
char *SDL_GetPrefPath(const char *org,const char *app){(void)org;(void)app;return strdup("/SAVES/OPENOMF/");}
void SDL_free(void *p){free(p);}
/* The game and audio producer run on the same main thread. These are recursive
   log locks, not an advertised general thread or IRQ synchronization API. */
SDL_mutex *SDL_CreateMutex(void){return calloc(1,sizeof(SDL_mutex));}
int SDL_LockMutex(SDL_mutex *m){if(!m)return -1;m->depth++;return 0;}
int SDL_UnlockMutex(SDL_mutex *m){if(!m||!m->depth)return -1;m->depth--;return 0;}
void SDL_DestroyMutex(SDL_mutex *m){assert(!m||!m->depth);free(m);}
int SDL_NumJoysticks(void){return 0;}
SDL_Joystick *SDL_JoystickOpen(int n){(void)n;fail("Use native keyboard/pad mapping");return NULL;}
void SDL_JoystickClose(SDL_Joystick *j){(void)j;}
SDL_bool SDL_JoystickGetAttached(SDL_Joystick *j){(void)j;return SDL_FALSE;}
SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick *j){(void)j;SDL_JoystickGUID v={{0}};return v;}
void SDL_JoystickGetGUIDString(SDL_JoystickGUID g,char *s,int n){(void)g;if(n>0)*s=0;}
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j){(void)j;return -1;}
const char *SDL_JoystickNameForIndex(int n){(void)n;return "Unavailable";}
int SDL_JoystickNumAxes(SDL_Joystick *j){(void)j;return 0;}
int SDL_JoystickNumButtons(SDL_Joystick *j){(void)j;return 0;}
int SDL_JoystickNumBalls(SDL_Joystick *j){(void)j;return 0;}
int SDL_JoystickNumHats(SDL_Joystick *j){(void)j;return 0;}
SDL_GameController *SDL_GameControllerOpen(int n){(void)n;return NULL;}
void SDL_GameControllerClose(SDL_GameController *j){(void)j;}
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *j){(void)j;return SDL_FALSE;}
Sint16 SDL_GameControllerGetAxis(SDL_GameController *j,SDL_GameControllerAxis a){(void)j;(void)a;return 0;}
Uint8 SDL_GameControllerGetButton(SDL_GameController *j,SDL_GameControllerButton b){(void)j;(void)b;return 0;}
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *j){(void)j;return NULL;}
const char *SDL_GameControllerNameForIndex(int n){(void)n;return "Unavailable";}
int SDL_GameControllerNumMappings(void){return 0;}
SDL_Haptic *SDL_HapticOpenFromJoystick(SDL_Joystick *j){(void)j;return NULL;}
void SDL_HapticClose(SDL_Haptic *h){(void)h;}
int SDL_HapticRumbleInit(SDL_Haptic *h){(void)h;return -1;}
int SDL_HapticRumblePlay(SDL_Haptic *h,float v,Uint32 ms){(void)h;(void)v;(void)ms;return -1;}
int SDL_HapticRumbleSupported(SDL_Haptic *h){(void)h;return 0;}
