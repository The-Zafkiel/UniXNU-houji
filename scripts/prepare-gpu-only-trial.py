#!/usr/bin/env python3
"""Prepare the next houji UEFI-only GPU run; preserve the unexecuted XNU asset.

Requires the previous captured native log, exact root rescue identity, all boot
and GPT backups, stable misc reserved bytes and the unchanged initial XNU marker.
Only removes the already-archived UniXNU.log and creates a fresh FAT backup.
The separate boot tool backs up/arms the no-wipe recovery request.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

P=Path('/Users/lonzzi/workspace/UniXNU-houji')
N=Path('/Users/lonzzi/Documents/Codex/UniXNU-Xiaomi14-2026-10-23/native-port')
U=Path('/Users/lonzzi/workspace/uefi-houji/Platforms/Xiaomi/houjiPkg/Application/HoujiStorageProbe')
C=N/'native-ios-candidate'
ADB='/Users/lonzzi/Library/Android/sdk/platform-tools/adb'
SERIAL='REDACTED_DEVICE_SERIAL'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=int,required=True)
    parser.add_argument('--adb-port',type=int,choices=(5037,5038,5047),required=True)
    a=parser.parse_args();v=a.run;prev=v-1
    if v<79:parser.error('This workflow follows the qualified r78 UEFI-only batch')
    old=f'r{prev}';new=f'r{v}'
    base=[ADB,'-P',str(a.adb_port),'-s',SERIAL]
    def root(command):
        ports=[int(base[2])]+[port for port in (5037,5038,5047) if port!=int(base[2])]
        for port in ports:
            command_base=[ADB,'-P',str(port),'-s',SERIAL]
            r=subprocess.run(command_base+['exec-out',command],capture_output=True,timeout=30)
            missing=r.returncode and any(s in r.stderr for s in (b'not found',b'device offline',b'no devices/emulators'))
            if missing:continue
            if r.returncode or r.stderr:raise RuntimeError((r.returncode,r.stderr.decode(errors='replace')))
            base[2]=str(port)
            return r.stdout
        raise RuntimeError('No existing ADB server has the verified rescue device')
    assert root('id -u; getprop ro.product.device; getprop ro.houji.native_rescue').splitlines()==[b'0',b'houji',b'1']
    spec=importlib.util.spec_from_file_location('preflight',P/'scripts/try-native-userland-boot.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    policy=json.loads((C/f'{old}-policy.json').read_text())
    assert policy==m.POLICY and policy['execution_profile']['XNU_boot'] is False
    for name,sha in m.BOOT.items():assert root('sha256sum /dev/block/by-name/'+name).split()==[sha.encode(),('/dev/block/by-name/'+name).encode()]
    for disk in json.loads((N/'partition-backup-20261024/manifest.json').read_text())['disks']:
        for suffix,skip in [('primary',0),('backup',disk['backup_start_lba'])]:
            expected=m.expected_gpt(disk,suffix)
            assert root(f'dd if={disk["device"]} bs=4096 skip={skip} count={len(expected)//4096} status=none')==expected
    misc=root('cat /dev/block/by-name/misc')
    assert len(misc)==4194304 and misc[864:]==(N/f'device-baseline/misc-before-native-ios-{old}.bin').read_bytes()[864:]
    (N/f'device-baseline/misc-after-native-ios-{old}.bin').write_bytes(misc)
    cap=N/f'evidence/logfs-recovery-{old}'
    captured=json.loads((cap/'capture.json').read_text())
    reader_spec=importlib.util.spec_from_file_location('capture',P/'scripts/capture-logfs.py')
    reader=importlib.util.module_from_spec(reader_spec);reader_spec.loader.exec_module(reader)
    native=(cap/'UniXNU.log').read_bytes()
    assert reader.verify_native_snapshot(native)['valid'] and f'HOUJI LOGFS {old}:'.encode() in native
    assert root('cat /dev/logfs/XnuLog.bin')==(cap/'XnuLog.bin').read_bytes()
    assert hashlib.sha256((cap/'XnuLog.bin').read_bytes()).hexdigest()==policy['log_initial_sha256']
    helper=N/'native-log-candidate/logfs-direct-read'
    assert hashlib.sha256(helper.read_bytes()).hexdigest()=='716048584c1c6e85e64335e9528704f0e80c46a893f0e3e5d723bb3bd778b6a0'
    subprocess.run(base+['push',str(helper),'/tmp/houji-logfs-read'],check=True,timeout=30)
    root('chmod 755 /tmp/houji-logfs-read')
    assert root('sha256sum /tmp/houji-logfs-read').split()[0].decode()==hashlib.sha256(helper.read_bytes()).hexdigest()
    existence=root('if [ -e /dev/logfs/UniXNU.log ]; then echo PRESENT; else echo ABSENT; fi').strip()
    if existence==b'PRESENT':
        assert root('cat /dev/logfs/UniXNU.log')==native
        root('set -e; mount -o remount,rw /dev/logfs; rm /dev/logfs/UniXNU.log; sync; mount -o remount,ro /dev/logfs')
    else:assert existence==b'ABSENT'  # Safe resume after prior archive/deletion.
    backup=None
    for _ in range(4):
        first=root('/tmp/houji-logfs-read');second=root('/tmp/houji-logfs-read')
        if len(first)==8388608 and first==second:backup=first;break
    if backup is None:raise RuntimeError('No stable complete 8MiB read; archived logs are preserved; do not boot')
    dest=N/f'logfs-backup-before-native-ios-{new}';dest.mkdir()
    (dest/'logfs.bin').write_bytes(backup)
    for name in ('HoujiStorageProbe.c','LogFsTest.h'):
        f=U/name;f.write_text(f.read_text().replace(old,new))
    f=P/'scripts/capture-logfs.py';s=f.read_text();needle=f'{prev})))';assert needle in s
    f.write_text(s.replace(needle,f'{prev}, {v})))'))
    for ext in ('macho','unstripped'):shutil.copy2(C/f'kernel-{old}.{ext}',C/f'kernel-{new}.{ext}')
    shutil.copy2(C/f'{old}-psci-assembly-check.txt',C/f'{new}-psci-assembly-check.txt')
    shutil.copytree(C/f'{old}-allocation',C/f'{new}-allocation')
    policy['execution_profile']['kernel_asset_reused_from']=f'{old}; same bytes and initial XNU marker; native UEFI label/image/source distinguish this run'
    (C/f'{new}-policy.json').write_text(json.dumps(policy,indent=2)+'\n')
    before=hashlib.sha256((N/f'logfs-backup-before-native-ios-{old}/logfs.bin').read_bytes()).hexdigest()
    after=hashlib.sha256(backup).hexdigest()
    stage=(N/f'storage-candidate/make-native-ios-{old}.py').read_text().replace(old,new).replace(before,after)
    (N/f'storage-candidate/make-native-ios-{new}.py').write_text(stage)
    f=P/'scripts/try-native-userland-boot.py'
    f.write_text('\n'.join('POLICY='+repr(policy) if line.startswith('POLICY=') else line.replace(old,new) for line in f.read_text().splitlines())+'\n')
    result_path=N/f'evidence/native-ios-{old}-result.json'
    result=json.loads(result_path.read_text()) if result_path.exists() else {}
    result.update(boot_partitions_unchanged=True,all_12_gpt_ranges_unchanged=True,misc_reserved_unchanged=True,
                  expected_XNU_not_booted=True,native_snapshot_integrity=captured['native_snapshot_integrity'])
    result_path.write_text(json.dumps(result,indent=2)+'\n')
    print(f'{new}: protected state/live logs verified, fresh FAT backup complete; XNU bytes/marker preserved, no kernel build; ADB port={base[2]}')


if __name__=='__main__':main()
