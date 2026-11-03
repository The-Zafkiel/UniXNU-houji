// SPDX-License-Identifier: MIT
#pragma once
#include <cstdlib>
#include <initializer_list>

#ifndef HOUJI_METAL_QUIET
#define HOUJI_METAL_QUIET 0
#endif

// The iOS16 houji compositor uses BGRA8. Match the reference's BGRA policy:
// render in system memory until GMEM load/store colours are device-qualified.
// Autotune is an explicit comparison option. Concurrent binning stays off.
#ifndef HOUJI_TURNIP_RENDER_AUTOTUNE
#define HOUJI_TURNIP_RENDER_AUTOTUNE 0
#endif
#if HOUJI_TURNIP_RENDER_AUTOTUNE != 0 && HOUJI_TURNIP_RENDER_AUTOTUNE != 1
#error HOUJI_TURNIP_RENDER_AUTOTUNE must be zero or one
#endif

namespace houji_turnip {
inline void disableDiagnostics() {
    for(const char *key : {"SHENG_DIAG", "SHENG_GPU_TRACE", "SHENG_GPU_PIXELS", "SHENG_LOG_GPU",
            "NATIVE_METAL_JOURNAL", "NATIVE_METAL_TIMING", "NATIVE_METAL_WARNINGS",
            "NATIVE_METAL_ABORT_PROBE", "METAL2VULKAN_LOG_PIPELINES"})unsetenv(key);
    setenv("NATIVE_METAL_NO_LOG", "1", 1);
}
inline const char *renderMode() {
    return HOUJI_TURNIP_RENDER_AUTOTUNE ? "autotune" : "sysmem";
}
inline const char *defaultDebugFlags() {
    return HOUJI_TURNIP_RENDER_AUTOTUNE ? "nocb" : "sysmem,nocb";
}
inline void configure() {
    // Preserve an explicit process environment, as the existing plugin does.
    setenv("TU_DEBUG", defaultDebugFlags(), 0);
    if(HOUJI_METAL_QUIET)disableDiagnostics();
}
}
