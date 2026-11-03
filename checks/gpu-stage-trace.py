#!/usr/bin/env python3
"""Exercise the selected upstream ring ABI, disabled mode, hook and wrap; no GPU."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
text=(ROOT/'graphics/runtime/overlay/crates/metal2vulkan-runtime/src/stage_trace.rs').read_text()
a=text.index('pub fn now_nanos()');b=text.index('\n/// Whether the host',a)
text=text[:a]+'''pub fn now_nanos()->u64 {static CLOCK:AtomicU64=AtomicU64::new(1);CLOCK.fetch_add(1,Ordering::Relaxed)}
'''+text[b:]
text+=r'''
static HOOKS:AtomicU64=AtomicU64::new(0);
extern "C" fn hook(kind:u32,_:u64,_:u64,_:u64){assert_eq!(kind,39);HOOKS.fetch_add(1,Ordering::Relaxed);}
fn main(){
 let on=std::env::var_os("SHENG_DIAG").is_some();
 assert_eq!(std::mem::size_of::<StageRecord>(),24);
 assert!(DIAGNOSTIC_HOOK.set(hook).is_ok());
 let total=RING*3;for i in 1..=total{mark(i,Stage::CommitCall,i as u32);}
 let mut out=[StageRecord::default();257];let (mut n,mut cursor)=read(0,&mut out);
 if !on {assert_eq!(n,0);assert_eq!(HOOKS.load(Ordering::Relaxed),0);return;}
 assert!(n>0 && out[0].sequence>RING as u32);
 let mut last=0;let mut count=0;
 loop {
  for r in &out[..n] {assert!(r.when>0&&r.sequence>last);assert_eq!(r.stage,1);assert_eq!(r.id as u16,r.arg);last=r.sequence;count+=1;}
  let (next,new)=read(cursor,&mut out);assert!(new>=cursor);cursor=new;
  if next==0{break;}n=next;
 }
 assert!(count>257);assert_eq!(HOOKS.load(Ordering::Relaxed),total as u64);
 println!("PASS: selected 24-byte stage ABI, diagnostic hook, wrap and ordered reads; no concurrent-lap or GPU guarantee");
}
'''
with tempfile.TemporaryDirectory(prefix='houji-current-stages-') as tmp:
    p=Path(tmp);(p/'ring.rs').write_text(text)
    subprocess.run(['rustc','--edition=2024','-O','-Adead_code',str(p/'ring.rs'),'-o',str(p/'ring')],check=True)
    for on in (False,True):
        env=dict(os.environ);env.pop('SHENG_DIAG',None)
        if on:env['SHENG_DIAG']='1'
        subprocess.run([str(p/'ring')],env=env,check=True,timeout=10)
