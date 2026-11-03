// SPDX-License-Identifier: MIT
// Defined-pixel qualification shared by the real host GPU check and the
// explicit, bounded-device-trial probe. No operating-system shader bytes.
#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "NativeBridgeMetalFrontend.h"
#include "bridge/NativeMetalObjects.h"
namespace native_metal_pixels {
static bool finish(id<MTLCommandBuffer> commands) {
    [commands commit];[commands waitUntilCompleted];
    if(commands.status==MTLCommandBufferStatusCompleted)return true;
    std::fprintf(stderr,"command failed: %s\n",commands.error.localizedDescription.UTF8String);return false;
}
static bool pixels(id<MTLTexture> texture,bool loaded,bool half,bool blended) {
    const size_t stride=half?8:4;
    std::vector<uint8_t> bytes(texture.width*texture.height*stride);
    [texture getBytes:bytes.data() bytesPerRow:texture.width*stride fromRegion:MTLRegionMake2D(0,0,texture.width,texture.height) mipmapLevel:0];
    for(size_t y=0;y<texture.height;y++)for(size_t x=0;x<texture.width;x++) {
        const bool drawn=x<texture.width/2;
        const float expected[4]={drawn?(blended?0.5f:1.0f):0.0f,!drawn && loaded?1.0f:0.0f,
                                  loaded?0.0f:(drawn?(blended?0.5f:0.0f):1.0f),1.0f};
        for(size_t c=0;c<4;c++) {
            const size_t channel=!half && c<3?2-c:c;
            float actual;
            if(half){_Float16 value;std::memcpy(&value,bytes.data()+(y*texture.width+x)*stride+c*2,2);actual=value;}
            else actual=bytes[(y*texture.width+x)*stride+channel]/255.0f;
            if(!std::isfinite(actual) || std::fabs(actual-expected[c])>0.006f) {
                std::fprintf(stderr,"pixel (%zu,%zu) channel %zu: %g != %g\n",x,y,c,actual,expected[c]);return false;
            }
        }
    }
    return true;
}
static bool resolve(id<MTLDevice> device,id<MTLLibrary> library,bool loaded,bool preserve,bool half,bool blended,bool memoryless) {
    const MTLPixelFormat format=half?MTLPixelFormatRGBA16Float:MTLPixelFormatBGRA8Unorm;
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:16 height:16 mipmapped:NO];
    td.usage=MTLTextureUsageRenderTarget;td.textureType=MTLTextureType2DMultisample;td.sampleCount=4;
    td.storageMode=memoryless?MTLStorageModeMemoryless:MTLStorageModePrivate;
    id<MTLTexture> source=[device newTextureWithDescriptor:td];
    td.textureType=MTLTextureType2D;td.sampleCount=1;td.storageMode=MTLStorageModeShared;
    id<MTLTexture> target=[device newTextureWithDescriptor:td],again=[device newTextureWithDescriptor:td];
    auto pd=[MTLRenderPipelineDescriptor new];pd.rasterSampleCount=4;
    pd.vertexFunction=[library newFunctionWithName:@"colored_vertex"];pd.fragmentFunction=[library newFunctionWithName:@"colored_fragment"];
    pd.colorAttachments[0].pixelFormat=format;pd.colorAttachments[0].blendingEnabled=blended;
    pd.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    NSError *error=nil;id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];
    if(!source || !target || !again || !pipeline){std::fprintf(stderr,"resource/pipeline: %s\n",error.localizedDescription.UTF8String);return false;}
    const float positions[6][2]={{-1,-1},{0,-1},{-1,1},{-1,1},{0,-1},{0,1}};
    float colors[6][4];for(auto &color:colors){color[0]=1;color[1]=color[2]=0;color[3]=blended?0.5f:1.0f;}
    auto vertices=[device newBufferWithBytes:positions length:sizeof positions options:MTLResourceStorageModeShared];
    auto tint=[device newBufferWithBytes:colors length:sizeof colors options:MTLResourceStorageModeShared];
    id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> commands=[queue commandBuffer];
    if(loaded) {
        auto initial=[MTLRenderPassDescriptor renderPassDescriptor];initial.colorAttachments[0].texture=source;
        initial.colorAttachments[0].loadAction=MTLLoadActionClear;initial.colorAttachments[0].clearColor=MTLClearColorMake(0,1,0,1);
        initial.colorAttachments[0].storeAction=MTLStoreActionStore;
        [[commands renderCommandEncoderWithDescriptor:initial] endEncoding];
    }
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=source;
    pass.colorAttachments[0].resolveTexture=target;pass.colorAttachments[0].loadAction=loaded?MTLLoadActionLoad:MTLLoadActionClear;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,1,1);
    pass.colorAttachments[0].storeAction=preserve?MTLStoreActionStoreAndMultisampleResolve:MTLStoreActionMultisampleResolve;
    auto encoder=[commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertices offset:0 atIndex:0];[encoder setVertexBuffer:tint offset:0 atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];[encoder endEncoding];
    if(preserve) {
        // A following pass must still see the source, not merely the first resolve.
        pass.colorAttachments[0].resolveTexture=again;pass.colorAttachments[0].loadAction=MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction=MTLStoreActionMultisampleResolve;
        [[commands renderCommandEncoderWithDescriptor:pass] endEncoding];
    }
    if(!finish(commands) || !pixels(target,loaded,half,blended) || (preserve && !pixels(again,loaded,half,blended)))return false;
    std::printf("PASS pixels load=%u preserve=%u half=%u blend=%u memoryless=%u\n",loaded,preserve,half,blended,memoryless);return true;
}
static bool ranges(id<MTLDevice> device) {
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:16 height:16 mipmapped:YES];
    td.textureType=MTLTextureType2DArray;td.arrayLength=3;td.storageMode=MTLStorageModeShared;
    id<MTLTexture> source=[device newTextureWithDescriptor:td],target=[device newTextureWithDescriptor:td];
    if(!source || !target)return false;
    for(NSUInteger slice=0;slice<3;slice++)for(NSUInteger level=0;level<td.mipmapLevelCount;level++) {
        NSUInteger width=std::max(NSUInteger(1),td.width>>level);std::vector<uint8_t> bytes(width*width*4,uint8_t(20+slice*30+level));
        [source replaceRegion:MTLRegionMake2D(0,0,width,width) mipmapLevel:level slice:slice withBytes:bytes.data() bytesPerRow:width*4 bytesPerImage:bytes.size()];
    }
    id<MTLCommandBuffer> commands=[[device newCommandQueue] commandBuffer];auto blit=[commands blitCommandEncoder];
    [blit copyFromTexture:source sourceSlice:1 sourceLevel:1 toTexture:target destinationSlice:0 destinationLevel:1 sliceCount:2 levelCount:3];
    [blit endEncoding];if(!finish(commands))return false;
    for(NSUInteger slice=0;slice<2;slice++)for(NSUInteger level=1;level<4;level++) {
        const NSUInteger width=std::max(NSUInteger(1),td.width>>level);std::vector<uint8_t> bytes(width*width*4);
        [target getBytes:bytes.data() bytesPerRow:width*4 bytesPerImage:bytes.size() fromRegion:MTLRegionMake2D(0,0,width,width) mipmapLevel:level slice:slice];
        for(uint8_t value:bytes)if(value!=20+(slice+1)*30+level)return false;
    }
    std::puts("PASS pixels ranged copy: 2 slices, 3 mip levels");return true;
}
static bool reflection(id<MTLDevice> device,bool apple) {
    NSError *error=nil;id<MTLLibrary> library;
    if(apple)library=[device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;constant int mode [[function_constant(0)]];kernel void fcfold(device float *out [[buffer(0)]],uint i [[thread_position_in_grid]]){out[i]=mode;}" options:nil error:&error];
    else {size_t length=0;const void *air=nmb_function_constant_air(&length);library=native_bridge_metal_library_with_air(air,length);}
    id<MTLFunction> generic=[library newFunctionWithName:@"fcfold"];
    NSDictionary *constants=generic.functionConstantsDictionary;MTLFunctionConstant *mode=constants[@"mode"];
    if(constants.count!=1 || ![mode.name isEqualToString:@"mode"] || mode.index!=0 || mode.type!=MTLDataTypeInt || !mode.required)return false;
    auto values=[MTLFunctionConstantValues new];int zero=0;[values setConstantValue:&zero type:mode.type atIndex:mode.index];
    if(![library newFunctionWithName:@"fcfold" constantValues:values error:&error])return false;
    std::puts("PASS reflection: original name, index, type, required flag and specialization");return true;
}
static bool run(id<MTLDevice> device,id<MTLLibrary> library,bool apple,unsigned &passed) {
    passed=0;
    for(bool half:{false,true})for(bool load:{false,true})for(bool preserve:{false,true}) {
        if(!resolve(device,library,load,preserve,half,false,false))return false;
        ++passed;
    }
    if(!resolve(device,library,false,false,false,true,false))return false;++passed;
    if(!resolve(device,library,false,false,false,false,true))return false;++passed;
    if(!ranges(device))return false;++passed;
    if(!reflection(device,apple))return false;++passed;
    return true;
}
} // namespace native_metal_pixels
