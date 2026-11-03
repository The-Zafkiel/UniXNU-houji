// SPDX-License-Identifier: MIT
// Resolve routing follows UniXNU/AdrenoMetal 171d30b and dada's single-sample
// path. Keep Load and StoreAndResolve contents instead of always redirecting
// rendering to the resolve target. This is a quality/performance policy, not
// a claim that the Vulkan driver's multisample implementation is incorrect.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace native_metal_render {
#ifndef HOUJI_METAL_SINGLE_SAMPLE
#define HOUJI_METAL_SINGLE_SAMPLE 1
#endif
static_assert(HOUJI_METAL_SINGLE_SAMPLE == 0 || HOUJI_METAL_SINGLE_SAMPLE == 1,
              "sample policy must be requested (0) or single (1)");
inline bool singleSample() {
    static const bool enabled = [] {
        const char *value = std::getenv("NATIVE_METAL_SINGLE_SAMPLE");
        if (value && !std::strcmp(value, "0")) return false;
        if (value && !std::strcmp(value, "1")) return true;
        return HOUJI_METAL_SINGLE_SAMPLE != 0;
    }();
    return enabled;
}
enum class ResolveRoute { Native, Unused, Direct, Copy };
inline ResolveRoute resolveRoute(bool single, bool target, uint64_t load, uint64_t store) {
    if (!single) return ResolveRoute::Native;
    if (!target || (store != 2 && store != 3)) return ResolveRoute::Unused;
    // Metal: Load=1, MultisampleResolve=2, StoreAndMultisampleResolve=3.
    return load != 1 && store == 2 ? ResolveRoute::Direct : ResolveRoute::Copy;
}
inline uint64_t mipExtent(uint64_t extent, uint64_t level) {
    return level < 64 && (extent >> level) ? extent >> level : 1;
}
} // namespace native_metal_render
