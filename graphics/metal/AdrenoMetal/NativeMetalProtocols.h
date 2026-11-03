// SPDX-License-Identifier: MIT
// Object protocol declarations from UniXNU/AdrenoMetal 01bfb259. Frameworks
// check conformsToProtocol: before calling the methods the bridge implements.
// iOS 16 MPSCommandBufferImageCache rejects an undeclared MTLCommandBuffer,
// even when its userDictionary and command methods are present.
#import <Metal/Metal.h>
#include <objc/runtime.h>
namespace native_metal_protocols {
struct Binding { const char *object; Protocol *protocol; };
// Emit protocol references just as upstream interface declarations do. Name-only
// lookup registered only 17/19 protocols on iOS 16 in r299: some definitions
// were not discoverable in the loaded images. Existing target definitions still
// take precedence when the Objective-C runtime registers this image.
static const Binding bindings[] = {
    {"NBBuffer", @protocol(MTLBuffer)}, {"NBTexture", @protocol(MTLTexture)},
    {"NBLibrary", @protocol(MTLLibrary)}, {"NBFunction", @protocol(MTLFunction)},
    {"NBComputePipeline", @protocol(MTLComputePipelineState)}, {"NBRenderPipeline", @protocol(MTLRenderPipelineState)},
    {"NBSampler", @protocol(MTLSamplerState)}, {"NBDepthStencil", @protocol(MTLDepthStencilState)},
    {"NBEvent", @protocol(MTLEvent)}, {"NBSharedEvent", @protocol(MTLSharedEvent)}, {"NBFence", @protocol(MTLFence)},
    {"NBQueue", @protocol(MTLCommandQueue)}, {"NBCommandBuffer", @protocol(MTLCommandBuffer)},
    {"NBEncoder", @protocol(MTLCommandEncoder)}, {"NBComputeEncoder", @protocol(MTLComputeCommandEncoder)},
    {"NBRenderEncoder", @protocol(MTLRenderCommandEncoder)}, {"NBBlitEncoder", @protocol(MTLBlitCommandEncoder)},
    {"NBHeap", @protocol(MTLHeap)}, {"NBBinaryArchive", @protocol(MTLBinaryArchive)},
};
static unsigned install() {
    unsigned declared = 0;
    for (const auto &binding : bindings) {
        Class cls = objc_getClass(binding.object);
        Protocol *protocol = binding.protocol;
        if (!cls || !protocol) continue;
        class_addProtocol(cls, protocol);
        if (class_conformsToProtocol(cls, protocol)) ++declared;
    }
    return declared;
}
}
