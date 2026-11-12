/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OMF_LOG_ASYNC_H
#define OMF_LOG_ASYNC_H
#ifdef __cplusplus
extern "C" {
#endif
#ifdef OMF_ASYNC_LOG
void omf_log_init(void);
void omf_log_service(void);
void omf_log_report(void);
#else
static inline void omf_log_init(void) {}
static inline void omf_log_service(void) {}
static inline void omf_log_report(void) {}
#endif
#ifdef __cplusplus
}
#endif
#endif
