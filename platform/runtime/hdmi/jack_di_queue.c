/* Reuse the proven DI scheduler/queue, but reserve its memory at link time.
 * No startup malloc failure and no dependency on the emulator PSRAM heap. */
#include "hstx_packet.h"
#ifndef JACK_DI_RING_CAPACITY
#define JACK_DI_RING_CAPACITY 128
#endif
#define DI_RING_BUFFER_SIZE JACK_DI_RING_CAPACITY
static hstx_data_island_t jack_data_islands[DI_RING_BUFFER_SIZE];
#define HSTX_DI_RING_ADDRESS jack_data_islands
#include "hstx_data_island_queue.c"
