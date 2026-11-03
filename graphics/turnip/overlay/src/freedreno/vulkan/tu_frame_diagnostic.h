/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#ifdef __APPLE__
extern "C" bool tu_darwin_frame_diagnostic(uint32_t,const void *,uint32_t);
static inline void tu_frame_event(uint32_t event,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0,uint64_t f=0,uint64_t g=0,uint64_t h=0,uint64_t i=0,uint64_t j=0,uint64_t k=0,uint64_t l=0) {
   static const bool on=getenv("SHENG_GPU_TRACE")!=nullptr;
   if(!on)return;
   struct {uint64_t seq,us;uint32_t event,pid;uint64_t v[12];} rec={0,0,event,0,{a,b,c,d,e,f,g,h,i,j,k,l}};
   (void)tu_darwin_frame_diagnostic(0x7102,&rec,sizeof(rec));
}
#else
static inline void tu_frame_event(uint32_t,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0,uint64_t=0) {}
#endif
