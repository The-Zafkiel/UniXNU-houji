/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * A bounded bring-up session on houji. The host must verify/prearm a no-wipe
 * recovery BCB before boot; this reset path performs no filesystem or UFS I/O.
 */
#include <kern/thread.h>
#include <kern/debug.h>
#include <pexpert/pexpert.h>
#include <console/video_console.h>
#include <stdint.h>
#include "HoujiTerminal26.h"
#include "../../iokit/Kernel/houji-ufs-root/HoujiZapTrial.h"

extern void IOSleep(unsigned int);
void houji26_reset_recovery(void) __attribute__((noreturn));
void houji26_start_recovery_timer(void);
void houji26_finish_trial(unsigned reason) __attribute__((noreturn));
void houji26_capture_reason(const char *text);
const void *houji26_panic_record(void);

extern struct vc_info vinfo;
extern unsigned char iso_font[256 * 16];
static unsigned terminal_entered;
static Hjt26Reason first_panic __attribute__((aligned(16384)));

const void *houji26_panic_record(void) { return &first_panic.record; }
void houji26_capture_reason(const char *text) {
    hjt26_capture(&first_panic, text, kHoujiZapTrial);
    /* Normal RAM only. A reset may preserve this exact tagged record; the
     * next firmware can accept it only by trial identity and CRC. */
    if (__atomic_load_n(&first_panic.state, __ATOMIC_ACQUIRE) == 2) {
        uintptr_t at = (uintptr_t)&first_panic.record & ~(uintptr_t)63;
        uintptr_t end = (uintptr_t)&first_panic.record + sizeof(first_panic.record);
        for (; at < end; at += 64) __asm__ volatile("dc cvac, %0" : : "r"(at) : "memory");
        __asm__ volatile("dsb sy" ::: "memory");
    }
}

void houji26_finish_trial(unsigned reason) {
    if (__atomic_exchange_n(&terminal_entered, 1U, __ATOMIC_ACQ_REL))
        houji26_reset_recovery();
    /* A non-Apple loader may supply no iBoot panic region. Capture the existing
     * formatter's first reason before panic handling, including early panics. */
    if (__atomic_load_n(&first_panic.state, __ATOMIC_ACQUIRE) == 2) reason = 2;
    const char *title = reason == 1 ? "HOUJI IOS26: TIME LIMIT; RECOVERY IN 20S" :
        reason == 2 ? "HOUJI IOS26: KERNEL PANIC" : "HOUJI IOS26: SYSTEM HALT OR RESTART";
    const char *detail = "Returning to Recovery in 20 seconds.\n";
    size_t detail_length = __builtin_strlen(detail);
    if (reason == 2 && __atomic_load_n(&first_panic.state, __ATOMIC_ACQUIRE) == 2) {
        detail = first_panic.record.text;
        detail_length = __builtin_strlen(detail);
    } else if (reason == 2) {
        uintptr_t first = (uintptr_t)debug_buf_base, last = (uintptr_t)debug_buf_ptr;
        if (first && last > first && last - first <= 256 * 1024) {
            detail = debug_buf_base;
            detail_length = last - first;
        }
    }
    Hjt26Surface surface;
    /* Reuse the console's live mapping. Never map MMIO, allocate or use UFS
     * from a panic, and never interpret a boot physical pointer as a VA. */
    uintptr_t base = (uintptr_t)vinfo.v_baseaddr;
    size_t bytes = (size_t)vinfo.v_rowbytes * vinfo.v_height;
    if ((base >> 48) == 0xffff && hjt26_surface(&surface, base, vinfo.v_width,
        vinfo.v_height, vinfo.v_rowbytes, vinfo.v_depth, bytes)) {
        hjt26_render(&surface, iso_font, title, __builtin_strlen(title), detail, detail_length);
        __asm__ volatile("dsb sy" ::: "memory");
    }
    uint64_t frequency = __builtin_arm_rsr64("CNTFRQ_EL0");
    uint64_t started = __builtin_arm_rsr64("CNTPCT_EL0");
    if (frequency >= 1000000 && frequency <= 1000000000)
        while (__builtin_arm_rsr64("CNTPCT_EL0") - started < frequency * 20)
            __asm__ volatile("yield");
    houji26_reset_recovery();
}

void houji26_reset_recovery(void) {
    /* The captured SM8650 FDT and the qualified iOS16 reset path use SMC. */
    __asm__ volatile("dsb sy" ::: "memory");
    register uint64_t function __asm__("x0") = 0x84000009;
    __asm__ volatile("smc #0" : "+r"(function) : : "x1","x2","x3","x4","x5","x6","x7",
        "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","cc","memory");
    for (;;) __asm__ volatile("wfi");
}
static void recovery_worker(void *context, wait_result_t result) {
    (void)result;
    /* Include the terminal screen in the requested session limit. */
    IOSleep(((unsigned int)(uintptr_t)context - 20U) * 1000U);
    houji26_finish_trial(1);
}
void houji26_start_recovery_timer(void) {
    int requested = 0, seconds = 90;
    PE_parse_boot_argn("houji_return_recovery", &requested, sizeof(requested));
    if (requested != 1) return;
    PE_parse_boot_argn("houji_trial_seconds", &seconds, sizeof(seconds));
    if (seconds < 30 || seconds > 300) panic("HOUJI26: invalid recovery deadline");
    thread_t thread = THREAD_NULL;
    if (kernel_thread_start(recovery_worker, (void *)(uintptr_t)seconds, &thread) != KERN_SUCCESS)
        panic("HOUJI26: cannot arm recovery deadline");
    thread_deallocate(thread);
}
