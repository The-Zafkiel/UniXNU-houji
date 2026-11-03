#!/usr/bin/env python3
"""Pass actual frontend descriptor bytes through the actual Rust conversion."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1] / 'graphics/metal/AdrenoMetal'


def function(source, start):
    at = source.index(start)
    return source[at:source.index('\n}\n', at) + 3]


frontend = (ROOT / 'NativeBridgeMetalFrontend.mm').read_text()
bridge = (ROOT / 'bridge/NativeMetalObjects.rs').read_text()
cpp = r'''
#include <cassert>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
'''
cpp += '#include "' + str(ROOT / 'bridge/NativeMetalObjects.h') + '"\n'

cpp += r'''
constexpr uint64_t MTLTextureType2D=2,MTLTextureType2DMultisample=4,MTLTextureType2DArray=3,MTLTextureType2DMultisampleArray=8;
constexpr uint64_t MTLResourceStorageModeMask=0xf0,MTLResourceStorageModeMemoryless=0x30,MTLResourceStorageModePrivate=0x20;
constexpr unsigned MTLTextureUsageShaderRead=1;
struct MTLTextureSwizzleChannels {uint64_t red,green,blue,alpha;};
struct MTLTextureDescriptor {
    uint64_t textureType=2,pixelFormat=80,width=128,height=64,depth=1,mipmapLevelCount=1,sampleCount=1,arrayLength=1,usage=1,resourceOptions=32;
    bool allowGPUOptimizedContents=true;
    uint64_t rotation=0;
    bool framebuffer=false,drawable=false;
    MTLTextureSwizzleChannels swizzle={2,3,4,5};
};
static bool isPvrtc(uint64_t){return false;}
static uint64_t pvrtcCarrier(uint64_t f){return f;}
static uint64_t textureRotation(MTLTextureDescriptor *d){return d->rotation;}
static bool textureFlag(MTLTextureDescriptor *d,const char *name){return !strcmp(name,"framebufferOnly")?d->framebuffer:d->drawable;}
'''
# Objective-C property access is the only syntax changed for this host peer.
cpp += '#define HOUJI_METAL_SINGLE_SAMPLE 1\n' + function(frontend,'static bool singleSample(') + function(frontend,'static uint64_t packedSwizzle(')
cpp += function(frontend, 'static bool textureDescriptor(').replace('d.', 'd->').replace('told->','told.')
cpp += r'''
int main(){
    NmbTextureDescriptor wire{};assert(!textureDescriptor(nullptr,&wire));
    unsigned index=0;
    for(unsigned r=0;r<6;++r)for(unsigned g=0;g<6;++g)for(unsigned b=0;b<6;++b)for(unsigned a=0;a<6;++a){
        MTLTextureDescriptor descriptor;
        descriptor.rotation=index%5;descriptor.framebuffer=(index%2)!=0;descriptor.drawable=(index%3)!=0;++index;
        descriptor.swizzle={r,g,b,a};assert(textureDescriptor(&descriptor,&wire));
        assert(fwrite(&wire,sizeof wire,1,stdout)==1);
    }

    // The active descriptor keeps swizzle/flags and makes a memoryless MSAA
    // source copyable for Load / StoreAndResolve preservation.
    MTLTextureDescriptor ms;ms.textureType=4;ms.sampleCount=4;ms.resourceOptions=0x30;
    assert(textureDescriptor(&ms,&wire));
    if(singleSample()) {
        assert(wire.type==2 && wire.samples==1 && wire.options==0x20);
    } else {assert(wire.type==4 && wire.samples==4 && wire.options==0x30);}
}
'''
rust = r'''
use std::io::Read;
type TextureType=u32;
type PixelFormat=u32;
type TextureUsage=u32;
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
struct TextureSwizzle(u32);
impl TryFrom<u32> for TextureSwizzle {
    type Error=();
    fn try_from(v:u32)->Result<Self,Self::Error>{if v<6{Ok(Self(v))}else{Err(())}}
}
#[derive(Clone,Copy)]
struct TextureSwizzleChannels {red:TextureSwizzle,green:TextureSwizzle,blue:TextureSwizzle,alpha:TextureSwizzle}
impl Default for TextureSwizzleChannels {
    fn default()->Self{Self{red:TextureSwizzle(2),green:TextureSwizzle(3),blue:TextureSwizzle(4),alpha:TextureSwizzle(5)}}
}
#[derive(Default)]
struct TextureDescriptor {
    texture_type:u32,pixel_format:u32,width:u64,height:u64,depth:u64,mipmap_level_count:u64,sample_count:u64,array_length:u64,usage:u32,resource_options:u64,
    swizzle:TextureSwizzleChannels,
    rotation:u64,framebuffer_only:bool,is_drawable:bool,
}
fn options(value:u64)->Result<u64,String>{Ok(value)}
'''
rust += '#[repr(C)]\n' + function(bridge, 'pub struct NmbTextureDescriptor {')
for name in ["fn get<'a, T>(", 'fn raw<T:', 'fn swizzle_of(', 'fn texture_descriptor(']:
    rust += function(bridge, name)
rust += r'''
fn main(){
    let size=std::mem::size_of::<NmbTextureDescriptor>();assert_eq!(size,112);
    let mut bytes=Vec::new();std::io::stdin().read_to_end(&mut bytes).unwrap();
    assert_eq!(bytes.len(),1296*size);
    assert!(texture_descriptor(std::ptr::null()).is_err());
    for (i,chunk) in bytes.chunks_exact(size).enumerate(){
        // The C++ compiler emitted the active public C ABI, not a recreated layout.
        let mut wire=unsafe{std::ptr::read_unaligned(chunk.as_ptr().cast::<NmbTextureDescriptor>())};
        let out=texture_descriptor(&wire).unwrap();let n=i%1296;

        assert_eq!((out.rotation,out.framebuffer_only,out.is_drawable),((i%5) as u64,i%2!=0,i%3!=0));
        assert_eq!([out.swizzle.red.0,out.swizzle.green.0,out.swizzle.blue.0,out.swizzle.alpha.0],
            [(n/216) as u32,((n/36)%6) as u32,((n/6)%6) as u32,(n%6) as u32]);
        assert_eq!((out.texture_type,out.pixel_format,out.width,out.height,out.depth),(2,80,128,64,1));
        assert_eq!((out.mipmap_level_count,out.sample_count,out.array_length,out.usage,out.resource_options),(1,1,1,1,32));
        for at in 0..4 {
            let previous=wire.swizzle;
            wire.swizzle=(previous & !(255u64<<(at*8))) | (6u64<<(at*8));
            assert!(texture_descriptor(&wire).is_err());wire.swizzle=previous;
        }

    }
    println!("PASS: actual C++/Rust texture descriptor ABI, rotation/drawable flags, 1296 channel maps and invalid-value rejection; no GPU");
}
'''
with tempfile.TemporaryDirectory(prefix='houji-texture-properties-') as directory:
    tmp = Path(directory)
    for extension, unit, command in [
        ('cpp', cpp, ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined']),
        ('rs', rust, ['rustc', '--edition=2021', '-Dwarnings']),
    ]:
        source, binary = tmp / ('properties.' + extension), tmp / extension
        source.write_text(unit)
        subprocess.run([*command, str(source), '-o', str(binary)], check=True)
    records = subprocess.check_output([str(tmp / 'cpp')])
    subprocess.run([str(tmp / 'rs')], input=records, check=True)
