/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * A bounded bring-up session on houji. The host must verify/prearm a no-wipe
 * recovery BCB before boot; this reset path performs no filesystem or UFS I/O.
 */
#include <kern/thread.h>
#include <kern/debug.h>
#include <pexpert/pexpert.h>
#include <stdint.h>

extern void IOSleep(unsigned int);
void houji26_reset_recovery(void) __attribute__((noreturn));
void houji26_start_recovery_timer(void);

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
    IOSleep((unsigned int)(uintptr_t)context * 1000U);
    houji26_reset_recovery();
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
