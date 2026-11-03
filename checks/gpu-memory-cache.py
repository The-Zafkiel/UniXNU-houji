#!/usr/bin/env python3
"""Execute the current runtime's spare reuse/expiry/pressure policy with fake handles."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/runtime/overlay/crates/metal2vulkan-runtime/src/memory.rs').read_text()
def method(name):
    start=text.index(('    pub(super) fn ' if name=='release_spare' else '    fn ')+name+'(')
    return text[start:text.index('\n    }\n',start)+7]
constants='\n'.join(re.findall(r'^(?:pub\(super\) )?const (?:SPARE_FOR|SPARE_BYTES):.*;',text,re.M))
unit=r'''
#![allow(dead_code)]
use std::time::{Instant,Duration};
mod ash {pub struct Device;}
mod stage_trace {pub enum Stage {MemoryCached} pub fn mark(_:usize,_:Stage,_:u32){}}
fn trace_memory(_:(Holds,u32),_:u64,_:u64,_:u32)->usize {0}
fn trace_handle(_:usize,_:u64) {}
#[derive(Clone,Copy,PartialEq,Eq)] enum Holds {Buffers,Images}
#[derive(Clone,Copy)] enum FreeCause {PooledEmptied,KeptStale,KeptForRoom}
struct Block {memory:u64,size:u64}
struct Spare {pool:(Holds,u32),block:Block,since:Instant}
// Only the device allocation and pool ownership are fake; all reuse decisions
// below are extracted verbatim from the active runtime.
struct Pool {empty:Vec<Block>}
impl Pool {fn take_kept(&mut self)->Vec<Block>{std::mem::take(&mut self.empty)}}
struct Memory {spare:Vec<Spare>,pools:Vec<Pool>,freed:Vec<u64>}
impl Memory {
 unsafe fn let_go(&mut self,_:&ash::Device,b:Block,_:FreeCause,after:Option<Box<dyn Send>>)->Option<Box<dyn Send>> {
  assert!(!self.freed.contains(&b.memory));self.freed.push(b.memory);after
 }
'''+ '\n'.join(method(n) for n in ['keep_spare','age_spare','take_spare','release_spare'])+r'''
}
'''+constants+r'''
const M:u64=1<<20;
fn main(){
 let d=ash::Device;let mut m=Memory{spare:vec![],pools:vec![Pool{empty:vec![]}],freed:vec![]};
 let now=Instant::now();
 for (id,bytes) in [(1,100*M),(2,100*M),(3,100*M)] {
  m.keep_spare(&d,(Holds::Images,0),Block{memory:id,size:bytes},now+Duration::from_millis(id));
 }
 assert_eq!(m.freed,vec![1]);assert_eq!(m.spare.len(),2); // global 256MiB budget evicts oldest
 assert!(m.take_spare(&d,(Holds::Images,1),100*M).is_none()); // memory type isolation
 assert!(m.take_spare(&d,(Holds::Buffers,0),100*M).is_none()); // resource pool isolation
 assert!(m.take_spare(&d,(Holds::Images,0),74*M).is_none()); // >25% unused backing refused
 let b=m.take_spare(&d,(Holds::Images,0),80*M).unwrap();assert_eq!(b.memory,2);
 assert!(m.take_spare(&d,(Holds::Images,0),101*M).is_none());
 m.keep_spare(&d,(Holds::Images,0),Block{memory:4,size:SPARE_BYTES+1},now);
 assert!(m.freed.contains(&4)); // oversized block cannot enter retained budget
 m.spare[0].since=now-SPARE_FOR;m.age_spare(&d,now);assert!(m.spare.is_empty());assert!(m.freed.contains(&3));
 m.keep_spare(&d,(Holds::Images,0),Block{memory:5,size:M},now);
 m.pools[0].empty.push(Block{memory:6,size:M});
 assert_eq!(m.release_spare(&d),2);assert_eq!(m.release_spare(&d),0);
 assert!(m.freed.contains(&5)&&m.freed.contains(&6));
 println!("PASS: active spare budget, oldest eviction, size/type isolation, expiry and pressure release; fake handles, no GPU");
}
'''
with tempfile.TemporaryDirectory(prefix='houji-current-memory-') as tmp:
    p=Path(tmp);(p/'check.rs').write_text(unit.replace('pub(super) ',''))
    subprocess.run(['rustc','--edition=2024',str(p/'check.rs'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True,timeout=10)
