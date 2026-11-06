/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Reserved firmware RAM can be mapped as Device by the iOS26 pmap. Keep
 * every destination access naturally aligned; never use memcpy/DC ZVA.
 */
#ifndef HOUJI_FIRMWARE_WRITE26_H
#define HOUJI_FIRMWARE_WRITE26_H
#include <stdint.h>
#include <stddef.h>
static inline void hjfw26_barrier(void) {
#if defined(__aarch64__) || defined(__arm64__)
 __asm__ volatile("dsb sy" ::: "memory");
#else
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}
static inline bool hjfw26_write(volatile void *mapping,uint64_t region,uint64_t offset,
                              const void *source,size_t bytes,bool zero) {
 const uintptr_t address=(uintptr_t)mapping, input=(uintptr_t)source;
 if(!address || (address&3) || !region || (region&3) || region-1>UINTPTR_MAX-address ||
    offset>region || bytes>region-offset || (!zero && bytes && (!source || bytes-1>UINTPTR_MAX-input)))return false;
 hjfw26_barrier();
 auto *words=(volatile uint32_t*)mapping;auto *src=(const uint8_t*)source;
 size_t done=0;
 while(done<bytes){
  const uint64_t at=offset+done;const unsigned lane=(unsigned)(at&3);
  unsigned count=4-lane;if(count>bytes-done)count=(unsigned)(bytes-done);
  uint32_t value=(lane || count!=4)?words[at/4]:0;
  for(unsigned i=0;i<count;i++){
   const unsigned shift=8*(lane+i);const uint32_t byte=zero?0:src[done+i];
   value=(value&~(UINT32_C(255)<<shift))|(byte<<shift);
  }
  words[at/4]=value;done+=count;
 }
 hjfw26_barrier();
 return true;
}
#endif
