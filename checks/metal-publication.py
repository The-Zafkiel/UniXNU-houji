#!/usr/bin/env python3
"""Exercise the actual houji discovery entry with the actual .clients list, without a .publish marker."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/metal/AdrenoMetal/NativeAdrenoMetalPlugin.mm').read_text()
a=text.index('static bool listedClient()');b=text.index('\n}\n',a)+3;listed=text[a:b]
a=text.index('@implementation NativeAdrenoMetalDevice\n');entry=text[a:]
assert 'adrenoUserTest(' not in entry and 'publish::requested()' not in entry
unit=r'''
#import <Foundation/Foundation.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
static const char *processName="backboardd",*allowlist;
static unsigned published,clients,preferences;
static id publishedDevice,clientDevice;
static const char *fixtureName(){return processName;}
#define getprogname fixtureName
static void fixturePreference(CFStringRef key,CFPropertyListRef value,CFStringRef){
 assert(value==kCFBooleanFalse);assert(CFEqual(key,CFSTR("CAEnableDeepFramebuffer"))||CFEqual(key,CFSTR("CAEnableFramebufferCompression")));++preferences;
}
#define CFPreferencesSetAppValue fixturePreference
namespace houji_turnip {static void configure(){}}
namespace publish {
static bool requestedPath(const char *name,char *path,size_t size){
 // The installed houji image contains the .clients file and no .publish file.
 if(strcmp(name,"NativeAdrenoMetal.clients"))return false;
 snprintf(path,size,"%s",allowlist);return true;
}
'''+listed+r'''
static id deviceForPort(unsigned port){assert(port==42);++published;return publishedDevice;}
static id clientDevice(unsigned port){assert(port==42);++clients;return ::clientDevice;}
}
@interface HoujiFixtureMTLDevice:NSObject
@end
@implementation HoujiFixtureMTLDevice
@end
@interface NativeAdrenoMetalDevice:HoujiFixtureMTLDevice
- (id)initWithAcceleratorPort:(unsigned)port;
@end
'''+entry+r'''
int main(int argc,char **argv){@autoreleasepool{
 assert(argc==2);allowlist=argv[1];publishedDevice=[NSObject new];clientDevice=[NSObject new];
 id value=[[NativeAdrenoMetalDevice alloc] initWithAcceleratorPort:0];assert(!value&&!published&&!clients);
 value=[[NativeAdrenoMetalDevice alloc] initWithAcceleratorPort:42];assert(value==publishedDevice&&published==1&&!clients&&preferences==2);
 processName="Preferences";value=[[NativeAdrenoMetalDevice alloc] initWithAcceleratorPort:42];assert(value==clientDevice&&clients==1&&published==1);
 processName="AegirPoster";value=[[NativeAdrenoMetalDevice alloc] initWithAcceleratorPort:42];assert(value==clientDevice&&clients==2);
 processName="chronod";value=[[NativeAdrenoMetalDevice alloc] initWithAcceleratorPort:42];assert(!value&&clients==2&&published==1);
 puts("PASS: actual discovery publishes backboardd and explicitly listed clients without .publish; no raw self-test route, unknown clients and zero ports refused; no GPU");
}}
'''
with tempfile.TemporaryDirectory(prefix='houji-publication-') as tmp:
    p=Path(tmp);(p/'check.mm').write_text(unit)
    subprocess.run(['xcrun','clang++','-std=c++17','-fobjc-arc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-framework','Foundation',str(p/'check.mm'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check'),str(ROOT/'graphics/metal/NativeAdrenoMetal.clients')],check=True)
