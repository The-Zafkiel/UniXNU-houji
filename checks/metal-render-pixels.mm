// SPDX-License-Identifier: MIT
// Run the production frontend on a real host Vulkan GPU and compare defined
// pixels with native Metal. This does not qualify Turnip, Adreno or scanout.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "NativeBridgeMetalFrontend.h"
#include "bridge/NativeMetalObjects.h"

// The plugin owns the idle primer in deployment. This instrument does not
// run a primer; these are only its telemetry counters, not GPU substitutes.
extern "C" {
uint32_t native_prime_compiling=0,native_prime_functions_resumed=0;
uint32_t native_prime_libraries_done=0,native_prime_libraries_total=0,native_prime_made=0,native_prime_rate=0;
}

#include "NativeMetalPixelProbe.h"
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=3)return 2;const bool apple=!std::strcmp(argv[1],"apple");
    id<MTLDevice> device=apple?MTLCreateSystemDefaultDevice():native_bridge_metal_open(argv[1],argv[2],0,nullptr);
    if(!device)return 2;NSError *error=nil;id<MTLLibrary> library;
    if(apple)library=[device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nstruct V{float4 p [[position]];float4 c [[flat]];};\nvertex V colored_vertex(device const float2 *p [[buffer(0)]],device const float4 *c [[buffer(1)]],uint i [[vertex_id]]){return {float4(p[i],0,1),c[i]};}\nfragment float4 colored_fragment(V v [[stage_in]]){return v.c;}" options:nil error:&error];
    else {size_t length=0;const void *air=nmb_fixture_air(&length);library=native_bridge_metal_library_with_air(air,length);}
    if(!library){std::fprintf(stderr,"library: %s\n",error.localizedDescription.UTF8String);return 2;}
    unsigned passed=0;if(!native_metal_pixels::run(device,library,apple,passed))return 1;
    std::puts("PASS: defined render, resolve preservation, half-float, blending, memoryless and ranged-copy pixels; host GPU only");return 0;
}}
