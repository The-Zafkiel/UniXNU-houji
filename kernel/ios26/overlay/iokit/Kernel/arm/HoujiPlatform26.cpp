/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Houji hardware policies on the reference's XNU12377 platform. The platform
 * owns CPU registration, PSCI, GIC and IOSurface; this file retains SM8650
 * EPSS/TSENS and translates the existing touch producer to the iOS26 input ABI.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <kern/processor.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/proc.h>
extern "C" {
#include <sys/redwood_input.h>
}
#include "HoujiCpuPolicy.h"
#include "HoujiThermal.inc"
#include "HoujiCpuDvfs.inc"

extern "C" uint64_t hakios_cpu_idle_mach(int cpu);
extern "C" uint32_t houji26_running_mask(void);
extern "C" void houji26_irq_counts(unsigned cpu, uint64_t *timer, uint64_t *ipi);
extern "C" processor_t thread_bind(processor_t processor);
extern uint32_t processor_avail_count;

extern "C" uint64_t houji_cpu_idle_mach(int cpu) { return hakios_cpu_idle_mach(cpu); }
extern "C" void houji_cpu_dvfs_start(void) { HoujiCpuDvfs::start(); }

static uint32_t proofStarted, proofMask, proofFailed;
static void proofWorker(void *argument, wait_result_t) {
    unsigned cpu = static_cast<unsigned>(reinterpret_cast<uintptr_t>(argument));
    processor_t processor = cpu_to_processor(cpu);
    if (processor == PROCESSOR_NULL) {
        __atomic_fetch_or(&proofFailed, 1U << cpu, __ATOMIC_RELEASE);
        return;
    }
    thread_bind(processor);
    thread_block(THREAD_CONTINUE_NULL);
    unsigned current = static_cast<unsigned>(cpu_number());
    uint64_t mpidr = __builtin_arm_rsr64("MPIDR_EL1") & 0xffffff;
    if (current != cpu || mpidr != static_cast<uint64_t>(cpu) << 8) {
        __atomic_fetch_or(&proofFailed, 1U << cpu, __ATOMIC_RELEASE);
    } else {
        __atomic_fetch_or(&proofMask, 1U << cpu, __ATOMIC_RELEASE);
    }
    thread_bind(PROCESSOR_NULL);
}

extern "C" bool houji_smp_preflight(void) {
    const ml_topology_info_t *topology = ml_get_topology_info();
    if (!topology || !topology->num_cpus || topology->num_cpus > 8 ||
        processor_avail_count != topology->num_cpus) return false;
    unsigned count = topology->num_cpus;
    for (unsigned cpu = 0; cpu < count; ++cpu)
        if (topology->cpus[cpu].phys_id != cpu << 8) return false;
    unsigned expected = 0;
    if (__atomic_compare_exchange_n(&proofStarted, &expected, 1U, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        for (unsigned cpu = 0; cpu < count; ++cpu) {
            thread_t thread = THREAD_NULL;
            if (kernel_thread_start(proofWorker, reinterpret_cast<void *>(static_cast<uintptr_t>(cpu)), &thread) != KERN_SUCCESS) {
                __atomic_fetch_or(&proofFailed, 1U << cpu, __ATOMIC_RELEASE);
                return false;
            }
            thread_deallocate(thread);
        }
    }
    uint64_t deadline = 0;
    nanoseconds_to_deadline(5 * NSEC_PER_SEC, &deadline);
    const uint32_t wanted = (1U << count) - 1;
    while (__atomic_load_n(&proofMask, __ATOMIC_ACQUIRE) != wanted) {
        if (__atomic_load_n(&proofFailed, __ATOMIC_ACQUIRE) || mach_absolute_time() >= deadline) return false;
        IOSleep(1);
    }
    if (count > 1 && (houji26_running_mask() & wanted) != wanted) return false;
    for (unsigned cpu = 0; cpu < count; ++cpu) {
        uint64_t timer = 0, ipi = 0;
        houji26_irq_counts(cpu, &timer, &ipi);
        if (!timer || (count > 1 && !ipi)) return false;
    }
    return __atomic_load_n(&proofFailed, __ATOMIC_ACQUIRE) == 0;
}

extern "C" void houji_smp_report(void) {
    IOLog("HOUJI26 SMP: online=%u PSCI=%x workers=%x failed=%x; actual kernel state\n",
        processor_avail_count, houji26_running_mask(), proofMask, proofFailed);
    const ml_topology_info_t *topology = ml_get_topology_info();
    if (!topology) return;
    for (unsigned cpu = 0; cpu < topology->num_cpus; ++cpu) {
        uint64_t timer = 0, ipi = 0;
        houji26_irq_counts(cpu, &timer, &ipi);
        IOLog("HOUJI26 CPU%u: phys=%x timer=%llu ipi=%llu\n", cpu,
            topology->cpus[cpu].phys_id, timer, ipi);
    }
}
extern "C" void houji_cpu_scheduler_report(void) { houji_smp_report(); }

extern "C" void sweet_hid_touch(int finger, int down, int x, int y) {
    static uint32_t active;
    if (finger < 0 || finger >= 10 || x < 0 || x >= 1200 || y < 0 || y >= 2670) return;
    unsigned bit = 1U << finger;
    unsigned previous = __atomic_load_n(&active, __ATOMIC_ACQUIRE);
    unsigned kind = down ? ((previous & bit) ? REDWOOD_TOUCH_MOVE : REDWOOD_TOUCH_DOWN) : REDWOOD_TOUCH_UP;
    if (down) __atomic_fetch_or(&active, bit, __ATOMIC_ACQ_REL);
    else __atomic_fetch_and(&active, ~bit, __ATOMIC_ACQ_REL);
    redwood_touch_post(kind, finger, static_cast<uint32_t>(x) * 65535U / 1199U,
        static_cast<uint32_t>(y) * 65535U / 2669U);
    if (down) houji_dvfs_touch();
}

/* These are optional diagnostics of the old framework, not device functions.
 * Keep the quiet configuration explicit; do not fabricate old counters for
 * the new platform's MAC, sampler or network stack.
 */
extern "C" unsigned houji_sample_snapshot(char *, unsigned) { return 0; }
extern "C" unsigned houji_network_stack_snapshot(char *, unsigned) { return 0; }
extern "C" unsigned houji_tcc_extension_snapshot(char *, unsigned) { return 0; }
extern "C" void houji_sandbox_abi_report(void) {
    IOLog("HOUJI26 MAC: reference minimac; legacy iOS16 diagnostic counters unavailable\n");
}
