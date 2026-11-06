/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI26_RESERVED_NC_H
#define HOUJI26_RESERVED_NC_H
#include <stdint.h>
#define HJNC_PAGE UINT64_C(16384)
#define HJNC_TARGET UINT64_C(0x9b098000)
typedef struct { uint32_t ready; uint64_t metadata; } HjncPolicy;
static inline int hjnc_span(uint64_t base,uint64_t bytes) {
    return base && bytes && base<=UINT64_MAX-bytes;
}
static inline int hjnc_overlap(uint64_t base,uint64_t bytes,uint64_t other,uint64_t length) {
    return base<other+length && other<base+bytes;
}
/* The caller must first verify the complete current-trial HZR record, including
 * EFI/GCD ownership and all loader AT observations. Only the exact two 16K
 * pages are published; raw alignment padding and the PIL span are excluded.
 */
static inline int hjnc_register(HjncPolicy *p,uint64_t metadata,uint64_t raw,uint64_t raw_bytes,
                               uint64_t kernel,uint64_t kernel_bytes) {
    if (!p || !hjnc_span(raw,raw_bytes) || !hjnc_span(kernel,kernel_bytes) ||
        raw<UINT64_C(0x80000000) || (raw&4095) || raw_bytes!=7*4096 ||
        raw+raw_bytes>UINT64_C(0x100000000) || (metadata&(HJNC_PAGE-1)) ||
        metadata<raw || metadata>raw+raw_bytes-HJNC_PAGE ||
        hjnc_overlap(raw,raw_bytes,HJNC_TARGET,HJNC_PAGE) ||
        hjnc_overlap(raw,raw_bytes,kernel,kernel_bytes) ||
        hjnc_overlap(HJNC_TARGET,HJNC_PAGE,kernel,kernel_bytes)) return 0;
    uint32_t expected=0;
    if (!__atomic_compare_exchange_n(&p->ready,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
    p->metadata=metadata;
    __atomic_store_n(&p->ready,2,__ATOMIC_RELEASE);
    return 1;
}
static inline int hjnc_covers(const HjncPolicy *p,uint64_t address,uint64_t bytes) {
    if (!p || __atomic_load_n(&p->ready,__ATOMIC_ACQUIRE)!=2 || !hjnc_span(address,bytes) || bytes>HJNC_PAGE) return 0;
    return (address>=p->metadata && address-p->metadata<=HJNC_PAGE-bytes) ||
           (address>=HJNC_TARGET && address-HJNC_TARGET<=HJNC_PAGE-bytes);
}
#ifdef __cplusplus
extern "C" {
#endif
int houji26_register_reserved_nc(uint64_t metadata,uint64_t raw,uint64_t raw_bytes);
int houji26_reserved_nc_covers(uint64_t address,uint64_t bytes);
#ifdef __cplusplus
}
#endif
#endif
