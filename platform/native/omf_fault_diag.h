#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void omf_fault_init(void);
void omf_fault_arm(void);
void omf_fault_mark(uint32_t phase);
void omf_fault_report(void);
#ifdef __cplusplus
}
#endif
