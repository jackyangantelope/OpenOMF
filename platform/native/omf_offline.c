/* SPDX-License-Identifier: BSD-3-Clause
 * Native target has no sockets. Network entry points fail explicitly; they
 * cannot create a fake connection or run a fake opponent. Local AI is upstream. */
#include "game/protos/scene.h"
#include "controller/net_controller.h"
#include "controller/spec_controller.h"
#include "utils/log.h"
#include <stdlib.h>
static void unavailable(void){log_error("Unexpected network controller in offline build");abort();}
int lobby_create(scene *s){(void)s;log_error("Network lobby unavailable on native RP2350 target");return 1;}
void net_controller_create(controller *c,ENetHost *h,ENetPeer *p,ENetPeer *l,int id){unavailable();}
void net_controller_free(controller *c){unavailable();}
int net_controller_get_rtt(controller *c){unavailable();return 0;}
void net_controller_har_hook(int action,void *data){unavailable();}
bool net_controller_ready(controller *c){unavailable();return false;}
int net_controller_tick_offset(controller *c){unavailable();return 0;}
ENetPeer *net_controller_get_lobby_connection(controller *c){unavailable();return NULL;}
ENetHost *net_controller_get_host(controller *c){unavailable();return NULL;}
int net_controller_get_winner(controller *c){unavailable();return -1;}
void net_controller_set_winner(controller *c,int w){unavailable();}
void menu_controller_hook(controller *c,int action){unavailable();}
void spec_controller_create(controller *c,int player,ENetHost *h,ENetPeer *l,hashmap *events){unavailable();}
void spec_controller_free(controller *c){unavailable();}
ENetPeer *spec_controller_get_lobby_connection(controller *c){unavailable();return NULL;}
ENetHost *spec_controller_get_host(controller *c){unavailable();return NULL;}
