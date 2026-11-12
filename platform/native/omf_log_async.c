/* SPDX-License-Identifier: BSD-3-Clause */
#include "omf_log_async.h"
#include "omf_log_queue.h"
#include "omf_loop_diag.h"
#include "pico/critical_section.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_uart.h"
#include "pico/time.h"
#include "hardware/uart.h"
#include <stdio.h>

static omf_log_queue log_queue;
static critical_section_t log_lock;
static bool log_ready;
static uint32_t service_max_us, enqueue_max_us;

/* No UART IRQ/DMA or audio work here. Only this adapter writes TX after init.
 * Lock excludes other stdio callers; its body never waits for the UART. */
void omf_log_service(void) {
    if(!log_ready)return;
    OMF_LOOP_SCOPE(OMF_LOOP_LOG);
    uint32_t start=time_us_32();
    critical_section_enter_blocking(&log_lock);
    for(unsigned n=0; n<32u && log_queue.used && uart_is_writable(uart_default); ++n)
        uart_get_hw(uart_default)->dr=(uint8_t)omf_log_queue_pop(&log_queue);
    uint32_t elapsed=time_us_32()-start;
    if(elapsed>service_max_us)service_max_us=elapsed;
    critical_section_exit(&log_lock);
}

static void enqueue(const char *data,int length) {
    if(length<=0)return;
    OMF_LOOP_SCOPE(OMF_LOOP_LOG);
    uint32_t start=time_us_32();
    critical_section_enter_blocking(&log_lock);
    omf_log_queue_feed(&log_queue,data,(unsigned)length);
    uint32_t elapsed=time_us_32()-start;
    if(elapsed>enqueue_max_us)enqueue_max_us=elapsed;
    critical_section_exit(&log_lock);
    omf_log_service();
}

void omf_log_init(void) {
    if(log_ready)return;
    critical_section_init(&log_lock);
    /* Keep the SDK's UART RX and CRLF behavior; replace TX and blocking flush.
     * Called once on core0 after runtime init, before the game/picker starts. */
    stdio_uart.out_chars=enqueue;
    stdio_uart.out_flush=omf_log_service;
    log_ready=true;
    printf("OMF_LOG_ASYNC active=1 queue_bytes=%u line_bytes=%u tx_budget=32\n",
           OMF_LOG_CAPACITY,OMF_LOG_LINE_CAPACITY);
}

void omf_log_report(void) {
    uint32_t used,peak,lines,bytes,sent,service,feed;
    critical_section_enter_blocking(&log_lock);
    used=log_queue.used;peak=log_queue.peak;
    lines=log_queue.dropped_lines;bytes=log_queue.dropped_bytes;sent=log_queue.sent_bytes;
    service=service_max_us;feed=enqueue_max_us;
    critical_section_exit(&log_lock);
    printf("OMF_LOG_ASYNC queued=%lu peak=%lu dropped_lines=%lu dropped_bytes=%lu sent_bytes=%lu service_max_us=%lu enqueue_max_us=%lu\n",
           (unsigned long)used,(unsigned long)peak,(unsigned long)lines,(unsigned long)bytes,
           (unsigned long)sent,(unsigned long)service,(unsigned long)feed);
}
