#include "omf_fault_diag.h"
#include "omf_fault_frame.h"
#include "pico/stdlib.h"
#include "hardware/exception.h"
#include "hardware/uart.h"
#include "hardware/structs/scb.h"
#include <stdio.h>
extern uint32_t __StackBottom[],__StackTop[],__StackOneBottom[],__StackOneTop[];
/* Separate per-core crash stacks; never depend on the interrupted MSP. */
uint32_t omf_fault_stacks[2][256] __attribute__((aligned(8)));
static volatile uint32_t phase_now,phase_at,min_sp=UINT32_MAX;
static uint32_t *paint_end;
static struct repeating_timer monitor;
static const uint32_t pattern=0xa53cc35a;
static void raw_char(char c){
    for(unsigned n=0;n<100000;n++)if(uart_is_writable(uart_default)){
        uart_get_hw(uart_default)->dr=(uint8_t)c;return;
    }
}
static void raw_text(const char*s){while(*s)raw_char(*s++);}
static void hex(const char*k,uint32_t v){
    raw_text(k);raw_char('=');for(int i=28;i>=0;i-=4)raw_char("0123456789abcdef"[(v>>i)&15]);raw_char(' ');
}
static uint32_t unused(uint32_t*b,uint32_t*e){uint32_t*p=b;while(p<e&&*(volatile uint32_t*)p==pattern)p++;return (uint32_t)((uintptr_t)p-(uintptr_t)b);}
void omf_fault_mark(uint32_t p){
    uint32_t sp;__asm volatile("mrs %0, msp":"=r"(sp));
    if(sp<min_sp)min_sp=sp;
    phase_now=p;phase_at=time_us_32();
}
static void raw_progress(void){
    hex("phase",phase_now);hex("age_us",time_us_32()-phase_at);hex("min_msp",min_sp);
    hex("c0_unused",unused(__StackBottom,paint_end));hex("c1_unused",unused(__StackOneBottom,__StackOneTop));
}
/* Called with interrupts masked, on a reserved crash stack. No printf,
 * allocation, stdio locks or async log calls are allowed here. */
void __attribute__((noreturn,used)) omf_fault_capture(uint32_t *frame,uint32_t er,uint32_t msp,uint32_t psp){
    raw_text("\r\nOMF_FAULT ");hex("core",get_core_num());hex("exc_return",er);
    hex("msp",msp);hex("psp",psp);hex("frame",(uint32_t)(uintptr_t)frame);
    uint32_t cfsr=scb_hw->cfsr;
    hex("cfsr",cfsr);hex("hfsr",scb_hw->hfsr);hex("bfar",scb_hw->bfar);hex("mmfar",scb_hw->mmfar);
    uint32_t control,ipsr;__asm volatile("mrs %0, control":"=r"(control));__asm volatile("mrs %0, ipsr":"=r"(ipsr));
    hex("control",control);hex("ipsr",ipsr);raw_progress();raw_text("\r\n");
    /* Integer state starts at raw SP even when an FP context is allocated.
     * Never dereference a frame with stacking errors or outside stack SRAM. */
    uintptr_t f;
    uintptr_t lo=get_core_num()?(uintptr_t)__StackOneBottom:(uintptr_t)__StackBottom;
    uintptr_t hi=get_core_num()?(uintptr_t)__StackOneTop:(uintptr_t)__StackTop;
    if(omf_fault_frame_address((uintptr_t)frame,er,cfsr,lo,hi,&f)){
        const uint32_t*p=(const uint32_t*)f;
        raw_text("OMF_FAULT_FRAME ");hex("addr",(uint32_t)f);hex("r0",p[0]);hex("r1",p[1]);hex("r2",p[2]);hex("r3",p[3]);
        hex("r12",p[4]);hex("lr",p[5]);hex("pc",p[6]);hex("xpsr",p[7]);raw_text("\r\n");
    }else raw_text("OMF_FAULT_FRAME invalid=1\r\n");
    /* Bounded raw evidence, not an inferred backtrace. No heap or printf. */
    unsigned words=omf_fault_stack_words((uintptr_t)frame,cfsr,lo,hi);
    raw_text("OMF_FAULT_STACK ");hex("base",(uint32_t)(uintptr_t)frame);hex("words",words);raw_text("\r\n");
    for(unsigned i=0;i<words;i+=4u){
        raw_text("OMF_FAULT_RAW ");hex("addr",(uint32_t)(uintptr_t)(frame+i));
        for(unsigned j=0;j<4u&&i+j<words;j++)hex("word",((volatile uint32_t*)frame)[i+j]);
        raw_text("\r\n");
    }
    raw_text("OMF_FAULT_END\r\n");
    for(;;)__asm volatile("wfi");
}
static void __attribute__((naked)) fault_entry(void){
    __asm volatile("mrs r2, msp\n mrs r3, psp\n tst lr,#4\n ite eq\n moveq r0,r2\n movne r0,r3\n mov r1,lr\n cpsid i\n"
      "ldr r4,=0xd0000000\n ldr r4,[r4]\n adds r4,#1\n lsls r4,r4,#10\n ldr r5,=omf_fault_stacks\n adds r4,r4,r5\n movs r5,#0\n msr msplim,r5\n msr msp,r4\n b omf_fault_capture\n");
}
static bool monitor_tick(struct repeating_timer*t){
    (void)t;
    if(time_us_32()-phase_at>5000000u){raw_text("\r\nOMF_STALL ");raw_progress();raw_text("\r\n");}
    return true;
}
void omf_fault_init(void){
    uint32_t sp;__asm volatile("mrs %0,msp":"=r"(sp));
    paint_end=(uint32_t*)(uintptr_t)((sp-512u)&~3u);
    for(uint32_t*p=__StackBottom;p<paint_end;p++)*p=pattern;
    for(uint32_t*p=__StackOneBottom;p<__StackOneTop;p++)*p=pattern;
    omf_fault_mark(0);
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION,fault_entry);
    exception_set_exclusive_handler(MEMMANAGE_EXCEPTION,fault_entry);
    exception_set_exclusive_handler(BUSFAULT_EXCEPTION,fault_entry);
    exception_set_exclusive_handler(USAGEFAULT_EXCEPTION,fault_entry);
}
void omf_fault_arm(void){
    omf_fault_mark(0);
    bool ok=add_repeating_timer_ms(-1000,monitor_tick,NULL,&monitor);
    printf("OMF_FAULT_DIAG active=1 monitor=%u stale_us=5000000 frame_v=2 raw_words_max=96\n",ok);
}
void omf_fault_report(void){
    printf("OMF_STACK c0_unused=%lu c0_painted=%lu c1_unused=%lu min_msp=%08lx phase=%lu\n",
      (unsigned long)unused(__StackBottom,paint_end),(unsigned long)((uintptr_t)paint_end-(uintptr_t)__StackBottom),
      (unsigned long)unused(__StackOneBottom,__StackOneTop),(unsigned long)min_sp,(unsigned long)phase_now);
}
