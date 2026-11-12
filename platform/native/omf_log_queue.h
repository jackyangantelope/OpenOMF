/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OMF_LOG_QUEUE_H
#define OMF_LOG_QUEUE_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef OMF_FILE_DIAG
#define OMF_LOG_CAPACITY 8192u
#else
#define OMF_LOG_CAPACITY 4096u
#endif
#define OMF_LOG_LINE_CAPACITY 512u
/* Caller serializes feed/drain. Publish complete lines only, including CRLF. */
typedef struct {
    char bytes[OMF_LOG_CAPACITY], line[OMF_LOG_LINE_CAPACITY];
    uint32_t head, tail, used, line_bytes, peak;
    uint32_t dropped_lines, dropped_bytes, sent_bytes;
} omf_log_queue;

static inline void omf_log_queue_feed(omf_log_queue *q, const char *data, unsigned length) {
    for (unsigned i=0; i<length; ++i) {
        char c=data[i];
        if (q->line_bytes < OMF_LOG_LINE_CAPACITY) q->line[q->line_bytes]=c;
        ++q->line_bytes;
        if (c!='\n') continue;
        if (q->line_bytes > OMF_LOG_LINE_CAPACITY || q->line_bytes > OMF_LOG_CAPACITY-q->used) {
            ++q->dropped_lines;
            q->dropped_bytes+=q->line_bytes;
        } else {
            unsigned first=OMF_LOG_CAPACITY-q->head;
            if (first>q->line_bytes) first=q->line_bytes;
            memcpy(q->bytes+q->head,q->line,first);
            memcpy(q->bytes,q->line+first,q->line_bytes-first);
            q->head=(q->head+q->line_bytes)%OMF_LOG_CAPACITY;
            q->used+=q->line_bytes;
            if(q->used>q->peak)q->peak=q->used;
        }
        q->line_bytes=0;
    }
}

static inline char omf_log_queue_pop(omf_log_queue *q) {
    char c=q->bytes[q->tail];
    q->tail=(q->tail+1u)%OMF_LOG_CAPACITY;
    --q->used;
    ++q->sent_bytes;
    return c;
}
#endif
