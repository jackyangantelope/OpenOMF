/* SPDX-License-Identifier: BSD-3-Clause */
#include <SDL.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "controller/joystick.h"
#include "game/common_defines.h"
#include "utils/log.h"
static int actions,closed;
void controller_cmd(controller *c,int action,ctrl_event **ev){(void)ev;actions|=action;c->current|=action;}
void controller_close(controller *c,ctrl_event **ev){(void)c;(void)ev;closed++;}
void log_msg(log_level level,const char *fmt,...){(void)level;(void)fmt;}
_Noreturn void _crash(const char *fn,const char *file,int line,const char *fmt,...){
    fprintf(stderr,"crash %s %s:%d %s\n",fn,file,line,fmt);abort();
}
static int poll(controller *c){ctrl_event *ev=NULL;actions=0;SDL_JoystickUpdate();c->poll_fun(c,&ev);return actions;}
int main(void){
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER)==0);
    SDL_VirtualJoystickDesc desc={0};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.name="Virtual test pad";
    desc.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
    desc.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;
    int index=SDL_JoystickAttachVirtualEx(&desc);assert(index>=0&&SDL_IsGameController(index));
    SDL_Joystick *j=SDL_JoystickOpen(index);assert(j);
    controller c={0};assert(joystick_create(&c,index)==1);
    SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_LEFTX,-28000);
    assert(poll(&c)&ACT_LEFT);
    SDL_JoystickSetVirtualAxis(j,SDL_CONTROLLER_AXIS_LEFTX,0);
    assert(poll(&c)==ACT_STOP);
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_DPAD_UP,1);
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_A,1);
    assert((poll(&c)&(ACT_UP|ACT_PUNCH))==(ACT_UP|ACT_PUNCH));
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_A,0);
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_DPAD_UP,0);
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_B,1);
    assert(poll(&c)&ACT_KICK);
    SDL_JoystickSetVirtualButton(j,SDL_CONTROLLER_BUTTON_B,0);assert(poll(&c)==ACT_STOP);
    SDL_JoystickDetachVirtual(index);poll(&c);assert(closed==1);
    joystick_free(&c);SDL_JoystickClose(j);SDL_Quit();
    puts("PASS: real SDL virtual gamepad axis/dpad/punch/kick/release/disconnect");
}
