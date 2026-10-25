#!/usr/bin/env python3
"""Check immediate capture, stale-log refusal, and preserved retry evidence."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('watcher',root/'scripts/watch-native-recovery.py')
w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
for retry in (False,True):
 with tempfile.TemporaryDirectory() as folder:
  output=Path(folder)/'trial';calls=[];sleeps=[];captures=0
  def run(cmd,**kw):
   global captures
   calls.append(cmd)
   if cmd[0]==w.ADB:
    return subprocess.CompletedProcess(cmd,0,b'0\n1\n',b'')
   assert cmd[1].endswith('/capture-logfs.py')
   captures+=1;output.mkdir()
   (output/'UniXNU.log').write_bytes(b'HOUJI LOGFS r77: app text')
   metadata={'native_snapshot_integrity':{'valid':True},'xnu_snapshot_integrity':{'run_id':('0' if retry and captures==1 else 'a')*32},'files':[{'file':'UniXNU.log','stable':True},{'file':'XnuLog.bin','stable':True}]}
   (output/'capture.json').write_text(json.dumps(metadata))
   return subprocess.CompletedProcess(cmd,0,'captured','')
  argv=['watcher','--run','77','--run-id','a'*32,'--output',str(output)]
  with patch.object(sys,'argv',argv),patch.object(w.subprocess,'run',run),patch.object(w.time,'sleep',lambda n:sleeps.append(n)),contextlib.redirect_stdout(io.StringIO()):
   assert w.main()==0
  status=json.loads(output.with_name('trial-watch.json').read_text())
  assert status['state']=='complete' and status['fixed_grace_sleep'] is False
  assert captures==(2 if retry else 1)
  assert not sleeps  # First capture occurs immediately; no boot grace sleep.
  assert output.with_name('trial-partial-1').exists()==retry
  assert all('kill-server' not in x and 'reboot' not in x for x in calls)
print('PASS: live watcher captures immediately, rejects stale run identity, archives partial capture, never reboots or restarts ADB servers')
with tempfile.TemporaryDirectory() as folder:
 output=Path(folder)/'trial';captures=0
 def stable_invalid(cmd,**kw):
  global captures
  if cmd[0]==w.ADB:return subprocess.CompletedProcess(cmd,0,b'0\n1\n',b'')
  captures+=1;output.mkdir();(output/'UniXNU.log').write_bytes(b'HOUJI LOGFS r77: identical invalid bytes')
  (output/'capture.json').write_text(json.dumps({'native_snapshot_integrity':{'valid':False},'xnu_snapshot_integrity':{'run_id':'a'*32},'files':[{'file':'UniXNU.log','stable':True},{'file':'XnuLog.bin','stable':True}]}))
  return subprocess.CompletedProcess(cmd,0,'captured','')
 with patch.object(sys,'argv',['watcher','--run','77','--run-id','a'*32,'--output',str(output)]),patch.object(w.subprocess,'run',stable_invalid),contextlib.redirect_stdout(io.StringIO()):
  assert w.main()==3
 assert captures==3 and output.exists()
 assert json.loads(output.with_name('trial-watch.json').read_text())['state']=='invalid-stable-snapshot'
print('PASS: three identical stable invalid captures stop instead of looping; latest and previous evidence retained')
