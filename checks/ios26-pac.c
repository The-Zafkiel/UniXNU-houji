/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DarwinPacFixups.h"

#define VA 0xfffffff004000000ULL
static unsigned char image[0x4000], loaded[0xc000], before[0xc000];
static unsigned calls;
static DpaU64 modifierSeen;
static void put32(unsigned at, DpaU32 v) {
  for (unsigned i = 0; i < 4; ++i) image[at+i] = (unsigned char)(v >> (8*i));
}
static void put64(unsigned at, DpaU64 v) { DpaWrite64(image + at, v); }
static void segment(unsigned at, const char *name, DpaU64 va, DpaU64 file, unsigned sections) {
  put32(at, 0x19); put32(at+4, 72+sections*80);
  memcpy(image+at+8, name, strlen(name));
  put64(at+24, va); put64(at+32, 4096); put64(at+40, file); put64(at+48, 4096);
  put32(at+64, sections);
}
static void section(unsigned at, const char *name, DpaU64 va, DpaU64 size, unsigned file) {
  memcpy(image+at, name, strlen(name)); memcpy(image+at+16, "__DATA", 6);
  put64(at+32, va); put64(at+40, size); put32(at+48, file);
}
static void setup(void) {
  memset(image, 0, sizeof(image)); memset(loaded, 0x59, sizeof(loaded)); calls=0; modifierSeen=0;
  put32(0, 0xfeedfacf); put32(4, 0x100000c); put32(8, 0x80000002); put32(12, 2);
  put32(16, 4); put32(20, 72+232+72+16);
  segment(32, "__TEXT", VA, 0, 0);
  segment(104, "__DATA", VA+0x4000, 0x1000, 2);
  section(176, "__pac_keys", VA+0x4100, 80, 0x1100);
  section(256, "__pac_config", VA+0x4180, 32, 0x1180);
  segment(336, "__LINKEDIT", VA+0x8000, 0x2000, 0);
  put32(408, 0x80000034); put32(412, 16); put32(416, 0x2000); put32(420, 68);
  put64(0x1180, DPA_CONFIG_MAGIC); put64(0x1188, DPA_TCR_MASK);
  put64(0x1190, 26ULL << 16); put64(0x1198, 80);
  put32(0x2004, 28); put32(0x201c, 3); put32(0x2024, 16);
  put32(0x202c, 24); image[0x2031]=0x10; image[0x2032]=7;
  put64(0x2034, 0x4000); image[0x2040]=1; image[0x2042]=0; image[0x2043]=2;
  put64(0x1200, (1ULL << 63) | (1ULL << 48) | (2ULL << 51) | 0x100);
  put64(0x1208, 0x4400);
  memcpy(loaded, image, 0x1000); memcpy(loaded+0x4000, image+0x1000, 0x1000);
  memcpy(loaded+0x8000, image+0x2000, 0x1000);
  memcpy(before, loaded, sizeof(loaded));
}
static int signPointer(void *cookie, DpaU64 target, DpaU64 modifier, DpaU32 key, DpaU64 *result) {
  assert(cookie == &calls); assert(target==VA+0x100); assert(key==0);
  ++calls; modifierSeen=modifier; *result=target ^ 0x5500000000000000ULL;
  return 0;
}
static int run(void) {
  DpaReport report;
  return DpaProcess(image, sizeof(image), VA, sizeof(loaded), loaded, signPointer, &calls, &report);
}
static void refused(void) {
  assert(run()!=DPA_OK); assert(calls==0); assert(memcmp(loaded,before,sizeof(loaded))==0);
}
static int inspectFile(const char *name) {
  FILE *f=fopen(name,"rb"); assert(f);
  assert(fseek(f,0,SEEK_END)==0); long length=ftell(f); assert(length>0);
  assert(fseek(f,0,SEEK_SET)==0);
  unsigned char *data=malloc((size_t)length); assert(data);
  assert(fread(data,1,(size_t)length,f)==(size_t)length); fclose(f);
  DpaU64 base=~0ULL;
  DpaU64 at=32;
  for(unsigned i=0;i<DpaRead32(data+16);++i) {
    assert(at+72<=(DpaU64)length);
    if(DpaRead32(data+at)==0x19 && DpaRead64(data+at+32) && DpaRead64(data+at+24)<base)
      base=DpaRead64(data+at+24);
    at+=DpaRead32(data+at+4);
  }
  base &= ~((1ULL<<25)-1);
  DpaReport report;
  int status=DpaProcess(data,(DpaU64)length,base,8ULL<<30,NULL,NULL,NULL,&report);
  if(status) fprintf(stderr,"kernel PAC inspection failed: %d\n",status);
  else printf("PASS: actual arm64e image: auth=%u rebase=%u segments=%u TCR=%llx keys-offset=%llx; no hardware signing\n",
      report.Authenticated,report.Rebased,report.Segments,report.TcrBits,report.KeysOffset);
  free(data); return status;
}
int main(int argc, char **argv) {
  if(argc==2) return inspectFile(argv[1]);
  setup(); DpaReport report;
  assert(DpaProcess(image,sizeof(image),VA,sizeof(loaded),NULL,NULL,NULL,&report)==DPA_OK);
  assert(report.Authenticated==1 && report.Rebased==1 && report.KeysOffset==0x4100);
  assert(run()==DPA_OK && calls==1 && modifierSeen==VA+0x4200);
  assert(DpaRead64(loaded+0x4208)==VA+0x4400);
  setup(); put64(0x1200,(1ULL<<63)|(1ULL<<48)|(0x2345ULL<<32)|(2ULL<<51)|0x100);
  assert(run()==DPA_OK && modifierSeen==0x2345fff004004200ULL);
  setup(); put64(0x1200,(1ULL<<63)|(0x1234ULL<<32)|(2ULL<<51)|0x100);
  assert(run()==DPA_OK && modifierSeen==0x1234);
  setup(); put64(0x1208,1ULL<<62); refused(); /* a late bind cannot leave earlier writes behind */
  setup(); put64(0x1208,0x6000); refused(); /* unmapped hole */
  setup(); put64(0x1200,(1ULL<<63)|(0x7ffULL<<51)|0x100); refused();
  setup(); image[0x2032]=1; refused(); /* unsupported pointer format */
  setup(); put32(0x2010,1); refused(); /* imported symbols */
  setup(); put64(0x1188,0); refused(); /* untrusted PAC geometry */
  setup(); put64(0x1190,0); refused();
  setup(); put32(420,40); refused(); /* truncated starts table */
  setup(); put32(20,0xfffffff0U); refused();
  setup(); put64(104+24,VA+0x800); refused(); /* overlapping segments */
  setup(); image[0x2042]=1; image[0x2043]=0x80; refused(); /* absent overflow starts */
  setup(); put32(420,28+16+28); put32(0x202c,28);
  image[0x2042]=1; image[0x2043]=0x80;
  image[0x2044]=0; image[0x2045]=2; image[0x2046]=8; image[0x2047]=0x82;
  put64(0x1200,(1ULL<<63)|0x100);
  assert(run()==DPA_OK && calls==1); /* two chains on the same page */
  setup(); memcpy(image+0x3000,image+0x2000,0x1000); memset(image+0x2000,0,0x1000);
  put64(104+32,0x2000); put64(104+48,0x2000); put64(336+40,0x3000); put32(416,0x3000);
  image[0x3042]=0xfc; image[0x3043]=0x0f;
  put64(0x1ffc,(1ULL<<63)|0x100);
  assert(run()==DPA_OK && calls==1); /* packed pointer straddling a 4K page */
  puts("PASS: actual PAC parser/rebaser, modifiers, multi-starts, complete validation before writes and malformed-image refusal; hardware PAC untested");
  return 0;
}
