#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool omf_fault_frame_address(uintptr_t raw,uint32_t er,uint32_t cfsr,
                                         uintptr_t lo,uintptr_t hi,uintptr_t *out){
    /* Arm Cortex-M33 DGUI figure 2-6: R0..xPSR precede the FP
     * context, so R0 is at raw SP for BOTH basic and FP frames.
     * Cross-security additional-state frames are not decoded here. */
    if((er&0xffffff80u)!=0xffffff80u || !(er&32u) || (er&2u))return false;
    uintptr_t f=raw;
    if((cfsr&0x103838u)||(f&3u)||hi<lo||hi-lo<32u||f<lo||f>hi-32u)return false;
    *out=f;return true;
}
static inline unsigned omf_fault_stack_words(uintptr_t raw,uint32_t cfsr,
                                           uintptr_t lo,uintptr_t hi){
    if((cfsr&0x103838u)||(raw&3u)||hi<lo||raw<lo||raw>=hi)return 0;
    uintptr_t n=(hi-raw)/4u;
    return (unsigned)(n>96u?96u:n);
}
