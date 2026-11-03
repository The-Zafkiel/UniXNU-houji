#!/usr/bin/env python3
"""Run the actual command-buffer initializer and dictionary property on the host."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/metal/AdrenoMetal/NativeBridgeMetalFrontend.mm').read_text()
interface=text.split('@interface NBCommandBuffer : NBObject {',1)[1].split('@end',1)[0]
properties='\n'.join(line for line in interface.splitlines() if line.startswith('@property') and not any(word in line for word in ['NmbCommandBuffer','NBQueue']))
# The new frontend stores the dictionary lazily as an associated object.
start=text.index('- (id)userDictionary {'); end=text.index('\n}',start)+2
method=text[start:end]
unit='''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cassert>
#include <objc/runtime.h>
#include "NativeMetalProtocols.h"
static char kUserDictionaryKey;
@interface NBCommandBuffer:NSObject {uint64_t createdUs;}
@property(nonatomic,readonly) NSMutableDictionary *userDictionary;\n@end
@implementation NBCommandBuffer
'''+method+'''\n@end
int main(){@autoreleasepool {
 NBCommandBuffer *a=[NBCommandBuffer new],*b=[NBCommandBuffer new];
 // MPS checks conformance before consulting the dictionary. Exercise that
 // gate using real host Metal protocols and the production registration code.
 assert(![a conformsToProtocol:@protocol(MTLCommandBuffer)]);
 for(const auto &binding:native_metal_protocols::bindings) {
  assert(binding.protocol && protocol_getName(binding.protocol));
 }
 assert(sizeof(native_metal_protocols::bindings)/sizeof(native_metal_protocols::bindings[0])==19);
 assert(native_metal_protocols::install()==1);
 assert([a conformsToProtocol:@protocol(MTLCommandBuffer)]);
 assert(![a conformsToProtocol:@protocol(MTLResource)]);
 assert(![[NSObject new] conformsToProtocol:@protocol(MTLCommandBuffer)]);
 assert(native_metal_protocols::install()==1);
 assert(a.userDictionary && b.userDictionary && a.userDictionary!=b.userDictionary);
 NSMutableDictionary *saved=a.userDictionary;
 __weak NSObject *value;
 @autoreleasepool {NSObject *object=[NSObject new];value=object;a.userDictionary[@"owner"]=object;}
 assert(value && saved==a.userDictionary && !b.userDictionary[@"owner"]);
 a.userDictionary[@"counter"]=@42;assert([saved[@"counter"] isEqual:@42]);
 [a.userDictionary removeObjectForKey:@"owner"];assert(!value);
 assert(![a respondsToSelector:NSSelectorFromString(@"setUserDictionary:")]);
 }puts("PASS: command-buffer protocol gate and actual initializer retain an independent mutable SPI dictionary, stable identity, retained/released values and readonly property; no GPU");}
'''
with tempfile.TemporaryDirectory(prefix='houji-command-dictionary-') as directory:
    source,binary=Path(directory)/'check.mm',Path(directory)/'check';source.write_text(unit)
    subprocess.run(['xcrun','clang++','-std=c++17','-fobjc-arc','-fblocks','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-framework','Foundation','-framework','Metal','-I'+str(ROOT/'graphics/metal/AdrenoMetal'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
