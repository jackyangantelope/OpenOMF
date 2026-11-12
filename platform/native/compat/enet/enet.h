/* Only opaque pointer declarations are needed by offline controller headers.
 * No ENet functions or network behavior are emulated in the native target. */
#pragma once
typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
