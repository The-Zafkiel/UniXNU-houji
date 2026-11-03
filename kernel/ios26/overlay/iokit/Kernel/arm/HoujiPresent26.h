/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef HOUJI_PRESENT26_H
#define HOUJI_PRESENT26_H
#include "HoujiSurfaceAcquire.h"

extern "C" int native_adreno_present_begin_frame(uint32_t fence);
extern "C" uint32_t native_adreno_present_capture_fence(void);
extern "C" void native_adreno_present_end(void);

/* Hold the qualified issuer gate from completed producer/current-tail fences
 * through the CPU copy. Retained surface references outlive this lease. */
class HoujiPresentLease26 {
    int token;
    uint32_t captured;
public:
    explicit HoujiPresentLease26(uint32_t fence) : token(native_adreno_present_begin_frame(fence)), captured(fence) {}
    ~HoujiPresentLease26() { if (token > 0) native_adreno_present_end(); }
    bool ready() const { return token > 0 || (token == 0 && captured == 0); }
    bool gpu() const { return token > 0; }
    HoujiPresentLease26(const HoujiPresentLease26 &) = delete;
    HoujiPresentLease26 &operator=(const HoujiPresentLease26 &) = delete;
};

/* The descriptor/map stays pinned by the retained IOSurface. Validate the
 * whole rounded range and every CPU page before any maintenance. The native
 * issuer gate excludes later GPU writes throughout. No Device memory is
 * accepted; a Normal-NC view needs no CPU cache operation. */
template<class Operations>
bool houji26AcquirePixels(uintptr_t base, size_t capacity, uintptr_t pixels, size_t bytes,
    unsigned line, unsigned page, Operations &ops)
{
    HoujiSurfaceCacheRange range;
    if (line < 16 || line > 2048 || bytes > 256U * 1024 * 1024 || page < line || page > 65536 ||
        !page || (page & (page - 1)) || page % line ||
        !houji_surface_cache_range(base, capacity, pixels, bytes, line, &range)) return false;
    for (uintptr_t cursor = range.begin; cursor < range.end;) {
        unsigned attribute = ops.attribute(cursor);
        if (attribute != 0xff && attribute != 0x44) return false;
        size_t chunk = page - (cursor & (page - 1));
        if (chunk > range.end - cursor) chunk = range.end - cursor;
        cursor += chunk;
    }
    for (uintptr_t cursor = range.begin; cursor < range.end;) {
        size_t chunk = page - (cursor & (page - 1));
        if (chunk > range.end - cursor) chunk = range.end - cursor;
        unsigned attribute = ops.attribute(cursor);
        if (attribute == 0xff) {
            for (uintptr_t address = cursor; address < cursor + chunk; address += line) ops.cleanInvalidate(address);
        } else if (attribute != 0x44) return false;
        cursor += chunk;
    }
    ops.barrier();
    return true;
}
#endif
