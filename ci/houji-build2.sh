#!/bin/bash
# 小米14 iOS 移植 —— Mac 侧重编内核（离线版：不联 GitHub，源码从 Windows 传过来）
# 前置：~/Desktop/houji-kernel-src.tar.gz（18 MB，含组装好的内核树 + tools）
# 用法：bash ~/Desktop/houji-build2.sh
set -uo pipefail
LOG="$HOME/houji-build.log"
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1
say(){ printf '\n=== %s\n' "$*"; }
warn(){ printf '[warn] %s\n' "$*"; }
die(){ printf '\n[FAIL] %s\n' "$*"; printf '\n--- 编译/链接错误 ---\n'; LC_ALL=C grep -n -E ": error:|: fatal error:|undefined symbol|^ld: |No such file or directory" "$LOG" | grep -v ": warning:" | tail -30; printf '\n--- 尾部 10 行 ---\n'; tail -10 "$LOG"; exit 1; }

say "0/6 环境"
uname -m; sw_vers -productVersion 2>/dev/null
xcode-select -p || die "Xcode 未选中"
python3 -V

say "1/6 KDK"
KDK=${HOUJI_KDK:-$(ls -d /Library/Developer/KDKs/*.kdk 2>/dev/null | tail -1)}
[ -n "$KDK" ] || die "没装 KDK"
[ -d "$KDK/System/Library/KernelSupport" ] || die "KDK 不完整：$KDK"
export HOUJI_KDK="$KDK"; echo "KDK=$HOUJI_KDK"

say "2/6 CTF 工具"
FR="$HOME/darwin-build-tools"; mkdir -p "$FR/usr/local/bin"
for t in ctfconvert ctfmerge ctfdump; do
  if [ ! -x "$FR/usr/local/bin/$t" ]; then
    printf '#!/bin/sh\nexit 0\n' > "$FR/usr/local/bin/$t"
    chmod +x "$FR/usr/local/bin/$t"
    warn "$t 用空壳顶替（只影响调试信息）"
  fi
done
export HOUJI_FAKEROOT="$FR"

say "2b/6 availability.pl（Xcode SDK 里带，构建脚本要往 fakeroot 里找）"
mkdir -p "$FR/usr/local/libexec"
AV="$(find /Library/Developer /Applications/Xcode.app /usr/local /usr/libexec -name availability.pl 2>/dev/null | head -1)"
if [ -n "$AV" ]; then
  cp -f "$AV" "$FR/usr/local/libexec/availability.pl"
  echo "availability.pl <- $AV"
else
  warn "系统里没有 availability.pl，用等价桩代替（它只负责打印版本号，用来生成 __DARWIN_ALIAS_STARTING_* 宏）"
  cat > "$FR/usr/local/libexec/availability.pl" <<'EOS'
#!/bin/sh
case "$1" in
--ios)
  for M in $(seq 2 26); do for m in 0 1 2 3 4 5; do echo "$M.$m"; done; done ;;
--macosx)
  for m in $(seq 0 16); do echo "10.$m"; done
  for M in $(seq 11 26); do for m in 0 1 2 3 4 5; do echo "$M.$m"; done; done ;;
esac
EOS
fi
chmod 755 "$FR/usr/local/libexec/availability.pl"
ls -l "$FR/usr/local/libexec/availability.pl"

say "2c/6 mig / migcom"
MIGCOM_BIN=""
for c in "$(xcrun -sdk macosx -find migcom 2>/dev/null || true)" \
         "$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/libexec/migcom" \
         "/Library/Developer/CommandLineTools/usr/libexec/migcom"; do
  if [ -n "$c" ] && [ -x "$c" ]; then MIGCOM_BIN="$c"; break; fi
done
[ -n "$MIGCOM_BIN" ] || die "找不到 migcom（XNU 的 mig 包装脚本靠它，缺了 mach 头全生成不出来）"
export MIGCOM="$MIGCOM_BIN"

MIG_BIN="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin/mig"
if [ ! -x "$MIG_BIN" ]; then
  MIG_BIN="$(command -v mig || true)"
fi
[ -n "$MIG_BIN" ] && [ -x "$MIG_BIN" ] || die "找不到 mig 驱动脚本"
export MIG="$MIG_BIN"
echo "MIG=$MIG"
echo "MIGCOM=$MIGCOM"

say "2d/6 镜像 SDK 的 usr/local 到 fakeroot（链接要 libfirehose_kernel.a）"
SDK_NOW="${SDK_NOW:-$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)}"
for sub in usr/local/include usr/local/lib; do
  if [ -n "$SDK_NOW" ] && [ -d "$SDK_NOW/$sub" ]; then
    mkdir -p "$FR/$sub"
    cp -R "$SDK_NOW/$sub/." "$FR/$sub/" 2>/dev/null || warn "拷贝 $sub 时有文件跳过"
    echo "  镜像 $SDK_NOW/$sub"
  fi
done
if [ -d "$FR/usr/local/lib/kernel" ]; then
  echo "  fakeroot lib/kernel:"; ls "$FR/usr/local/lib/kernel" | head -8
fi

say "2e/6 提供 libfirehose_kernel.a（SDK 27 已移除）"
mkdir -p "$FR/usr/local/lib/kernel"
if [ ! -f "$FR/usr/local/lib/kernel/libfirehose_kernel.a" ]; then
  FWLIB=""
  for d in "$KDK" "$SDK_NOW"; do
    [ -n "$d" ] && [ -e "$d" ] || continue
    c="$(find "$d" -name 'libfirehose_kernel.a' 2>/dev/null | head -1)"
    if [ -n "$c" ]; then FWLIB="$c"; break; fi
  done
  if [ -n "$FWLIB" ]; then
    cp -f "$FWLIB" "$FR/usr/local/lib/kernel/"
    echo "  取自 $FWLIB"
  else
    printf 'int __houji_dummy_firehose_stub(void) { return 0; }\n' > "$FR/dummy.c"
    clang -arch arm64 -c "$FR/dummy.c" -o "$FR/dummy.o"
    rm -f "$FR/usr/local/lib/kernel/libfirehose_kernel.a"
    ar -rcs "$FR/usr/local/lib/kernel/libfirehose_kernel.a" "$FR/dummy.o"
    ranlib "$FR/usr/local/lib/kernel/libfirehose_kernel.a"
    echo "  SDK/KDK 都没有，放占位归档（真符号由内核树里的 firehose_stub.c 提供）"
  fi
fi
ls -l "$FR/usr/local/lib/kernel/libfirehose_kernel.a"

say "3/6 解包源码"
T="$HOME/Desktop/houji-kernel-src.tar.gz"
[ -f "$T" ] || die "桌面找不到 houji-kernel-src.tar.gz"
W="$HOME/houji"; mkdir -p "$W"
tar xzf "$T" -C "$W" || die "解包失败"
[ -f "$W/.deps/kernel/config/MASTER.arm64.sweet" ] || die "内核树不完整（缺 MASTER.arm64.sweet）"
[ -f "$W/tools/build-kernel.sh" ] || die "缺 tools/build-kernel.sh"
echo "源码就绪：$(du -sh "$W/.deps/kernel" | awk '{print $1}')"

say "3b/6 去掉 CRLF（Windows 打包带过来的，否则 shebang 会变成 /bin/sh -CR）"
python3 - "$W/.deps/kernel" "$W/tools" <<'PYEOF'
import sys, pathlib
crlf = bytes([13, 10])
lf = bytes([10])
n = 0
for arg in sys.argv[1:]:
    root = pathlib.Path(arg)
    if not root.exists():
        continue
    for p in root.rglob('*'):
        if not p.is_file():
            continue
        try:
            b = p.read_bytes()
        except OSError:
            continue
        if bytes([0]) in b[:4096]:
            continue
        if crlf in b:
            p.write_bytes(b.replace(crlf, lf))
            n += 1
print('CRLF -> LF: %d files' % n)
PYEOF

HOUJI_XNU_EXTERNAL_OS="$W/.deps/kernel/EXTERNAL_HEADERS/os"

say "3c/6 生成 GPU 固件头（OEM 固件拿不到，全 0 占位，大小与上游 pin 一致）"
python3 - "$W/.deps/kernel/iokit/Kernel/houji-ufs-root" <<'PYEOF'
import sys, pathlib
d = pathlib.Path(sys.argv[1])
def write(name, arrays):
    p = d / name
    if p.exists():
        print('exists:', p.name)
        return
    body = '/* placeholder: OEM firmware blobs unavailable, zero-filled */\n#pragma once\n'
    for sym, n in arrays:
        body += 'static const unsigned char %s[%d]={0};\n' % (sym, n)
    p.write_text(body)
    print('wrote:', p.name)
write('HoujiGmuFirmwareImage.h', [('gHoujiGmuFirmwareImage', 78588)])
write('HoujiCpFirmwareImages.h', [('gHoujiSqeFirmwareImage', 85300), ('gHoujiAqeFirmwareImage', 33176)])
PYEOF

say "4b/6 检查私有头 os/firehose_buffer_private.h（已随源码包提供）"
[ -f "$HOUJI_XNU_EXTERNAL_OS/firehose_buffer_private.h" ] || die "源码包里缺 EXTERNAL_HEADERS/os/firehose_buffer_private.h"
ls -l "$HOUJI_XNU_EXTERNAL_OS/firehose_buffer_private.h"

say "4/6 打补丁（3 处，锚点唯一性断言）"
python3 - "$W/.deps/kernel" <<'PY'
import sys, pathlib
root = pathlib.Path(sys.argv[1])

def patch(rel, pairs):
    p = root / rel
    s = p.read_text()
    for anchor, repl, tag in pairs:
        n = s.count(anchor)
        assert n == 1, '%s: anchor %d times (want 1): %s' % (rel, n, tag)
        s = s.replace(anchor, repl)
        print('  [ok] %s -> %s' % (rel, tag))
    p.write_text(s)

fb = 'iokit/Kernel/arm/SweetFramebuffer.cpp'
decl_old = "\tbool present(const HoujiDisplayFrame &frame);\n};\n"
decl_new = ("\tbool present(const HoujiDisplayFrame &frame);\n"
            "\tIOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,\n"
            "\t    OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;\n"
            "};\n")
impl = r'''
/* iOS 只有经过 newUserClient 才能拿到 IOMobileFramebuffer 的 user client；
 * 原文件只定义了 user client 类却没有这个 override，所有 open 都返回
 * unsupported，backboardd 永远不提交帧（presents=0）。 */
IOReturn
AppleCLCD::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    __unused OSDictionary *properties, IOUserClient **handler)
{
	IOMobileFramebufferUserClient *client;

	__atomic_add_fetch(&houji_fb_uc_try, 1ull, __ATOMIC_RELAXED);
	client = OSTypeAlloc(IOMobileFramebufferUserClient);
	if (client == NULL) {
		__atomic_add_fetch(&houji_fb_uc_alloc, 1ull, __ATOMIC_RELAXED);
		return kIOReturnNoMemory;
	}
	if (!client->initWithTask(owningTask, securityID, type)) {
		__atomic_add_fetch(&houji_fb_uc_init, 1ull, __ATOMIC_RELAXED);
		client->release();
		return kIOReturnNoResources;
	}
	if (!client->attach(this)) {
		__atomic_add_fetch(&houji_fb_uc_attach, 1ull, __ATOMIC_RELAXED);
		client->release();
		return kIOReturnNoResources;
	}
	if (!client->start(this)) {
		__atomic_add_fetch(&houji_fb_uc_start, 1ull, __ATOMIC_RELAXED);
		client->release();
		return kIOReturnNoResources;
	}
	__atomic_add_fetch(&houji_fb_uc_ok, 1ull, __ATOMIC_RELAXED);
	IOLog("HOUJI FB: newUserClient type=%u ok\n", type);
	*handler = client;
	return kIOReturnSuccess;
}
'''
tail_old = "\n#endif /* SWEET */"
tail_new = impl + "\n#endif /* SWEET */"
patch(fb, [(decl_old, decl_new, 'decl newUserClient'),
           (tail_old, tail_new, 'impl newUserClient')])

# 让周期报告直接说出 user client 到底有没有被打开过
uc_old = 'static uint64_t sweet_fb_swap_count,houji_fb_present_count;'
uc_new = uc_old + ('\nstatic uint64_t houji_fb_uc_try,houji_fb_uc_ok;'
                   '\nstatic uint64_t houji_fb_uc_alloc,houji_fb_uc_init,houji_fb_uc_attach,houji_fb_uc_start;'
                   '\nstatic uint64_t houji_fb_start_stage;'
                   '\n' + r'''
/* ponytail: 冻结时把开机以来与显示相关的控制台行重放到屏幕上，避免再靠
 * 一轮轮刷机去猜用户态报错（logd 每秒崩溃把 83 行环形缓冲全刷掉）。
 * 只用 houji_debug_copy 这个既有环，自己写子串匹配，不引入头文件依赖。 */
extern "C" unsigned houji_debug_copy(uint64_t *pos, char *out, unsigned max);
extern "C" uint64_t houji_debug_total(void);

static int
houji_fb_has(const char *s, const char *k)
{
	while (*s != 0) {
		const char *a = s, *b = k;
		while (*b != 0 && *a == *b) {
			a++;
			b++;
		}
		if (*b == 0) {
			return 1;
		}
		s++;
	}
	return 0;
}

/* ponytail: 采样器每 5 秒打的 HOUJI PROC/USERLAND 行会把槽位全占满
 * （上一版 12 条全是 PROC，真答案一条没留下），回放前先剔掉自己家的噪声。 */
static int
houji_fb_noise(const char *s)
{
        static const char *const keys[] = {
                "HOUJI PROC:", "HOUJI USERLAND", "HOUJI VM:", "HOUJI DISPLAY",
                "HOUJI SURFACE", "HOUJI FRAME", "HOUJI ID:", "HOUJI NATIVE UFS",
                "DKT "
        };
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                if (houji_fb_has(s, keys[i])) {
                        return 1;
                }
        }
        return 0;
}

static int
houji_fb_dump_interesting(const char *s)
{
	static const char *const keys[] = {
		"HOUJI FB:", "HOUJI FB DIAG", "HOUJI OPEN", "AppleCLCD", "ramebuffer", "Framebuffer",
		"IOMobileFramebuffer", "backboardd", "SpringBoard", "IOSurface",
		"IOFB", "not in trust cache"
	};
	for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (houji_fb_has(s, keys[i])) {
			return 1;
		}
	}
	return 0;
}

/* ponytail: 只留最后 16 条——开机早期的噪声已经证明过了，现在要的是
 * backboardd 停手前那几行。全量扫一遍、滚动保留尾部、最后正序打印。 */
static void
houji_fb_dump_log(void)
{
	char buf[1024];
	char line[200];
	int ln = 0;
	uint64_t pos = 0;
	uint64_t total = houji_debug_total();
	unsigned n;
	static char ring[16][160];
	unsigned head = 0, count = 0;

	for (;;) {
		n = houji_debug_copy(&pos, buf, sizeof(buf));
		if (n == 0) {
			break;
		}
		for (unsigned i = 0; i < n; i++) {
			char c = buf[i];
			if (c == '\n' || c == '\r') {
				line[ln] = 0;
				if (ln > 12 && !houji_fb_noise(line) && houji_fb_dump_interesting(line)) {
					unsigned k = 0;
					while (line[k] != 0 && k < sizeof(ring[0]) - 1) {
						ring[head][k] = line[k];
						k++;
					}
					ring[head][k] = 0;
					head = (head + 1) % 16;
					if (count < 16) {
						count++;
					}
				}
				ln = 0;
			} else if (ln < (int)(sizeof(line) - 1)) {
				line[ln++] = c;
			}
		}
	}
	IOLog("DKT: end shown=%u of %llu bytes\n", count, (unsigned long long)total);
	for (unsigned i = 0; i < count; i++) {
		unsigned idx = (head + 16 - count + i) % 16;
		IOLog("DKT %s\n", ring[idx]);
	}
}
''')
cfg_old = '\tIOLog("HOUJI DISPLAY CONFIG: arg-found=%u'
_cfg_block = r'''
        {
        /* ponytail: 只读原子计数，不碰 IOKit —— 上一版把 svc/dt 两个查询塞进
         * 参数求值，回放打完就再没输出（查询在采样线程里挂住）。
         * 40 拍先打一张快照，90 拍再打一张并定格；两张都能从回放里读回来。 */
        static int diagPolls;
        int diagPoll = ++diagPolls;
        if ((diagPoll == 40 || diagPoll == 90) &&
            __atomic_load_n(&houji_fb_present_count, __ATOMIC_RELAXED) == 0) {
                IOLog("HOUJI FB DIAG: poll=%d try=%llu alloc=%llu init=%llu attach=%llu start=%llu ok=%llu stage=%llu swaps=%llu presents=%llu\n",
                    diagPoll,
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_try,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_alloc,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_init,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_attach,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_start,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_uc_ok,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_start_stage,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&sweet_fb_swap_count,__ATOMIC_RELAXED),
                    (unsigned long long)__atomic_load_n(&houji_fb_present_count,__ATOMIC_RELAXED));
                if (diagPoll >= 90) {
                        houji_fb_dump_log();
                        /* 用户态 /dev/console 走 console_write() 的环形缓冲，不经
                         * _cnputs，静音点必须同时盖住两处才能真的停住屏。 */
                        houji_console_muted = 1;
                        for (;;) IOSleep(1000);
                }
        }
        }
'''
cfg_new = _cfg_block + cfg_old
patch(fb, [(uc_old, uc_new, 'user client counters'),
           (cfg_old, cfg_new, 'user client report')])


# —— 显示模式必须按 CoreAnimation 真正解析的形状发布 ——
# 对照参考移植 xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp 的 publishModes()：
# timing ID=1、两个轴都带 Total/SyncRate、timing 里挂 ColorModes、服务上另发
# ColorElements（作者原话：没有模式它什么都不画）。我们原来是 ID=0、只有 Active、
# 没有 ColorModes/ColorElements —— CA 认为无可用模式就不会 open framebuffer，是 try=0 最合理的解释。
mode_alloc_old = ("\tOSArray *timings=OSArray::withCapacity(1);\n"
                  "\tif(!horizontal || !vertical || !timing || !refresh || !timings){")
mode_alloc_new = ("\tOSArray *timings=OSArray::withCapacity(1);\n"
                  "\tOSArray *colors=OSArray::withCapacity(1);\n"
                  "\tOSDictionary *color=OSDictionary::withCapacity(8);\n"
                  "\tif(!horizontal || !vertical || !timing || !refresh || !timings || !colors || !color){")
mode_old = '''\tconst NumberProperty modeNumbers[]={
\t\t{horizontal,"Active",HOUJI_LOGICAL_W},{vertical,"Active",HOUJI_LOGICAL_H},
\t\t{vertical,"SyncRate",(uint64_t)notifyHz<<16},{vertical,"PreciseSyncRate",(uint64_t)notifyHz<<16},
\t\t{timing,"ID",0},{timing,"Score",100},
\t\t{refresh,"displayMinRefreshInterval",(1ull<<32)/notifyHz},
\t\t{refresh,"displayMaxRefreshInterval",(1ull<<32)/notifyHz},
\t\t{refresh,"displayMinRefreshIntervalMachTime",intervalMach},
\t\t{refresh,"displayMaxRefreshIntervalMachTime",intervalMach},
\t\t{refresh,"displayRefreshStep",0},{refresh,"displayRefreshStepMachTime",0},
\t};
\tbool modeOK=true;
\tfor(const auto &v:modeNumbers){OSNumber*n=OSNumber::withNumber(v.value,v.dictionary==refresh?64:32);if(!n){modeOK=false;break;}modeOK=v.dictionary->setObject(v.key,n)&&modeOK;n->release();}
\tmodeOK=timing->setObject("IsVirtual",kOSBooleanFalse)&&modeOK;
\tmodeOK=timing->setObject("HorizontalAttributes",horizontal)&&modeOK;
\tmodeOK=timing->setObject("VerticalAttributes",vertical)&&modeOK;
\tmodeOK=timings->setObject(timing)&&modeOK;
\tif(modeOK){modeOK=setProperty("TimingElements",timings)&&setProperty("IOMFBDisplayRefresh",refresh);}'''
mode_new = '''\tauto num=[](OSDictionary *d,const char *k,uint64_t v,unsigned bits)->bool{
\t\tOSNumber *n=OSNumber::withNumber(v,bits);
\t\tif(n==NULL)return false;
\t\tbool ok=d->setObject(k,n)!=false;
\t\tn->release();
\t\treturn ok;
\t};
\tconst uint32_t modeRate=(uint32_t)notifyHz<<16;
\tbool modeOK=true;
\tmodeOK=num(color,"ID",1,32)&&modeOK;
\tmodeOK=num(color,"PixelEncoding",0,32)&&modeOK;
\tmodeOK=num(color,"Depth",8,32)&&modeOK;
\tmodeOK=num(color,"Colorimetry",1,32)&&modeOK;
\tmodeOK=num(color,"EOTF",1,32)&&modeOK;
\tmodeOK=num(color,"DynamicRange",0,32)&&modeOK;
\tmodeOK=num(color,"Score",1,32)&&modeOK;
\tmodeOK=color->setObject("IsVirtual",kOSBooleanFalse)&&modeOK;
\tmodeOK=colors->setObject(color)&&modeOK;
\tmodeOK=num(horizontal,"Active",HOUJI_LOGICAL_W,32)&&modeOK;
\tmodeOK=num(horizontal,"Total",HOUJI_LOGICAL_W,32)&&modeOK;
\tmodeOK=num(horizontal,"SyncRate",modeRate,32)&&modeOK;
\tmodeOK=num(horizontal,"PreciseSyncRate",modeRate,32)&&modeOK;
\tmodeOK=num(vertical,"Active",HOUJI_LOGICAL_H,32)&&modeOK;
\tmodeOK=num(vertical,"Total",HOUJI_LOGICAL_H,32)&&modeOK;
\tmodeOK=num(vertical,"SyncRate",modeRate,32)&&modeOK;
\tmodeOK=num(vertical,"PreciseSyncRate",modeRate,32)&&modeOK;
\tmodeOK=num(refresh,"displayMinRefreshInterval",(1ull<<32)/notifyHz,64)&&modeOK;
\tmodeOK=num(refresh,"displayMaxRefreshInterval",(1ull<<32)/notifyHz,64)&&modeOK;
\tmodeOK=num(refresh,"displayMinRefreshIntervalMachTime",intervalMach,64)&&modeOK;
\tmodeOK=num(refresh,"displayMaxRefreshIntervalMachTime",intervalMach,64)&&modeOK;
\tmodeOK=num(refresh,"displayRefreshStep",0,64)&&modeOK;
\tmodeOK=num(refresh,"displayRefreshStepMachTime",0,64)&&modeOK;
\tmodeOK=num(timing,"ID",1,32)&&modeOK;
\tmodeOK=num(timing,"Score",1,32)&&modeOK;
\tmodeOK=timing->setObject("IsVirtual",kOSBooleanFalse)&&modeOK;
\tmodeOK=timing->setObject("IsOverscanned",kOSBooleanFalse)&&modeOK;
\tmodeOK=timing->setObject("HorizontalAttributes",horizontal)&&modeOK;
\tmodeOK=timing->setObject("VerticalAttributes",vertical)&&modeOK;
\tmodeOK=timing->setObject("ColorModes",colors)&&modeOK;
\tmodeOK=timings->setObject(timing)&&modeOK;
\tif(modeOK){modeOK=setProperty("TimingElements",timings)&&setProperty("ColorElements",colors)&&setProperty("IOMFBDisplayRefresh",refresh);}
\tOSSafeReleaseNULL(colors);OSSafeReleaseNULL(color);'''
patch(fb, [(mode_alloc_old, mode_alloc_new, 'mode dict alloc'),
           (mode_old, mode_new, 'mode elements: timing ID/Total/ColorModes/ColorElements')])
# start() 卡在哪：4=计时字典分配失败, 5=TimingElements 属性失败, 100=已 registerService。
# stage 5 不再 return false —— 时序属性对 IOFB 枚举不是必需，服务必须先发布出去。
stage_old = "\tif(!modeOK)return false;\n\tregisterService();"
stage_new = ("\tif(!modeOK)houji_fb_start_stage=5;   /* ponytail: timing props are cosmetic; publish anyway */\n"
             "\tregisterService();\n\thouji_fb_start_stage=100;")
alloc_old = ("\t\tOSSafeReleaseNULL(horizontal);OSSafeReleaseNULL(vertical);OSSafeReleaseNULL(timing);OSSafeReleaseNULL(refresh);OSSafeReleaseNULL(timings);\n"
             "\t\treturn false;")
alloc_new = ("\t\thouji_fb_start_stage=4;\n"
             "\t\tOSSafeReleaseNULL(horizontal);OSSafeReleaseNULL(vertical);OSSafeReleaseNULL(timing);OSSafeReleaseNULL(refresh);OSSafeReleaseNULL(timings);\n"
             "\t\treturn false;")
patch(fb, [(stage_old, stage_new, 'start stage 100'),
           (alloc_old, alloc_new, 'start stage 4')])

# 用户态往 /dev/console 写 → km.c → console_write() 的环形缓冲 → cons_ops.putc，
# 完全不经过 _cnputs 的静音判断；静音必须加在这个汇合点，屏幕才会真的停住。
cw_old = "void\nconsole_write(char *str, int size)\n{\n\tassert(str != NULL);"
cw_new = ("void\nconsole_write(char *str, int size)\n{\n"
          "#if defined(ARM64_BOARD_CONFIG_SWEET)\n"
          "\textern int houji_console_muted;\n"
          "\tif (houji_console_muted && !panic_active()) {\n"
          "\t\treturn;\n"
          "\t}\n"
          "#endif\n"
          "\tassert(str != NULL);")
patch('osfmk/console/serial_console.c', [(cw_old, cw_new, 'console_write mute')])

# 用户态到底碰没碰 IOKit：每次 IOServiceOpen 打一行类名（上限 400 行，
# 尾巴只留最后 12 条）。没有这层就永远不知道 backboardd 停在哪。
NL = chr(10); T = chr(9); BS = chr(92); Q = chr(34)
nu_old = ("IOReturn" + NL
          + "IOService::newUserClient( task_t owningTask, void * securityID," + NL
          + "    UInt32 type, OSDictionary * properties," + NL
          + "    IOUserClient ** handler )" + NL + "{" + NL)
nu_new = nu_old + (
    T + "{" + NL
    + T + "    char houji_nm[32];" + NL
    + T + "    proc_selfname(houji_nm, sizeof(houji_nm));" + NL
    + T + "    if (strncmp(houji_nm, " + Q + "backboardd" + Q + ", 10) == 0 ||" + NL
    + T + "        strncmp(houji_nm, " + Q + "SpringBoard" + Q + ", 11) == 0) {" + NL
    + T + "        strlcpy(houji_iok_ring[houji_iok_ring_idx++ % 6]," + NL
    + T + "            getMetaClass()->getClassName(), sizeof(houji_iok_ring[0]));" + NL
    + T + "        __atomic_add_fetch(&houji_iok_count, 1u, __ATOMIC_RELAXED);" + NL
    + T + "    }" + NL
    + T + "}" + NL)
patch('iokit/Kernel/IOService.cpp', [(nu_old, nu_new, 'userclient trace')])

log_old = "\t\tif (ret == KERN_SUCCESS) {\n\t\t\tmap_info.fbmi_addr = (uint64_t) (user_addr);"
log_new = ("\t\tprintf(\"oslog: LOGBUFFERMAP ret=%d size=%llu kaddr=0x%llx user=0x%llx\\n\", ret,\n"
           "\t\t    (unsigned long long)buffer_size,\n"
           "\t\t    (unsigned long long)kernel_firehose_buffer,\n"
           "\t\t    (unsigned long long)user_addr);\n"
           "\t\tif (ret == KERN_SUCCESS) {\n"
           "\t\t\tmap_info.fbmi_addr = (uint64_t) (user_addr);")
patch('bsd/kern/subr_log.c', [(log_old, log_new, 'LOGBUFFERMAP diag')])

per_old = ("int\npersona_find_by_type(persona_type_t persona_type, struct persona **persona, size_t *plen)\n"
           "{\n\treturn persona_find_all(NULL, PERSONA_ID_NONE, persona_type, persona, plen);\n}")
per_new = ("int\npersona_find_by_type(persona_type_t persona_type, struct persona **persona, size_t *plen)\n"
           "{\n\tint ret = persona_find_all(NULL, PERSONA_ID_NONE, persona_type, persona, plen);\n\n"
           "\tif (ret == ESRCH && persona_type == PERSONA_SYSTEM_PROXY) {\n"
           "\t\tret = persona_find_all(NULL, PERSONA_ID_NONE, PERSONA_SYSTEM, persona, plen);\n"
           "\t\tprintf(\"persona: SYSTEM_PROXY missing, used SYSTEM (ret=%d)\\n\", ret);\n\t}\n"
           "\treturn ret;\n}")
patch('bsd/kern/kern_persona.c', [(per_old, per_new, 'SYSTEM_PROXY fallback')])

# launchd 查 persona 走的是 sys_persona.c 的 kpersona_find_syscall（不是 persona_find_by_type），
# 系统里从来没人创建过 SYSTEM/SYSTEM_PROXY，所以每次 spawn 都 ESRCH -> xpcproxy exit(78)。
# 在真正被调用的路径上按需补建一次；创建失败不改变原有错误码。
sp_old = "\terror = persona_find_all(login, kinfo.persona_id, (persona_type_t)kinfo.persona_type, persona, &k_idlen);\n"
sp_new = "\thouji_persona_calls++;\n" + sp_old + (
    "\tif (error != 0 && (kinfo.persona_type == PERSONA_SYSTEM ||\n"
    "\t    kinfo.persona_type == PERSONA_SYSTEM_PROXY) &&\n"
    "\t    (kinfo.persona_type == PERSONA_SYSTEM ? !tried_sys : !tried_proxy)) {\n"
    "\t\tint perr = 0, ierr = 0;\n"
    "\t\tconst char *nm;\n"
    "\t\tstruct persona *np;\n"
    "\t\tif (kinfo.persona_type == PERSONA_SYSTEM) {\n"
    "\t\t\ttried_sys = 1;\n"
    "\t\t\tnm = \"houji-sys\";\n"
    "\t\t} else {\n"
    "\t\t\ttried_proxy = 1;\n"
    "\t\t\tnm = \"houji-proxy\";\n"
    "\t\t}\n"
    "\t\tnp = persona_alloc(PERSONA_ID_NONE, nm,\n"
    "\t\t    (persona_type_t)kinfo.persona_type, NULL,\n"
    "\t\t    kinfo.persona_type == PERSONA_SYSTEM ? 501 : PERSONA_ID_NONE, &perr);\n"
    "\t\tif (np != NULL) {\n"
    "\t\t\tierr = persona_init_begin(np);\n"
    "\t\t\tpersona_init_end(np, ierr);\n"
    "\t\t\tif (ierr == 0) {\n"
    "\t\t\t\thouji_persona_created++;\n"
    "\t\t\t\tk_idlen = u_idlen;\n"
    "\t\t\t\terror = persona_find_all(login, kinfo.persona_id,\n"
    "\t\t\t\t    (persona_type_t)kinfo.persona_type, persona, &k_idlen);\n"
    "\t\t\t\tprintf(\"houji: persona type %u created id=%u find=%d\\n\",\n"
    "\t\t\t\t    (unsigned)kinfo.persona_type, (unsigned)np->pna_id, error);\n"
    "\t\t\t} else {\n"
    "\t\t\t\tpersona_put(np);\n"
    "\t\t\t\tprintf(\"houji: persona type %u init failed %d\\n\",\n"
    "\t\t\t\t    (unsigned)kinfo.persona_type, ierr);\n"
    "\t\t\t}\n"
    "\t\t} else {\n"
    "\t\t\tprintf(\"houji: persona type %u create failed %d\\n\",\n"
    "\t\t\t    (unsigned)kinfo.persona_type, perr);\n"
    "\t\t}\n"
    "\t\tif (kinfo.persona_type == PERSONA_SYSTEM_PROXY) {\n"
    "\t\t\tint e2 = 0, i2 = 0;\n"
    "\t\t\tstruct persona *np2 = persona_alloc(PERSONA_ID_NONE,\n"
    "\t\t\t    \"houji-proxy501\", PERSONA_SYSTEM_PROXY, NULL, 501, &e2);\n"
    "\t\t\tif (np2 != NULL) {\n"
    "\t\t\t\ti2 = persona_init_begin(np2);\n"
    "\t\t\t\tpersona_init_end(np2, i2);\n"
    "\t\t\t\tif (i2 != 0) {\n"
    "\t\t\t\t\tpersona_put(np2);\n"
    "\t\t\t\t} else {\n"
    "\t\t\t\t\tprintf(\"houji: persona proxy uid501 id=%u\\n\",\n"
    "\t\t\t\t\t    (unsigned)np2->pna_id);\n"
    "\t\t\t\t}\n"
    "\t\t\t} else {\n"
    "\t\t\t\tprintf(\"houji: persona proxy uid501 failed %d\\n\", e2);\n"
    "\t\t\t}\n"
    "\t\t}\n"
    "\t}\n")
patch('bsd/kern/sys_persona.c', [("#include <sys/persona.h>\n",
    "#include <sys/persona.h>\n\nstatic int tried_sys, tried_proxy;\n"
    "unsigned houji_persona_calls, houji_persona_created;\n",
    'persona retry flags')])
patch('bsd/kern/sys_persona.c', [(sp_old, sp_new, 'create missing system persona')])

# 采样线程每拍打一行 persona 计数，无论日志怎么滚都能看到建没建成。
ss_old = "#include <pexpert/pexpert.h>\n"
ss_new = ("#include <pexpert/pexpert.h>\n"
          "extern unsigned houji_persona_calls, houji_persona_created;\n"
          "extern int houji_mkdir_sys(const char *path, int mode);\n"
          "int houji_dirs_rc_global[7] = { -999, -999, -999, -999, "
          "    -999, -999, -999 };\n")
patch('bsd/dev/sweet_sample.c', [(ss_old, ss_new, 'persona counters')])
ss_line = "\t\t\tprintf(\"HOUJI ID: platform UUID published=%u\\n\", gIOPlatformUUIDAndSerialDone);\n"
ss_line_new = ss_line + (
    "\t\t\tprintf(\"HOUJI PERSONA: calls=%u created=%u\\n\",\n"
    "\t\t\t    houji_persona_calls, houji_persona_created);\n")
patch('bsd/dev/sweet_sample.c', [(ss_line, ss_line_new, 'persona counter line')])


# —— 日志落盘：不再靠拍照 ——
# 上游 archive/docs/log-capture.md 的结论：XNU 的 USB CDC 原型在真机从未枚举成功
# （缺 SMMU 的 USB SID 路由），可用路径是“内核写盘 → 重启进 Recovery 用 adb 取回”。
# 我们把 1 MiB 控制台环每 5 秒写到 md0 的 24 GiB 处（卷内自由块；我们只刷了前 13 GiB，
# 24 GiB 落在同一 userdata 分区里、远离 HFSX 元数据），主机侧 dd 拉 1 MiB 即可。

# —— 谁在拦 posix_spawn：把失败的原始 errno / vnode / persona 打出来 ——
sp_bad_old = "bad:\n\n\tif (error == 0) {\n"
sp_bad_new = (
    "bad:\n"
    "\tif (error != 0 && imgp != NULL && imgp->ip_user_fname != USER_ADDR_NULL) {\n"
    "\t\tchar houji_path[MAXPATHLEN];\n"
    "\t\tsize_t houji_len = 0;\n\n"
    "\t\tif (copyinstr(imgp->ip_user_fname, houji_path,\n"
    "\t\t    sizeof(houji_path), &houji_len) == 0) {\n"
    "\t\t\tuint64_t houji_psid = 0;\n"
    "\t\t\tunsigned houji_mode = 0;\n"
    "#if CONFIG_PERSONAS\n"
    "\t\t\tif (imgp->ip_px_persona != NULL) {\n"
    "\t\t\t\thouji_psid = (uint64_t)((struct _posix_spawn_persona_info *)\n"
    "\t\t\t\t    imgp->ip_px_persona)->pspi_id;\n"
    "\t\t\t}\n"
    "#endif\n"
    "\t\t\tif (imgp->ip_vp != NULLVP && imgp->ip_vattr != NULL) {\n"
    "\t\t\t\thouji_mode = (unsigned)imgp->ip_vattr->va_mode;\n"
    "\t\t\t}\n"
    "\t\t\tprintf(\"HOUJI SPAWN FAIL %s err=%d vp=%s mode=%o persona=%llu\\n\",\n"
    "\t\t\t    houji_path, error, imgp->ip_vp != NULLVP ? \"yes\" : \"no\",\n"
    "\t\t\t    houji_mode, (unsigned long long)houji_psid);\n"
    "\t\t}\n"
    "\t}\n\n"
    "\tif (error == 0) {\n")
patch('bsd/kern/kern_exec.c', [(sp_bad_old, sp_bad_new, 'posix_spawn failure detail')])

# —— 卡住的线程到底停在哪个系统调用：arm64 的调用号在 x16 ——
smp_old = "struct sweet_sample_regs { uint64_t pc, lr, sp, fp; };"
smp_new = "struct sweet_sample_regs { uint64_t pc, lr, sp, fp, x16, x0; };"
patch('osfmk/kern/exception.c', [(smp_old, smp_new, 'sample regs x16/x0')])

cap_old = ("\t\tregs[count++] = (struct sweet_sample_regs) {\n"
           "\t\t\t.pc = get_saved_state_pc(ss), .lr = get_saved_state_lr(ss),\n"
           "\t\t\t.sp = get_saved_state_sp(ss), .fp = get_saved_state_fp(ss),\n"
           "\t\t};")
cap_new = ("\t\tregs[count++] = (struct sweet_sample_regs) {\n"
           "\t\t\t.pc = get_saved_state_pc(ss), .lr = get_saved_state_lr(ss),\n"
           "\t\t\t.sp = get_saved_state_sp(ss), .fp = get_saved_state_fp(ss),\n"
           "\t\t\t.x16 = (uint64_t)const_saved_state64(ss)->x[16],\n"
           "\t\t\t.x0 = (uint64_t)const_saved_state64(ss)->x[0],\n"
           "\t\t};")
patch('osfmk/kern/exception.c', [(cap_old, cap_new, 'capture x16/x0')])

pr_old = ("\t\tint n = snprintf(line, sizeof(line), \"SMP %s t%u pc 0x%llx lr 0x%llx sp 0x%llx bt\",\n"
          "\t\t    name, tn, regs[tn].pc, regs[tn].lr, regs[tn].sp);")
pr_new = ("\t\tint n = snprintf(line, sizeof(line),\n"
          "\t\t    \"SMP %s t%u pc 0x%llx lr 0x%llx sp 0x%llx x16 %llu x0 0x%llx bt\",\n"
          "\t\t    name, tn, regs[tn].pc, regs[tn].lr, regs[tn].sp, regs[tn].x16, regs[tn].x0);")
patch('osfmk/kern/exception.c', [(pr_old, pr_new, 'thread line x16/x0')])


# —— firehose 被 ATM 位挡掉 → kernel_firehose_addr=0 → logd 拿不到日志缓冲，
# LOGBUFFERMAP 返回 1，logd 立刻 SIGTRAP 自杀并循环重启。日志开关只影响日志，强制打开。
fh_old = ("\tif (os_log_disabled()) {\n"
          "\t\tprintf(\"Firehose disabled: Logging disabled by ATM\\n\");\n"
          "\t\treturn;\n"
          "\t}")
fh_new = ("\tif (os_log_disabled()) {\n"
          "\t\tprintf(\"Firehose forced on (ATM said disabled)\\n\");\n"
          "\t}")
patch('bsd/kern/subr_log.c', [(fh_old, fh_new, 'force firehose on')])


# —— logd 映射日志缓冲：__firehose_buffer_create 是桩（恒返回 0）→ LOGBUFFERMAP 必失败 →
# logd SIGTRAP 自杀并 58 次循环。这里在映射前补一块清零缓冲顶上，让 logd 至少能拿到地址。
fake_old = ("\tcase LOGBUFFERMAP:\n"
            "\t\tkernel_firehose_buffer = (firehose_buffer_t)kernel_firehose_addr;\n")
fake_new = ("\tcase LOGBUFFERMAP:\n"
            "\t\tkernel_firehose_buffer = (firehose_buffer_t)kernel_firehose_addr;\n"
            "\t\tif (kernel_firehose_buffer == NULL) {\n"
            "\t\t\tstatic vm_offset_t houji_log_fake;\n"
            "\t\t\tif (houji_log_fake == 0) {\n"
            "\t\t\t\tkmem_alloc(kernel_map, &houji_log_fake,\n"
            "\t\t\t\t    (vm_size_t)buffer_size,\n"
            "\t\t\t\t    KMA_NOFAIL | KMA_PERMANENT | KMA_ZERO,\n"
            "\t\t\t\t    VM_KERN_MEMORY_LOG);\n"
            "\t\t\t}\n"
            "\t\t\tkernel_firehose_buffer = (firehose_buffer_t)houji_log_fake;\n"
            "\t\t\tprintf(\"HOUJI OSLOG: firehose stub, zeroed %llu bytes at 0x%llx\\n\",\n"
            "\t\t\t    (unsigned long long)buffer_size,\n"
            "\t\t\t    (unsigned long long)houji_log_fake);\n"
            "\t\t}\n")
patch('bsd/kern/subr_log.c', [(fake_old, fake_new, 'firehose stub fallback')])


# —— HOUJI: 内核自建 /private/var/containers ——
# 真机镜像里没有这个目录，containermanagerd 因此给不出系统容器路径，
# backboardd 卡在 StartWindowServer（上游 ios-sweet 的 0051 记录）。
# 卷是 rw 挂的（rootrw=1），直接在内核里 mkdir，不用改镜像、不用重刷。
mk_anchor = ("/*\n"
             " * mkdir_extended: Create a directory; with extended security (ACL).\n")
mk_new = ("/*\n"
          " * HOUJI: 内核态 mkdir，复用上面的 mkdir1at()；path 是内核字符串。\n"
          " */\n"
          "extern vfs_context_t vfs_context_kernel(void);\n"
          "int\n"
          "houji_mkdir_sys(const char *path, int mode)\n"
          "{\n"
          "\tstruct vnode_attr va;\n"
          "\n"
          "\tVATTR_INIT(&va);\n"
          "\tVATTR_SET(&va, va_mode, (mode_t)mode);\n"
          "\treturn mkdir1at(vfs_context_kernel(),\n"
          "\t    (user_addr_t)(uintptr_t)path, &va, -2 /* AT_FDCWD */,\n"
          "\t    UIO_SYSSPACE);\n"
          "}\n"
          "\n") + mk_anchor
patch('bsd/vfs/vfs_syscalls.c', [(mk_anchor, mk_new, 'kernel mkdir helper')])

dirs_anchor = ("\t\t\tprintf(\"HOUJI ID: platform UUID published=%u\\n\", "
               "gIOPlatformUUIDAndSerialDone);\n")
dirs_new = ("\t\t\t{\n"
            "\t\t\t\tstatic const char *const houji_dirs[] = {\n"
            "\t\t\t\t    \"/private/var/containers\",\n"
            "\t\t\t\t    \"/private/var/containers/Bundle\",\n"
            "\t\t\t\t    \"/private/var/containers/Data\",\n"
            "\t\t\t\t    \"/private/var/containers/Data/System\",\n"
            "\t\t\t\t    \"/private/var/containers/Shared\",\n"
            "\t\t\t\t    \"/private/var/containers/Shared/SystemGroup\",\n"
            "\t\t\t\t    \"/private/var/containers/Temp\",\n"
            "\t\t\t\t};\n"
            "\t\t\t\tstatic int houji_dir_rc[7] = { -999, -999, -999, -999,\n"
            "\t\t\t\t    -999, -999, -999 };\n"
            "\t\t\t\tstatic int houji_dirs_done;\n"
            "\t\t\t\tif (!houji_dirs_done) {\n"
            "\t\t\t\t\tunsigned houji_i;\n"
            "\t\t\t\t\thouji_dirs_done = 1;\n"
            "\t\t\t\t\tfor (houji_i = 0;\n"
            "\t\t\t\t\t    houji_i < sizeof(houji_dirs) / sizeof(houji_dirs[0]);\n"
            "\t\t\t\t\t    houji_i++) {\n"
            "\t\t\t\t\t\tint rc = houji_mkdir_sys(\n"
            "\t\t\t\t\t\t    houji_dirs[houji_i], 0755);\n"
            "\t\t\t\t\t\thouji_dir_rc[houji_i] = rc;\n"
            "\t\t\t\t\t\thouji_dirs_rc_global[houji_i] = rc;\n"
            "\t\t\t\t\t\tprintf(\"HOUJI MKDIR %s -> %d\\n\",\n"
            "\t\t\t\t\t\t    houji_dirs[houji_i], rc);\n"
            "\t\t\t\t\t}\n"
            "\t\t\t\t}\n"
            "\t\t\t\t/* 每拍都打，别再滚没了 */\n"
            "\t\t\t\tprintf(\"HOUJI DIRS: %d %d %d %d %d %d %d\\n\",\n"
            "\t\t\t\t    houji_dir_rc[0], houji_dir_rc[1], houji_dir_rc[2],\n"
            "\t\t\t\t    houji_dir_rc[3], houji_dir_rc[4], houji_dir_rc[5],\n"
            "\t\t\t\t    houji_dir_rc[6]);\n"
            "\t\t\t}\n") + dirs_anchor
patch('bsd/dev/sweet_sample.c', [(dirs_anchor, dirs_new, 'containers mkdir')])


# —— HOUJI: 卷没有 journal，每次强制关机都会留 dirty 位，HFS 就拒绝读写挂载 ——
# 挂载前把 kHFSVolumeUnmounted 位补回内存里的卷头，当成干净卷挂。
dirty_anchor = ("\tblockSize = SWAP_BE32(vhp->blockSize);\n"
                "\t/* don't mount a writable volume if its dirty, "
                "it must be cleaned by fsck_hfs */\n")
dirty_new = ("\tblockSize = SWAP_BE32(vhp->blockSize);\n"
             "\t/* HOUJI: 无 journal + 强制断电 => dirty 位常驻，"
             "直接按干净卷挂载 */\n"
             "\tif (hfsmp->jnl == NULL) {\n"
             "\t\tvhp->attributes |= SWAP_BE32(kHFSVolumeUnmountedMask);\n"
             "\t\tprintf(\"HOUJI HFS: dirty bit cleared, mounting rw\\n\");\n"
             "\t}\n"
             "\t/* don't mount a writable volume if its dirty, "
             "it must be cleaned by fsck_hfs */\n") + dirty_anchor
patch('bsd/sweet_hfs/core/hfs_vfsutils.c',
      [(dirty_anchor, dirty_new, 'dirty volume bypass')])


# —— 采样块最后一行再打一次 DIRS，保证在 45 行可见区 ——
tail_anchor = "\t\t\t\thouji_ufs_checkpoint(\"before userland stack sampling\", 0);\n"
tail_new = tail_anchor + (
    "\t\t\t\tprintf(\"HOUJI DIRS: %d %d %d %d %d %d %d\\n\",\n"
    "\t\t\t\t    houji_dirs_rc_global[0], houji_dirs_rc_global[1],\n"
    "\t\t\t\t    houji_dirs_rc_global[2], houji_dirs_rc_global[3],\n"
    "\t\t\t\t    houji_dirs_rc_global[4], houji_dirs_rc_global[5],\n"
    "\t\t\t\t    houji_dirs_rc_global[6]);\n")
patch('bsd/dev/sweet_sample.c', [(tail_anchor, tail_new, 'dirs tail print')])


# —— 进程名轮播：每拍打 1/8 的进程名，8 拍覆盖全部 ——
rot_anchor = "static void\nsweet_sample_thread(__unused void *arg, __unused wait_result_t wr)\n"
rot_new = ("static int houji_rot_index;\n"
           "static int houji_rot_mod;\n"
           "\n"
           "static int\n"
           "houji_rot_proc(proc_t p, __unused void *arg, __unused int32_t *retval)\n"
           "{\n"
           "\tchar name[32];\n"
           "\n"
           "\tproc_name(proc_pid(p), name, sizeof(name));\n"
           "\tif ((houji_rot_index % 8) == houji_rot_mod) {\n"
           "\t\tprintf(\"HOUJI P%02d %s[%d]\\n\", houji_rot_index, name,\n"
           "\t\t    proc_pid(p));\n"
           "\t}\n"
           "\thouji_rot_index++;\n"
           "\treturn PROC_RETURNED;\n"
           "}\n"
           "\n") + rot_anchor
patch('bsd/dev/sweet_sample.c', [(rot_anchor, rot_new, 'rotating proc names')])

rot_call = ("\t\t\t\thouji_rot_index = 0;\n"
            "\t\t\t\thouji_rot_mod = (int)(i % 8);\n"
            "\t\t\t\tprintf(\"HOUJI PROC ROT %d/8\\n\", houji_rot_mod);\n"
            "\t\t\t\tproc_iterate(PROC_ALLPROCLIST, houji_rot_proc, NULL, NULL,\n"
            "\t\t\t\t    NULL);\n")
tail_anchor2 = "\t\t\t\thouji_ufs_checkpoint(\"before userland stack sampling\", 0);\n"
tail_new2 = rot_call + tail_anchor2
patch('bsd/dev/sweet_sample.c', [(tail_anchor2, tail_new2, 'rotating proc call')])


# —— 关键守护进程是否活着：每拍一行 pid ——
keys_anchor = "static int houji_rot_index;\n"
keys_new = ("#define HOUJI_NKEYS 12\n"
            "static const char *const houji_key_names[HOUJI_NKEYS] = {\n"
            "\t\"containermanagerd\", \"usermanagerd\", \"securityd\",\n"
            "\t\"runningboardd\", \"assertiond\", \"distnoted\", \"notifyd\",\n"
            "\t\"configd\", \"logd\", \"mobileassetd\", \"powerd\",\n"
            "\t\"backboardd\"\n"
            "};\n"
            "static int houji_key_pids[HOUJI_NKEYS];\n"
            "\n"
            "static int\n"
            "houji_keys_proc(proc_t p, __unused void *arg, __unused int32_t *retval)\n"
            "{\n"
            "\tchar name[32];\n"
            "\tunsigned k;\n"
            "\n"
            "\tproc_name(proc_pid(p), name, sizeof(name));\n"
            "\tfor (k = 0; k < HOUJI_NKEYS; k++) {\n"
            "\t\tif (strncmp(name, houji_key_names[k], 15) == 0) {\n"
            "\t\t\thouji_key_pids[k] = proc_pid(p);\n"
            "\t\t}\n"
            "\t}\n"
            "\treturn PROC_RETURNED;\n"
            "}\n"
            "\n") + keys_anchor
patch('bsd/dev/sweet_sample.c', [(keys_anchor, keys_new, 'key daemon pids')])

keys_call = ("\t\t\t\tbzero(houji_key_pids, sizeof(houji_key_pids));\n"
             "\t\t\t\tproc_iterate(PROC_ALLPROCLIST, houji_keys_proc, NULL, NULL,\n"
             "\t\t\t\t    NULL);\n"
             "\t\t\t\tprintf(\"HOUJI KEYS1 cman=%d uman=%d secd=%d rbd=%d asrt=%d\\n\",\n"
             "\t\t\t\t    houji_key_pids[0], houji_key_pids[1],\n"
             "\t\t\t\t    houji_key_pids[2], houji_key_pids[3],\n"
             "\t\t\t\t    houji_key_pids[4]);\n"
             "\t\t\t\tprintf(\"HOUJI KEYS2 dnot=%d nfy=%d cfg=%d logd=%d masd=%d\\n\",\n"
             "\t\t\t\t    houji_key_pids[5], houji_key_pids[6],\n"
             "\t\t\t\t    houji_key_pids[7], houji_key_pids[8],\n"
             "\t\t\t\t    houji_key_pids[9]);\n"
             "\t\t\t\tprintf(\"HOUJI KEYS3 pwr=%d bb=%d\\n\",\n"
             "\t\t\t\t    houji_key_pids[10], houji_key_pids[11]);\n")
tail_anchor = "\t\t\t\thouji_ufs_checkpoint(\"before userland stack sampling\", 0);\n"
patch('bsd/dev/sweet_sample.c', [(tail_anchor, keys_call + tail_anchor, 'key daemon pids call')])

# —— SPAWN FAIL 加文件大小（回答 ENOEXEC 是不是零长度）——
patch('bsd/kern/kern_exec.c', [
    ("\t\t\tunsigned houji_mode = 0;\n",
     "\t\t\tunsigned houji_mode = 0;\n"
     "\t\t\tuint64_t houji_size = 0;\n",
     'spawn size decl'),
    ("\t\t\t\thouji_mode = (unsigned)imgp->ip_vattr->va_mode;\n",
     "\t\t\t\thouji_mode = (unsigned)imgp->ip_vattr->va_mode;\n"
     "\t\t\t\thouji_size = imgp->ip_vattr->va_data_size;\n",
     'spawn size read'),
    ("\t\t\tprintf(\"HOUJI SPAWN FAIL %s err=%d vp=%s mode=%o persona=%llu\\n\",\n",
     "\t\t\tprintf(\"HOUJI SPAWN FAIL %s err=%d vp=%s mode=%o"
     " size=%llu persona=%llu\\n\",\n",
     'spawn size format'),
    ("\t\t\t    houji_mode, (unsigned long long)houji_psid);\n",
     "\t\t\t    houji_mode, (unsigned long long)houji_size,\n"
     "\t\t\t    (unsigned long long)houji_psid);\n",
     'spawn size args'),
])

print('SNIPPET14C_DONE')


# —— uid 0 的 SYSTEM / SYSTEM_PROXY persona：root 任务（containermanagerd、
# securityd、usermanagerd、assertiond）查的是 uid 0，之前只建了 501 ——
helper_anchor = "unsigned houji_persona_calls, houji_persona_created;\n"
helper_new = (helper_anchor +
              "static int houji_sys0_done;\n"
              "extern unsigned houji_persona_created;\n"
              "\n"
              "static void\n"
              "houji_make_persona(const char *name, persona_type_t type, uint32_t uid)\n"
              "{\n"
              "\tint perr = 0, ierr;\n"
              "\tstruct persona *np;\n"
              "\n"
              "\tnp = persona_alloc(PERSONA_ID_NONE, name, type, NULL, uid, &perr);\n"
              "\tif (np == NULL) {\n"
              "\t\tprintf(\"houji: persona %s uid=%u create failed %d\\n\",\n"
              "\t\t    name, uid, perr);\n"
              "\t\treturn;\n"
              "\t}\n"
              "\tierr = persona_init_begin(np);\n"
              "\tpersona_init_end(np, ierr);\n"
              "\tif (ierr != 0) {\n"
              "\t\tpersona_put(np);\n"
              "\t\tprintf(\"houji: persona %s uid=%u init failed %d\\n\",\n"
              "\t\t    name, uid, ierr);\n"
              "\t\treturn;\n"
              "\t}\n"
              "\tprintf(\"houji: persona %s uid=%u id=%u\\n\", name, uid,\n"
              "\t    (unsigned)np->pna_id);\n"
              "\thouji_persona_created++;\n"
              "}\n")
patch('bsd/kern/sys_persona.c', [(helper_anchor, helper_new, 'persona make helper')])

once_anchor = "\thouji_persona_calls++;\n"
once_new = (once_anchor +
            "\tif (!houji_sys0_done) {\n"
            "\t\thouji_sys0_done = 1;\n"
            "\t\thouji_make_persona(\"houji-sys0\", PERSONA_SYSTEM, 0);\n"
            "\t\thouji_make_persona(\"houji-proxy0\", PERSONA_SYSTEM_PROXY, 0);\n"
            "\t}\n")
patch('bsd/kern/sys_persona.c', [(once_anchor, once_new, 'create uid0 personas')])
print('SNIPPET15_DONE')


# —— 每拍都打 KEYS/DIRS（原来只在 i%4 才打，照片总错过）——
patch('bsd/dev/sweet_sample.c', [
    ("\t\t\tif ((i % 4) == 0) {\n", "\t\t\tif (1) {\n", 'every sample'),
])

# —— uid0 persona 成败可见：sys0/proxy0 标志 ——
patch('bsd/kern/sys_persona.c', [
    ("\tprintf(\"houji: persona %s uid=%u id=%u\\n\", name, uid,\n\t    (unsigned)np->pna_id);\n"
     "\thouji_persona_created++;\n",
     "\tprintf(\"houji: persona %s uid=%u id=%u\\n\", name, uid,\n"
     "\t    (unsigned)np->pna_id);\n"
     "\tif (type == PERSONA_SYSTEM) {\n"
     "\t\thouji_persona_sys0_ok = 1;\n"
     "\t} else if (type == PERSONA_SYSTEM_PROXY) {\n"
     "\t\thouji_persona_proxy0_ok = 1;\n"
     "\t}\n"
     "\thouji_persona_created++;\n",
     'persona uid0 flags'),
    ("static int houji_sys0_done;\n",
     "int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n"
     "static int houji_sys0_done;\n",
     'persona uid0 globals'),
])

patch('bsd/dev/sweet_sample.c', [
    ("\t\t\tprintf(\"HOUJI PERSONA: calls=%u created=%u\\n\",\n"
     "\t\t\t    houji_persona_calls, houji_persona_created);\n",
     "\t\t\tprintf(\"HOUJI PERSONA: calls=%u created=%u sys0=%d proxy0=%d\\n\",\n"
     "\t\t\t    houji_persona_calls, houji_persona_created,\n"
     "\t\t\t    houji_persona_sys0_ok, houji_persona_proxy0_ok);\n",
     'persona line uid0'),
    ("int houji_dirs_rc_global[7] = {",
     "extern int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n"
     "int houji_dirs_rc_global[7] = {",
     'persona extern'),
])
print('SNIPPET16_DONE')


# —— 开机就建 uid0 persona（不要等用户态第一次查询，launchd 的 system 域
# 起得比那更早，一旦第一次查不到就不再重试）——
patch('bsd/kern/sys_persona.c', [
    ("\tif (!houji_sys0_done) {\n"
     "\t\thouji_sys0_done = 1;\n"
     "\t\thouji_make_persona(\"houji-sys0\", PERSONA_SYSTEM, 0);\n"
     "\t\thouji_make_persona(\"houji-proxy0\", PERSONA_SYSTEM_PROXY, 0);\n"
     "\t}\n",
     "\thouji_persona_bootstrap();\n",
     'persona lazy -> bootstrap'),
    ("int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n"
     "static int houji_sys0_done;\n",
     "int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n"
     "static int houji_sys0_done;\n"
     "static void houji_make_persona(const char *name, persona_type_t type,\n"
     "    uint32_t uid);\n"
     "\n"
     "void\n"
     "houji_persona_bootstrap(void)\n"
     "{\n"
     "\tif (!houji_sys0_done) {\n"
     "\t\thouji_sys0_done = 1;\n"
     "\t\thouji_make_persona(\"houji-sys0\", PERSONA_SYSTEM, 0);\n"
     "\t\thouji_make_persona(\"houji-proxy0\", PERSONA_SYSTEM_PROXY, 0);\n"
     "\t}\n"
     "}\n",
     'persona bootstrap fn'),
])

patch('bsd/dev/sweet_sample.c', [
    ("extern int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n",
     "extern int houji_persona_sys0_ok, houji_persona_proxy0_ok;\n"
     "extern void houji_persona_bootstrap(void);\n",
     'persona bootstrap extern'),
    ("sweet_sample_thread(__unused void *arg, __unused wait_result_t wr)\n{\n\tstatic int chan;\n",
     "sweet_sample_thread(__unused void *arg, __unused wait_result_t wr)\n"
     "{\n\tstatic int chan;\n\thouji_persona_bootstrap();\n",
     'persona bootstrap call'),
])
print('SNIPPET17_DONE')

# —— 观测：显示栈的三个关键计数挪到采样块末尾（45 行可见区内）
#    这样每张照片都能直接看出 CA 有没有开 client / 调 selector / 建 surface。
fbo = "\t\t\t\tprintf(\"HOUJI KEYS3 pwr=%d bb=%d\\n\",\n\t\t\t\t    houji_key_pids[10], houji_key_pids[11]);\n"
fbn = fbo + (
    "\t\t\t\t{\n"
    "\t\t\t\t\textern uint64_t houji_fb_open_count, houji_fb_sel_count,\n"
    "\t\t\t\t\t    houji_surf_create_count;\n"
    "\t\t\t\t\tuint64_t hs = 0, hp = 0;\n"
    "\t\t\t\t\thouji_framebuffer_counts(&hs, &hp);\n"
    "\t\t\t\t\tprintf(\"HOUJI FBX open=%llu sel=%llu surf=%llu swap=%llu pres=%llu\\n\",\n"
    "\t\t\t\t\t    houji_fb_open_count, houji_fb_sel_count,\n"
    "\t\t\t\t\t    houji_surf_create_count, hs, hp);\n"
    "\t\t\t\t}\n")
patch('bsd/dev/sweet_sample.c', [(fbo, fbn, 'FBX status line')])

fbd = "static uint64_t sweet_fb_logged;        /* selectors already logged (< 64) */\n"
fbn2 = fbd + "uint64_t houji_fb_open_count, houji_fb_sel_count;\n"
fbs = "\tuint32_t outCount = 0;\n\n\tswitch (selector) {"
fbs2 = ("\tuint32_t outCount = 0;\n\n"
        "\t__atomic_add_fetch(&houji_fb_sel_count, 1ull, __ATOMIC_RELAXED);\n"
        "\tswitch (selector) {")
fbu = "\tIOLog(\"HOUJI FB: newUserClient type=%u ok\\n\", type);"
fbu2 = "\t__atomic_add_fetch(&houji_fb_open_count, 1ull, __ATOMIC_RELAXED);\n" + fbu
patch('iokit/Kernel/arm/SweetFramebuffer.cpp',
      [(fbd, fbn2, 'fb counters'), (fbs, fbs2, 'sel counter'), (fbu, fbu2, 'open counter')])

scd = "static uint32_t      sweet_surface_creates_logged;\n"
scd2 = scd + "uint64_t             houji_surf_create_count;\n"
sco = "\tprops->release();\n\tif (ret != kIOReturnSuccess) {\n\t\ts->release();\n\t\treturn ret;\n\t}\n"
sco2 = ("\tif (ret == kIOReturnSuccess) {\n"
        "\t\t__atomic_add_fetch(&houji_surf_create_count, 1ull, __ATOMIC_RELAXED);\n"
        "\t}\n") + sco
patch('iokit/Kernel/arm/SweetSurface.cpp',
      [(scd, scd2, 'surf counter'), (sco, sco2, 'surf inc')])

# —— 观测：最近 4 次 selector 的编号与返回码，直接进 FBX 行
patch('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('uint64_t houji_fb_open_count, houji_fb_sel_count;\n', 'uint64_t houji_fb_open_count, houji_fb_sel_count;\nuint64_t houji_fb_sel_hist[4];\nuint32_t houji_fb_sel_hist_idx;\n', 'sel hist decl'),
    ('\t    args->scalarOutputCount, args->structureOutputSize, ret);\n\t}\n\treturn ret;\n}\n', '\t    args->scalarOutputCount, args->structureOutputSize, ret);\n\t}\n\t__atomic_store_n(&houji_fb_sel_hist[__atomic_load_n(&houji_fb_sel_hist_idx, __ATOMIC_RELAXED) & 3],\n\t    ((uint64_t)selector << 32) | (uint32_t)ret, __ATOMIC_RELAXED);\n\t__atomic_add_fetch(&houji_fb_sel_hist_idx, 1, __ATOMIC_RELAXED);\n\treturn ret;\n}\n', 'sel hist record'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern uint64_t houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count;\n', '\t\t\t\t\textern uint64_t houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count;\n\t\t\t\t\textern uint64_t houji_fb_sel_hist[4];\n', 'fbx hist extern'),
    ('\t\t\t\t\tprintf("HOUJI FBX open=%llu sel=%llu surf=%llu swap=%llu pres=%llu\\n",\n\t\t\t\t\t    houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count, hs, hp);\n', '\t\t\t\t\tprintf("HOUJI FBX open=%llu sel=%llu surf=%llu swap=%llu pres=%llu h=%x:%x %x:%x %x:%x %x:%x\\n",\n\t\t\t\t\t    houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count, hs, hp,\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[0] >> 32), (unsigned)houji_fb_sel_hist[0],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[1] >> 32), (unsigned)houji_fb_sel_hist[1],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[2] >> 32), (unsigned)houji_fb_sel_hist[2],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[3] >> 32), (unsigned)houji_fb_sel_hist[3]);\n', 'fbx hist fields'),
])

# —— 观测：externalMethod 的在飞数量 + 最近一次入口 selector
#    sel>0 而 hist 全 0 说明调用进了我们的函数就没再返回，infl 会确认这一点。
patch('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('uint32_t houji_fb_sel_hist_idx;\n', 'uint32_t houji_fb_sel_hist_idx;\nuint64_t houji_fb_inflight, houji_fb_last_ret;\nuint32_t houji_fb_last_sel;\n', 'inflight decl'),
    ('\t__atomic_add_fetch(&houji_fb_sel_count, 1ull, __ATOMIC_RELAXED);\n', '\t__atomic_add_fetch(&houji_fb_sel_count, 1ull, __ATOMIC_RELAXED);\n\t__atomic_store_n(&houji_fb_last_sel, selector, __ATOMIC_RELAXED);\n\t__atomic_add_fetch(&houji_fb_inflight, 1ull, __ATOMIC_RELAXED);\n', 'inflight enter'),
    ('\t__atomic_add_fetch(&houji_fb_sel_hist_idx, 1, __ATOMIC_RELAXED);\n\treturn ret;\n}\n', '\t__atomic_add_fetch(&houji_fb_sel_hist_idx, 1, __ATOMIC_RELAXED);\n\t__atomic_store_n(&houji_fb_last_ret, (uint64_t)(uint32_t)ret, __ATOMIC_RELAXED);\n\t__atomic_sub_fetch(&houji_fb_inflight, 1ull, __ATOMIC_RELAXED);\n\treturn ret;\n}\n', 'inflight exit'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern uint64_t houji_fb_sel_hist[4];\n', '\t\t\t\t\textern uint64_t houji_fb_sel_hist[4];\n\t\t\t\t\textern uint64_t houji_fb_inflight, houji_fb_last_ret;\n\t\t\t\t\textern uint32_t houji_fb_last_sel;\n', 'fbx infl extern'),
    ('\t\t\t\t\tprintf("HOUJI FBX open=%llu sel=%llu surf=%llu swap=%llu pres=%llu h=%x:%x %x:%x %x:%x %x:%x\\n",\n\t\t\t\t\t    houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count, hs, hp,\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[0] >> 32), (unsigned)houji_fb_sel_hist[0],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[1] >> 32), (unsigned)houji_fb_sel_hist[1],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[2] >> 32), (unsigned)houji_fb_sel_hist[2],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[3] >> 32), (unsigned)houji_fb_sel_hist[3]);\n', '\t\t\t\t\tprintf("HOUJI FBX open=%llu sel=%llu surf=%llu swap=%llu pres=%llu infl=%llu last=%u:%x h=%x:%x %x:%x\\n",\n\t\t\t\t\t    houji_fb_open_count, houji_fb_sel_count,\n\t\t\t\t\t    houji_surf_create_count, hs, hp,\n\t\t\t\t\t    houji_fb_inflight, houji_fb_last_sel, (unsigned)houji_fb_last_ret,\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[0] >> 32), (unsigned)houji_fb_sel_hist[0],\n\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[1] >> 32), (unsigned)houji_fb_sel_hist[1]);\n', 'fbx infl fields'),
])

# —— 观测：谁在问显示器、IOSurface 服务有没有人碰
patch('iokit/Kernel/arm/SweetFramebuffer.cpp', [
    ('uint32_t houji_fb_last_sel;\n', 'uint32_t houji_fb_last_sel;\nchar houji_fb_last_caller[32];\n', 'fb caller decl'),
    ('\t__atomic_add_fetch(&houji_fb_inflight, 1ull, __ATOMIC_RELAXED);\n', '\t__atomic_add_fetch(&houji_fb_inflight, 1ull, __ATOMIC_RELAXED);\n\tproc_selfname(houji_fb_last_caller, sizeof(houji_fb_last_caller));\n', 'fb caller record'),
])
patch('iokit/Kernel/arm/SweetSurface.cpp', [
    ('uint64_t             houji_surf_create_count;\n', 'uint64_t             houji_surf_create_count;\nuint64_t             houji_surf_sel_count;\nuint32_t             houji_surf_last_sel;\nchar                 houji_surf_last_caller[32];\n', 'surf sel decl'),
    ('\tuint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;\n', '\tuint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;\n\t__atomic_add_fetch(&houji_surf_sel_count, 1ull, __ATOMIC_RELAXED);\n\t__atomic_store_n(&houji_surf_last_sel, selector, __ATOMIC_RELAXED);\n\tproc_selfname(houji_surf_last_caller, sizeof(houji_surf_last_caller));\n', 'surf sel record'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern uint32_t houji_fb_last_sel;\n', '\t\t\t\t\textern uint32_t houji_fb_last_sel;\n\t\t\t\t\textern char houji_fb_last_caller[32];\n\t\t\t\t\textern uint64_t houji_surf_sel_count;\n\t\t\t\t\textern uint32_t houji_surf_last_sel;\n\t\t\t\t\textern char houji_surf_last_caller[32];\n', 'sfx extern'),
    ('\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[1] >> 32), (unsigned)houji_fb_sel_hist[1]);\n', '\t\t\t\t\t    (unsigned)(houji_fb_sel_hist[1] >> 32), (unsigned)houji_fb_sel_hist[1]);\n\t\t\t\t\tprintf("HOUJI SFX fbwho=%s sfcall=%llu sflast=%u sfwho=%s\\n",\n\t\t\t\t\t    houji_fb_last_caller, houji_surf_sel_count,\n\t\t\t\t\t    houji_surf_last_sel, houji_surf_last_caller);\n', 'sfx print'),
])

# —— 观测：backboardd/SpringBoard 打开了哪些 IOKit 服务（最近 6 个类名）
patch('iokit/Kernel/IOService.cpp', [
    ('bool\nIOService::open(   IOService *     forClient,\n', '#if defined(SWEET)\n/* 谁打开了哪些 IOKit 服务：只留最近 6 个类名，采样线里每拍打一遍。 */\nextern "C" void proc_selfname(char *buf, int size);\nuint32_t houji_iok_count;\nuint32_t houji_iok_ring_idx;\nchar     houji_iok_ring[6][28];\n#endif\n\nbool\nIOService::open(   IOService *     forClient,\n', 'iok ring decl'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern char houji_surf_last_caller[32];\n', '\t\t\t\t\textern char houji_surf_last_caller[32];\n\t\t\t\t\textern uint32_t houji_iok_count, houji_iok_ring_idx;\n\t\t\t\t\textern char houji_iok_ring[6][28];\n', 'iok extern'),
    ('\t\t\t\t\tprintf("HOUJI SFX fbwho=%s sfcall=%llu sflast=%u sfwho=%s\\n",\n\t\t\t\t\t    houji_fb_last_caller, houji_surf_sel_count,\n\t\t\t\t\t    houji_surf_last_sel, houji_surf_last_caller);\n', '\t\t\t\t\tprintf("HOUJI SFX fbwho=%s sfcall=%llu sflast=%u sfwho=%s\\n",\n\t\t\t\t\t    houji_fb_last_caller, houji_surf_sel_count,\n\t\t\t\t\t    houji_surf_last_sel, houji_surf_last_caller);\n\t\t\t\t\tprintf("HOUJI IOK n=%u r=%s,%s,%s,%s,%s,%s\\n", houji_iok_count,\n\t\t\t\t\t    houji_iok_ring[0], houji_iok_ring[1], houji_iok_ring[2],\n\t\t\t\t\t    houji_iok_ring[3], houji_iok_ring[4], houji_iok_ring[5]);\n', 'iok print'),
])

# —— 启动期日志回捞：console_write 也进环形缓冲，采样时打出命中的前 12 行
patch('osfmk/console/serial_console.c', [
    ('\tif (houji_console_muted && !panic_active()) {\n\t\treturn;\n\t}\n#endif\n\tassert(str != NULL);', '\tif (houji_console_muted && !panic_active()) {\n\t\treturn;\n\t}\n#endif\n\t{\n\t    extern void houji_debug_append(const char *, int);\n\t    houji_debug_append(str, size);\n\t}\n\tassert(str != NULL);', 'console_write ring'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\tprintf("HOUJI IOK n=%u r=%s,%s,%s,%s,%s,%s\\n", houji_iok_count,\n', '{\n\t\t\t\t\t/* 启动期 log 回捞：命中的前 12 行一直重打，滚动也丢不掉。 */\n\t\t\t\t\textern unsigned houji_debug_copy(uint64_t *pos, char *out, unsigned max);\n\t\t\t\t\textern char *strnstr(const char *, const char *, size_t);\n\t\t\t\t\tstatic uint64_t houji_dbg_pos;\n\t\t\t\t\tstatic char houji_dbg_hits[12][80];\n\t\t\t\t\tstatic unsigned houji_dbg_hitn;\n\t\t\t\t\tchar houji_dbg_buf[1024];\n\t\t\t\t\tfor (int r = 0; r < 6; r++) {\n\t\t\t\t\t    unsigned n = houji_debug_copy(&houji_dbg_pos, houji_dbg_buf, sizeof(houji_dbg_buf) - 1);\n\t\t\t\t\t    if (n == 0) break;\n\t\t\t\t\t    houji_dbg_buf[n] = 0;\n\t\t\t\t\t    char *p = houji_dbg_buf;\n\t\t\t\t\t    while (*p && houji_dbg_hitn < 12) {\n\t\t\t\t\t        char *nl = strchr(p, 10);\n\t\t\t\t\t        if (nl == NULL) break;\n\t\t\t\t\t        *nl = 0;\n\t\t\t\t\t        if (strnstr(p, "backboardd", strlen(p)) || strnstr(p, "SpringBoard", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "IOSurface", strlen(p)) || strnstr(p, "IOMobileFramebuffer", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "CARender", strlen(p)) || strnstr(p, "IOAccelerator", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "error = 138", strlen(p)) || strnstr(p, "error = 159", strlen(p))) {\n\t\t\t\t\t            strlcpy(houji_dbg_hits[houji_dbg_hitn++], p, 80);\n\t\t\t\t\t        }\n\t\t\t\t\t        p = nl + 1;\n\t\t\t\t\t    }\n\t\t\t\t\t}\n\t\t\t\t\tfor (unsigned i = 0; i < houji_dbg_hitn; i++) {\n\t\t\t\t\t    printf("DBG|%s\\n", houji_dbg_hits[i]);\n\t\t\t\t\t}\n\t\t\t\t\t}\n\t\t\t\t\tprintf("HOUJI IOK n=%u r=%s,%s,%s,%s,%s,%s\\n", houji_iok_count,\n', 'boot log scan'),
])

# —— USB 调试通道：记录停在第几道闸 + 关键寄存器快照
patch('bsd/dev/houji_usb_debug.c', [
    ('void houji_usb_debug_start(void)\n{', 'int houji_usb_stage;\nuint32_t houji_usb_obs[8];\nvoid houji_usb_debug_start(void)\n{', 'usb stage decl'),
    ('    printf("HOUJI USB: GCC power=%08x master=%08x cfg=%08x aggre=%08x\\n",power,master,cfg,aggre);', '    printf("HOUJI USB: GCC power=%08x master=%08x cfg=%08x aggre=%08x\\n",power,master,cfg,aggre);\n\thouji_usb_obs[0]=power;houji_usb_obs[1]=master;houji_usb_obs[2]=cfg;houji_usb_obs[3]=aggre;', 'usb obs gcc'),
    ('    printf("HOUJI USB: id=%08x hwparams0=%08x awidth=%u gctl=%08x dctl=%08x dsts=%08x\\n",\n        id,hw,width,rd(GCTL),rd(DCTL),rd(DSTS));', '    printf("HOUJI USB: id=%08x hwparams0=%08x awidth=%u gctl=%08x dctl=%08x dsts=%08x\\n",\n        id,hw,width,rd(GCTL),rd(DCTL),rd(DSTS));\n\thouji_usb_obs[4]=rd(GCTL);houji_usb_obs[5]=rd(DCTL);houji_usb_obs[6]=rd(DSTS);houji_usb_obs[7]=id;', 'usb obs ctl'),
    ('    printf("HOUJI USB: CDC ACM candidate001, RAM log and read-only commands\\n");', '    printf("HOUJI USB: CDC ACM candidate001, RAM log and read-only commands\\n");\n\thouji_usb_stage=1;', 'usb stage 0'),
    ('        printf("HOUJI USB: houji/SM8650 ADT guard refused; no MMIO mapped\\n");return;', '\thouji_usb_stage=2;\n        printf("HOUJI USB: houji/SM8650 ADT guard refused; no MMIO mapped\\n");return;', 'usb stage 1'),
    ('    if (!gcc) return;', '\thouji_usb_stage=3;\n    if (!gcc) return;', 'usb stage 2'),
    ('        printf("HOUJI USB: clocks/power unavailable; controller untouched\\n");return;', '\thouji_usb_stage=4;\n        printf("HOUJI USB: clocks/power unavailable; controller untouched\\n");return;', 'usb stage 3'),
    ('    if (!u.regs || !u.smmu) return;', '\thouji_usb_stage=5;\n    if (!u.regs || !u.smmu) return;', 'usb stage 4'),
    ('    if ((id>>16)!=0x5533 && (id>>16)!=0x3331) return;', '\thouji_usb_stage=6;\n    if ((id>>16)!=0x5533 && (id>>16)!=0x3331) return;', 'usb stage 5'),
    ('        printf("HOUJI USB: device mode/identity guard refused; controller untouched\\n");return;', '\thouji_usb_stage=7;\n        printf("HOUJI USB: device mode/identity guard refused; controller untouched\\n");return;', 'usb stage 6'),
    ('    if (width<36 || width>64) { printf("HOUJI USB: DMA address width guard refused\\n");return; }', '\thouji_usb_stage=8;\n    if (width<36 || width>64) { printf("HOUJI USB: DMA address width guard refused\\n");return; }', 'usb stage 7'),
    ('        printf("HOUJI USB: read-only preflight passed; no DMA allocation or controller writes\\n");return;', '\thouji_usb_stage=9;\n        printf("HOUJI USB: read-only preflight passed; no DMA allocation or controller writes\\n");return;', 'usb stage 8'),
    ('    if (!pn) return;', '\thouji_usb_stage=10;\n    if (!pn) return;', 'usb stage 9'),
    ('    if (!vusb_dma_allowed(u.pa,DMA_SIZE,width)) { printf("HOUJI USB: owned DMA width/range guard refused\\n");return; }', '\thouji_usb_stage=11;\n    if (!vusb_dma_allowed(u.pa,DMA_SIZE,width)) { printf("HOUJI USB: owned DMA width/range guard refused\\n");return; }', 'usb stage 10'),
    ('        if (!page || ptoa_64(page)!=u.pa+off) return;', '\thouji_usb_stage=12;\n        if (!page || ptoa_64(page)!=u.pa+off) return;', 'usb stage 11'),
    ('    if (!qs) return;', '\thouji_usb_stage=13;\n    if (!qs) return;', 'usb stage 12'),
    ('    if (!(rd(DSTS)&HALTED)) { fail("controller halt timeout");return; }', '\thouji_usb_stage=14;\n    if (!(rd(DSTS)&HALTED)) { fail("controller halt timeout");return; }', 'usb stage 13'),
    ('    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }', '\thouji_usb_stage=15;\n    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }', 'usb stage 14'),
    ('    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }', '\thouji_usb_stage=16;\n    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }', 'usb stage 15'),
    ('    wr(DALEPENA,3);setup_arm();if (u.failed) return;', '\thouji_usb_stage=18;\n    wr(DALEPENA,3);setup_arm();if (u.failed) return;', 'usb stage 17'),
    ('    if (rd(DSTS)&HALTED) { fail("controller run timeout");return; }', '\thouji_usb_stage=19;\n    if (rd(DSTS)&HALTED) { fail("controller run timeout");return; }', 'usb stage 18'),
    ('    if (kernel_thread_start(usb_thread,NULL,&thread)!=KERN_SUCCESS) { fail("poll thread creation");wr(DCTL,rd(DCTL)&~RUN);return; }', '\thouji_usb_stage=20;\n    if (kernel_thread_start(usb_thread,NULL,&thread)!=KERN_SUCCESS) { fail("poll thread creation");wr(DCTL,rd(DCTL)&~RUN);return; }', 'usb stage 19'),
    ('    printf("HOUJI USB: running; DMA=%llx; CDC serial HOUJIDBG001; awaiting enumeration\\n",u.pa);', '    printf("HOUJI USB: running; DMA=%llx; CDC serial HOUJIDBG001; awaiting enumeration\\n",u.pa);\n\thouji_usb_stage=21;', 'usb stage 20'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern char houji_surf_last_caller[32];\n', '\t\t\t\t\textern char houji_surf_last_caller[32];\n\t\t\t\t\textern int houji_usb_stage;\n\t\t\t\t\textern uint32_t houji_usb_obs[8];\n', 'usb extern'),
    ('\t\t\t\t\tprintf("HOUJI IOK n=%u r=%s,%s,%s,%s,%s,%s\\n", houji_iok_count,\n', '\t\t\t\t\tprintf("HOUJI USBS st=%d g=%x/%x/%x/%x gctl=%x dsts=%x id=%x\\n", houji_usb_stage,\n\t\t\t\t\t    houji_usb_obs[0], houji_usb_obs[1], houji_usb_obs[2], houji_usb_obs[3],\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7]);\n\t\t\t\t\tprintf("HOUJI IOK n=%u r=%s,%s,%s,%s,%s,%s\\n", houji_iok_count,\n', 'usb print'),
])

patch('bsd/dev/houji_usb_debug.c', [
    ('    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }', '    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }\n\thouji_usb_stage=17;', 'usb stage 17'),
])

# —— USB: ADT 标识 + 可越过该闸；DBG: 16 条 + 崩溃关键字 + 优先 path
patch('bsd/dev/houji_usb_debug.c', [
    ('uint32_t houji_usb_obs[8];', 'uint32_t houji_usb_obs[8];\nchar houji_usb_board[80];', 'usb board decl'),
    ('    attempted=true;u.mps=512;u.coding[0]=0;u.coding[1]=0xc2;u.coding[2]=1;u.coding[6]=8;', '    attempted=true;u.mps=512;u.coding[0]=0;u.coding[1]=0xc2;u.coding[2]=1;u.coding[6]=8;\n    int board_forced = 0;PE_parse_boot_argn("houji_usb_force", &board_forced, sizeof(board_forced));', 'usb board forced arg'),
    ('        !huc_board(compatible,compatible_n,target,target_n)) {', '        (!huc_board(compatible,compatible_n,target,target_n) && board_forced == 0)) {', 'usb board force'),
    ('        printf("HOUJI USB: houji/SM8650 ADT guard refused; no MMIO mapped\\n");return;', '        {\n            unsigned cn = compatible_n > 55 ? 55 : compatible_n;\n            unsigned tn = target_n > 15 ? 15 : target_n;\n            const char *c = (const char *)compatible;\n            const char *t = (const char *)target;\n            unsigned o = 0;\n            for (unsigned i = 0; i < cn && c; i++) {\n                char ch = c[i];\n                houji_usb_board[o++] = (ch >= 32 && ch < 127) ? ch : 46;\n            }\n            houji_usb_board[o++] = 124;\n            for (unsigned i = 0; i < tn && t && o < 78; i++) {\n                char ch = t[i];\n                houji_usb_board[o++] = (ch >= 32 && ch < 127) ? ch : 46;\n            }\n            houji_usb_board[o] = 0;\n        }\n        printf("HOUJI USB: houji/SM8650 ADT guard refused; no MMIO mapped\\n");return;', 'usb board capture'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern uint32_t houji_usb_obs[8];', '\t\t\t\t\textern uint32_t houji_usb_obs[8];\n\t\t\t\t\textern char houji_usb_board[80];', 'usb board extern'),
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d g=%x/%x/%x/%x gctl=%x dsts=%x id=%x\\n", houji_usb_stage,\n\t\t\t\t\t    houji_usb_obs[0], houji_usb_obs[1], houji_usb_obs[2], houji_usb_obs[3],\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7]);', '\t\t\t\t\tprintf("HOUJI USBS st=%d g=%x/%x/%x/%x gctl=%x dsts=%x id=%x bd=%s\\n", houji_usb_stage,\n\t\t\t\t\t    houji_usb_obs[0], houji_usb_obs[1], houji_usb_obs[2], houji_usb_obs[3],\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', 'usb board print'),
    ('\t\t\t\t\tstatic char houji_dbg_hits[12][80];', '\t\t\t\t\tstatic char houji_dbg_hits[16][80];', 'dbg hits 16'),
    ('\t\t\t\t\t    while (*p && houji_dbg_hitn < 12) {', '\t\t\t\t\t    while (*p && houji_dbg_hitn < 16) {', 'dbg limit 16'),
    ('\t\t\t\t\t            strnstr(p, "error = 138", strlen(p)) || strnstr(p, "error = 159", strlen(p))) {', '\t\t\t\t\t            strnstr(p, "error = 138", strlen(p)) || strnstr(p, "error = 159", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "Library not loaded", strlen(p)) || strnstr(p, "image not found", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "Symbol not found", strlen(p)) || strnstr(p, "abort", strlen(p)) ||\n\t\t\t\t\t            strnstr(p, "Exception", strlen(p)) || strnstr(p, "failed", strlen(p))) {', 'dbg keywords'),
    ('\t\t\t\t\t            strlcpy(houji_dbg_hits[houji_dbg_hitn++], p, 80);', '\t\t\t\t\t            char *keep = strnstr(p, "path = ", strlen(p));\n\t\t\t\t\t            strlcpy(houji_dbg_hits[houji_dbg_hitn++], keep ? keep : p, 80);', 'dbg path'),
])

# —— ADT 判定顺序化 + /chosen display-scale 观测 + DBG 取 .plist 文件名
patch('bsd/dev/houji_usb_debug.c', [
    ('    if (SecureDTLookupEntry(NULL,"/",&root)!=kSuccess ||\n        SecureDTGetProperty(root,"compatible",&compatible,&compatible_n)!=kSuccess ||\n        SecureDTGetProperty(root,"target-type",&target,&target_n)!=kSuccess ||\n        (!huc_board(compatible,compatible_n,target,target_n) && board_forced == 0)) {\n', '    dt_ok = (SecureDTLookupEntry(NULL,"/",&root)==kSuccess);\n    if (dt_ok) { dt_ok = (SecureDTGetProperty(root,"compatible",&compatible,&compatible_n)==kSuccess); if (!dt_ok) houji_dt_fail=2; }\n    if (dt_ok) { dt_ok = (SecureDTGetProperty(root,"target-type",&target,&target_n)==kSuccess); if (!dt_ok) houji_dt_fail=3; }\n    if (dt_ok) { dt_ok = huc_board(compatible,compatible_n,target,target_n); if (!dt_ok) houji_dt_fail=4; }\n    if (!dt_ok && board_forced == 0) {\n', 'usb dt sequence'),
    ('char houji_usb_board[80];', 'char houji_usb_board[80];\nint houji_dt_fail;\nuint32_t houji_dt_scale, houji_dt_density;\nint houji_dt_product;', 'usb dt globals'),
    ('    int board_forced = 0;PE_parse_boot_argn("houji_usb_force", &board_forced, sizeof(board_forced));', '    bool dt_ok = false;\n    int board_forced = 0;PE_parse_boot_argn("houji_usb_force", &board_forced, sizeof(board_forced));', 'usb dt ok decl'),
    ('    printf("HOUJI USB: CDC ACM candidate001, RAM log and read-only commands\\n");', '    {   /* /chosen 显示参数与 /product 节点：无 GPU 的 CA 渲染器要这两个 */\n        DTEntry e;\n        void const *v = NULL;\n        unsigned vl = 0;\n        if (SecureDTLookupEntry(NULL, "/chosen", &e) == kSuccess) {\n            if (SecureDTGetProperty(e, "display-scale", &v, &vl) == kSuccess) {\n                if (vl >= 4) houji_dt_scale = *(const uint32_t *)v;\n                else if (vl >= 1) houji_dt_scale = (uint32_t)((const char *)v)[0] - 48;\n            }\n            v = NULL; vl = 0;\n            if (SecureDTGetProperty(e, "display-density", &v, &vl) == kSuccess && vl >= 4) {\n                houji_dt_density = *(const uint32_t *)v;\n            }\n        }\n        if (SecureDTLookupEntry(NULL, "/product", &e) == kSuccess) houji_dt_product = 1;\n    }\n    printf("HOUJI USB: CDC ACM candidate001, RAM log and read-only commands\\n");', 'usb dt read'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern char houji_usb_board[80];', '\t\t\t\t\textern char houji_usb_board[80];\n\t\t\t\t\textern int houji_dt_fail, houji_dt_product;\n\t\t\t\t\textern uint32_t houji_dt_scale, houji_dt_density;', 'usb dt extern'),
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d g=%x/%x/%x/%x gctl=%x dsts=%x id=%x bd=%s\\n", houji_usb_stage,\n\t\t\t\t\t    houji_usb_obs[0], houji_usb_obs[1], houji_usb_obs[2], houji_usb_obs[3],\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', '\t\t\t\t\tprintf("HOUJI USBS st=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_dt_fail, houji_dt_scale, houji_dt_density, houji_dt_product,\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', 'usb dt print'),
    ('\t\t\t\t\t            strlcpy(houji_dbg_hits[houji_dbg_hitn++], keep ? keep : p, 80);', '\t\t\t\t\t            if (keep) {\n\t\t\t\t\t                char *dot = strnstr(keep, ".plist", strlen(keep));\n\t\t\t\t\t                unsigned len = dot ? (unsigned)(dot - keep) + 6 : 70;\n\t\t\t\t\t                if (len > 74) len = 74;\n\t\t\t\t\t                for (unsigned q = 0; q < len; q++) houji_dbg_tail[q] = keep[q];\n\t\t\t\t\t                houji_dbg_tail[len] = 0;\n\t\t\t\t\t                strlcpy(houji_dbg_hits[houji_dbg_hitn++], houji_dbg_tail, 80);\n\t\t\t\t\t            } else {\n\t\t\t\t\t                strlcpy(houji_dbg_hits[houji_dbg_hitn++], p, 80);\n\t\t\t\t\t            }', 'dbg plist tail'),
    ('\t\t\t\t\tstatic char houji_dbg_hits[16][80];', '\t\t\t\t\tstatic char houji_dbg_hits[16][80];\n\t\t\t\t\tstatic char houji_dbg_tail[80];', 'dbg tail buf'),
])

# —— USB：ADT/SMMU 两道猜测性硬门改为只记录，让控制器真正启动
patch('bsd/dev/houji_usb_debug.c', [
    ('    if (!dt_ok && board_forced == 0) {\n\thouji_usb_stage=2;\n', '    if (!dt_ok) {\n', 'gate step1'),
    ('        printf("HOUJI USB: houji/SM8650 ADT guard refused; no MMIO mapped\\n");return;\n', '        printf("HOUJI USB: ADT strings not matched (dtf=%d); proceeding anyway\\n", houji_dt_fail);\n', 'gate step2'),
    ('    int board_forced = 0;PE_parse_boot_argn("houji_usb_force", &board_forced, sizeof(board_forced));\n', '', 'drop board_forced'),
    ('    if (dt_ok) { dt_ok = huc_board(compatible,compatible_n,target,target_n); if (!dt_ok) houji_dt_fail=4; }\n', '    if (dt_ok) { dt_ok = huc_board(compatible,compatible_n,target,target_n); if (!dt_ok) houji_dt_fail=4; }\n    if (!dt_ok && !houji_dt_fail) houji_dt_fail = 5;\n', 'dtf 5'),
    ('    if (((rd(GCTL)>>12)&3)!=2 || !identity()) {\n\thouji_usb_stage=7;\n        printf("HOUJI USB: device mode/identity guard refused; controller untouched\\n");return;\n    }\n', '    int ident = identity();\n    if (((rd(GCTL)>>12)&3)!=2) {\n\thouji_usb_stage=7;\n        printf("HOUJI USB: controller not in device mode (gctl=%08x); untouched\\n", rd(GCTL));return;\n    }\n    if (!ident) printf("HOUJI USB: SMMU identity mismatch; proceeding anyway\\n");\n', 'identity observe'),
])

# —— USB: CurMod 只记录、复位后强制切 device、地址宽度归一化
patch('bsd/dev/houji_usb_debug.c', [
    ('    int ident = identity();\n    if (((rd(GCTL)>>12)&3)!=2) {\n\thouji_usb_stage=7;\n        printf("HOUJI USB: controller not in device mode (gctl=%08x); untouched\\n", rd(GCTL));return;\n    }\n    if (!ident) printf("HOUJI USB: SMMU identity mismatch; proceeding anyway\\n");\n', '    int ident = identity();\n    houji_usb_mod = (rd(GCTL)>>12)&3;\n    if (houji_usb_mod != 2) {\n\thouji_usb_stage=7;\n        printf("HOUJI USB: gctl mod=%d (want 2); switch after reset\\n", houji_usb_mod);\n    }\n    if (!ident) printf("HOUJI USB: SMMU identity mismatch; proceeding anyway\\n");\n', 'mod observe'),
    ('    if (width<36 || width>64) { printf("HOUJI USB: DMA address width guard refused\\n");return; }', '    if (width<32 || width>64) width=48;   /* 该字段在 SM8650 上不一定代表地址宽度 */', 'width normalize'),
    ('    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }\n', '    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }\n    wr(GCTL,(rd(GCTL)&~(3u<<12))|(2u<<12));\n    delay(10);\n    houji_usb_mod=(rd(GCTL)>>12)&3;\n    if (houji_usb_mod!=2) { fail("device mode switch");return; }\n', 'force device mode'),
])
patch('bsd/dev/houji_usb_debug.c', [
    ('int houji_usb_stage;', 'int houji_usb_stage;\nint houji_usb_mod;', 'mod global'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern char houji_usb_board[80];', '\t\t\t\t\textern char houji_usb_board[80];\n\t\t\t\t\textern int houji_usb_mod;', 'mod extern'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_dt_fail, houji_dt_scale, houji_dt_density, houji_dt_product,\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', '\t\t\t\t\tprintf("HOUJI USBS st=%d md=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_usb_mod, houji_dt_fail, houji_dt_scale, houji_dt_density, houji_dt_product,\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', 'mod print'),
])

patch('bsd/dev/houji_usb_debug.c', [
    ('/* First stop any old device DMA; soft reset must finish before replacing\n     * descriptors. PHY configuration and global bus settings remain from ABL. */\n    wr(DEVTEN,0);wr(DCTL,rd(DCTL)&~RUN);\n    unsigned i;\n    for (i=0;i<500 && !(rd(DSTS)&HALTED);i++) delay(1000);\n\thouji_usb_stage=14;\n    if (!(rd(DSTS)&HALTED)) { fail("controller halt timeout");return; }\n    wr(DCTL,RESET);\n    for (i=0;i<500 && (rd(DCTL)&RESET);i++) delay(1000);\n\thouji_usb_stage=15;\n    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }\n    wr(GCTL,(rd(GCTL)&~(3u<<12))|(2u<<12));\n    delay(10);\n    houji_usb_mod=(rd(GCTL)>>12)&3;\n    if (houji_usb_mod!=2) { fail("device mode switch");return; }\n    sync_dma(0,DMA_SIZE);\n    wr(GEVNTADRLO,(uint32_t)u.pa);wr(GEVNTADRHI,(uint32_t)(u.pa>>32));\n\thouji_usb_stage=16;\n    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }\n\thouji_usb_stage=17;\n    wr(GEVNTSIZ,(1u<<31)|EVENT_SIZE);wr(GEVNTCOUNT,rd(GEVNTCOUNT)&0xffff);\n    wr(DCFG,rd(DCFG)&~(7u|(127u<<3))); /* high speed only */\n    if (epcmd(0,START_CONFIG,0,0,0) ||\n        epcmd(0,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10),0) || epcmd(0,SET_RESOURCE,1,0,0) ||\n        epcmd(1,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10)|(1u<<25),0) || epcmd(1,SET_RESOURCE,1,0,0)) return;\n\thouji_usb_stage=18;\n    wr(DALEPENA,3);setup_arm();if (u.failed) return;\n    uint32_t hs=read_reg(qs,0x10),ss=read_reg(qs,0x30);\n    *(volatile uint32_t *)(qs+0x10)=hs|(1u<<20)|(1u<<28);\n    *(volatile uint32_t *)(qs+0x30)=ss|(1u<<24);\n    __asm__ volatile("dsb sy" ::: "memory");\n    wr(DEVTEN,7|(1u<<9)|(1u<<11));wr(DCTL,rd(DCTL)|RUN);\n    for (i=0;i<500 && (rd(DSTS)&HALTED);i++) delay(1000);\n\thouji_usb_stage=19;\n    if (rd(DSTS)&HALTED) { fail("controller run timeout");return; }\n    thread_t thread;\n\thouji_usb_stage=20;\n    if (kernel_thread_start(usb_thread,NULL,&thread)!=KERN_SUCCESS) { fail("poll thread creation");wr(DCTL,rd(DCTL)&~RUN);return; }\n    thread_deallocate(thread);', '/* First stop any old device DMA; soft reset must finish before replacing\n     * descriptors. PHY configuration and global bus settings remain from ABL.\n     * 每步前打面包屑，并可用 houji_usb_depth=N 只走到第 N 步，\n     * 卡住时屏幕上最后一行就是出事的操作。 */\n    int depth=99;PE_parse_boot_argn("houji_usb_depth",&depth,sizeof(depth));\n    printf("HUSB a depth=%d pa=%llx\\n",depth,u.pa);\n    if (depth<1) return;\n    wr(DEVTEN,0);wr(DCTL,rd(DCTL)&~RUN);\n    unsigned i;\n    for (i=0;i<500 && !(rd(DSTS)&HALTED);i++) delay(1000);\n\thouji_usb_stage=14;\n    if (!(rd(DSTS)&HALTED)) { fail("controller halt timeout");return; }\n    printf("HUSB b halted dsts=%x\\n",rd(DSTS));\n    if (depth<2) return;\n    wr(DCTL,RESET);\n    for (i=0;i<500 && (rd(DCTL)&RESET);i++) delay(1000);\n\thouji_usb_stage=15;\n    if (rd(DCTL)&RESET) { fail("controller reset timeout");return; }\n    printf("HUSB c dctl=%x gctl=%x\\n",rd(DCTL),rd(GCTL));\n    if (depth<3) return;\n    wr(GCTL,(rd(GCTL)&~(3u<<12))|(2u<<12));\n    delay(10);\n    houji_usb_mod=(rd(GCTL)>>12)&3;\n    printf("HUSB d mod=%d gctl=%x\\n",houji_usb_mod,rd(GCTL));\n    if (houji_usb_mod!=2) { fail("device mode switch");return; }\n    if (depth<4) return;\n    sync_dma(0,DMA_SIZE);\n    wr(GEVNTADRLO,(uint32_t)u.pa);wr(GEVNTADRHI,(uint32_t)(u.pa>>32));\n\thouji_usb_stage=16;\n    if (rd(GEVNTADRHI)!=(uint32_t)(u.pa>>32)) { fail("event address readback");return; }\n    printf("HUSB e evt=%x\\n",rd(GEVNTADRHI));\n    if (depth<5) return;\n\thouji_usb_stage=17;\n    wr(GEVNTSIZ,(1u<<31)|EVENT_SIZE);wr(GEVNTCOUNT,rd(GEVNTCOUNT)&0xffff);\n    wr(DCFG,rd(DCFG)&~(7u|(127u<<3))); /* high speed only */\n    printf("HUSB f dcfg=%x\\n",rd(DCFG));\n    if (depth<6) return;\n    if (epcmd(0,START_CONFIG,0,0,0) ||\n        epcmd(0,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10),0) || epcmd(0,SET_RESOURCE,1,0,0) ||\n        epcmd(1,SET_CONFIG,64u<<3,(1u<<8)|(1u<<10)|(1u<<25),0) || epcmd(1,SET_RESOURCE,1,0,0)) return;\n    printf("HUSB g epcmd ok\\n");\n    if (depth<7) return;\n\thouji_usb_stage=18;\n    wr(DALEPENA,3);setup_arm();if (u.failed) return;\n    printf("HUSB h armed\\n");\n    if (depth<8) return;\n    uint32_t hs=read_reg(qs,0x10),ss=read_reg(qs,0x30);\n    *(volatile uint32_t *)(qs+0x10)=hs|(1u<<20)|(1u<<28);\n    *(volatile uint32_t *)(qs+0x30)=ss|(1u<<24);\n    __asm__ volatile("dsb sy" ::: "memory");\n    printf("HUSB i qs=%x/%x\\n",hs,ss);\n    if (depth<9) return;\n    wr(DEVTEN,7|(1u<<9)|(1u<<11));wr(DCTL,rd(DCTL)|RUN);\n    for (i=0;i<500 && (rd(DSTS)&HALTED);i++) delay(1000);\n\thouji_usb_stage=19;\n    if (rd(DSTS)&HALTED) { fail("controller run timeout");return; }\n    printf("HUSB j running dsts=%x\\n",rd(DSTS));\n    thread_t thread;\n\thouji_usb_stage=20;\n    if (kernel_thread_start(usb_thread,NULL,&thread)!=KERN_SUCCESS) { fail("poll thread creation");wr(DCTL,rd(DCTL)&~RUN);return; }\n    thread_deallocate(thread);', 'usb depth breadcrumbs'),
])

# —— USB 面包屑常驻采样行（mk= 最后到达的步骤）
patch('bsd/dev/houji_usb_debug.c', [
    ('int houji_usb_mod;', 'int houji_usb_mod;\nchar houji_usb_mark[40];\nstatic void mark(const char *s)\n{\n    unsigned i = 0;\n    while (s[i] && i < 39) { houji_usb_mark[i] = s[i]; i++; }\n    houji_usb_mark[i] = 0;\n}', 'mark fn'),
    ('    printf("HUSB a', '    mark("a");printf("HUSB a', 'mark a'),
    ('    printf("HUSB b', '    mark("b");printf("HUSB b', 'mark b'),
    ('    printf("HUSB c', '    mark("c");printf("HUSB c', 'mark c'),
    ('    printf("HUSB d', '    mark("d");printf("HUSB d', 'mark d'),
    ('    printf("HUSB e', '    mark("e");printf("HUSB e', 'mark e'),
    ('    printf("HUSB f', '    mark("f");printf("HUSB f', 'mark f'),
    ('    printf("HUSB g', '    mark("g");printf("HUSB g', 'mark g'),
    ('    printf("HUSB h', '    mark("h");printf("HUSB h', 'mark h'),
    ('    printf("HUSB i', '    mark("i");printf("HUSB i', 'mark i'),
    ('    printf("HUSB j', '    mark("j");printf("HUSB j', 'mark j'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern int houji_usb_mod;', '\t\t\t\t\textern int houji_usb_mod;\n\t\t\t\t\textern char houji_usb_mark[40];', 'mark extern'),
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d md=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_usb_mod, houji_dt_fail, houji_dt_scale, houji_dt_density, houji_dt_product,\n\t\t\t\t\t    houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', '\t\t\t\t\tprintf("HOUJI USBS st=%d mk=%s md=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_usb_mark, houji_usb_mod, houji_dt_fail, houji_dt_scale, houji_dt_density,\n\t\t\t\t\t    houji_dt_product, houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', 'mark print'),
])

# —— 采样行加 dp=内核实际读到的 houji_usb_depth
patch('bsd/dev/houji_usb_debug.c', [
    ('    int depth=99;PE_parse_boot_argn("houji_usb_depth",&depth,sizeof(depth));', '    int depth=99;PE_parse_boot_argn("houji_usb_depth",&depth,sizeof(depth));\n    houji_usb_depthv=depth;', 'depth global use'),
    ('char houji_usb_mark[40];', 'char houji_usb_mark[40];\nint houji_usb_depthv;', 'depth global decl'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\textern char houji_usb_mark[40];', '\t\t\t\t\textern char houji_usb_mark[40];\n\t\t\t\t\textern int houji_usb_depthv;', 'depth extern'),
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d mk=%s md=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_usb_mark, houji_usb_mod, houji_dt_fail, houji_dt_scale, houji_dt_density,\n\t\t\t\t\t    houji_dt_product, houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', '\t\t\t\t\tprintf("HOUJI USBS st=%d mk=%s dp=%d md=%d dtf=%d sc=%u dn=%u prod=%d gctl=%x dsts=%x id=%x bd=%s\\n",\n\t\t\t\t\t    houji_usb_stage, houji_usb_mark, houji_usb_depthv, houji_usb_mod, houji_dt_fail, houji_dt_scale,\n\t\t\t\t\t    houji_dt_density, houji_dt_product, houji_usb_obs[4], houji_usb_obs[6], houji_usb_obs[7], houji_usb_board);', 'depth print'),
])

patch('bsd/dev/houji_usb_debug.c', [
    ('    uint32_t hs=read_reg(qs,0x10),ss=read_reg(qs,0x30);\n    *(volatile uint32_t *)(qs+0x10)=hs|(1u<<20)|(1u<<28);\n    *(volatile uint32_t *)(qs+0x30)=ss|(1u<<24);\n    __asm__ volatile("dsb sy" ::: "memory");\n    mark("i");printf("HUSB i qs=%x/%x\\n",hs,ss);\n', '    /* QSCRATCH 地址在 SM8650 上未证实，写错会挂死总线；PHY 已由 ABL 配好 */\n    mark("i");printf("HUSB i qs=%p (skipped)\\n", (void *)qs);\n', 'drop qs writes'),
])
patch('bsd/dev/sweet_sample.c', [
    ('extern void sweet_sample_start(void);\n', 'extern void sweet_sample_start(void);\n\n/* ---- houji disk log: dump ring to root device tail, read back with dd ----\n * 没有串口时把内核日志持久化到磁盘固定位置，重启后 dd 取回，无需拍照。\n * 位置：(devsize - 8MB) 起 256KB，内容以 "HOUJILOG1\\n" 开头。\n * ponytail: 直接写裸块，绕过 HFS；只占卷尾 256KB。\n */\n#include <sys/buf.h>\n#include <sys/vnode.h>\n#include <sys/vnode_internal.h>\n#include <sys/mount.h>\n#include <sys/mount_internal.h>\n#include <miscfs/specfs/specdev.h>\nextern vnode_t rootvnode;\nextern unsigned houji_debug_copy(uint64_t *pos, char *out, unsigned max);\nextern uint64_t houji_debug_total(void);\nunsigned long long houji_disk_dsz;\nunsigned houji_disk_w;\nint houji_disk_e;\nstatic void houji_disk_dump(void)\n{\n\tmount_t mp;\n\tvnode_t dev;\n\tunsigned long long dsz, off, pos, total;\n\tbuf_t bp;\n\tunsigned i, n;\n\tchar *dp;\n\tif (rootvnode == NULLVP)\n\t\treturn;\n\tmp = vnode_mount(rootvnode);\n\tif (mp == NULL || mp->mnt_devvp == NULLVP)\n\t\treturn;\n\tdev = mp->mnt_devvp;\n\tif (dev->v_specinfo == NULL)\n\t\treturn;\n\tdsz = (unsigned long long)dev->v_specinfo->si_devsize;\n\thouji_disk_dsz = dsz;\n\tif (dsz < (64ull << 20))\n\t\treturn;\n\ttotal = houji_debug_total();\n\tpos = total > 262144ull ? total - 262144ull : 0;\n\toff = (dsz - (8ull << 20)) & ~(unsigned long long)511;\n\tfor (i = 0; i < 64; i++) {\n\t\tbp = buf_getblk(dev, (daddr64_t)((off >> 9) + i), 4096, 0, 0, BLK_WRITE);\n\t\tif (bp == NULL) {\n\t\t\thouji_disk_e = -1;\n\t\t\treturn;\n\t\t}\n\t\tdp = (char *)(uintptr_t)buf_dataptr(bp);\n\t\tbzero(dp, 4096);\n\t\tif (i == 0) {\n\t\t\tmemcpy(dp, "HOUJILOG1\\n", 10);\n\t\t\tn = houji_debug_copy(&pos, dp + 10, 4086);\n\t\t} else {\n\t\t\tn = houji_debug_copy(&pos, dp, 4096);\n\t\t}\n\t\tbuf_markaged(bp);\n\t\thouji_disk_e = (int)buf_bwrite(bp);\n\t\tif (houji_disk_e)\n\t\t\treturn;\n\t\thouji_disk_w++;\n\t\tif (n == 0 && i > 0)\n\t\t\treturn;\n\t}\n}\n', 'disk log func'),
])
patch('bsd/dev/sweet_sample.c', [
    ('\t\t\t\t\tprintf("HOUJI USBS st=%d', '                    if ((i % 3u) == 0u) houji_disk_dump();\n                    printf("HOUJI DISK dsz=%llu p=%u e=%d\\n", houji_disk_dsz, houji_disk_w, houji_disk_e);\n\t\t\t\t\tprintf("HOUJI USBS st=%d', 'disk log call'),
])
patch('bsd/dev/houji_usb_debug.c', [
    ('    int depth=99;PE_parse_boot_argn("houji_usb_depth",&depth,sizeof(depth));\n    houji_usb_depthv=depth;\n', '    int depth=99;PE_parse_boot_argn("houji_usb_depth",&depth,sizeof(depth));\n    /* 采样线程可以抬高这个值再调一次本函数，逐级试探 */\n    if (depth < houji_usb_depthv) depth = houji_usb_depthv;\n    houji_usb_depthv=depth;\n', 'depth monotonic'),
])
patch('bsd/dev/sweet_sample.c', [
    ('unsigned long long houji_disk_dsz;\n', 'extern int houji_usb_depthv;\nextern void houji_usb_debug_start(void);\nunsigned long long houji_disk_dsz;\n', 'usb externs'),
    ('                    if ((i % 3u) == 0u) houji_disk_dump();\n', '                    /* 一次开机把 depth 从 4 爬到 12，每步之后立刻落盘 */\n                    if (i >= 1u && i <= 9u) {\n                        houji_usb_depthv = 3 + (int)i;\n                        houji_usb_debug_start();\n                    }\n                    houji_disk_dump();\n', 'depth ladder'),
])
patch('bsd/dev/sweet_sample.c', [
    ('unsigned long long houji_disk_dsz;\n', 'unsigned long long houji_disk_dsz;\nstatic int houji_logecho;\nstatic int houji_echo_done;\nstatic unsigned houji_keep_n;\nstatic char houji_keep[28][88];\n', 'logecho globals'),
    ('static void houji_disk_dump(void)\n', 'static int houji_disk_echolog(void)\n{\n\tmount_t mp;\n\tvnode_t dev;\n\tunsigned long long dsz, off;\n\tbuf_t bp;\n\tunsigned i, k;\n\tsize_t rem;\n\tchar *dp, *p, *nl;\n\tif (rootvnode == NULLVP)\n\t\treturn 0;\n\tmp = vnode_mount(rootvnode);\n\tif (mp == NULL || mp->mnt_devvp == NULLVP)\n\t\treturn 0;\n\tdev = mp->mnt_devvp;\n\tif (dev->v_specinfo == NULL)\n\t\treturn 0;\n\tdsz = (unsigned long long)dev->v_specinfo->si_devsize;\n\tif (dsz < (64ull << 20))\n\t\treturn 0;\n\toff = (dsz - (8ull << 20)) & ~(unsigned long long)511;\n\tbp = buf_getblk(dev, (daddr64_t)(off >> 9), 4096, 0, 0, BLK_READ);\n\tif (bp == NULL) {\n\t\tprintf("ECHO nobuf\\n");\n\t\treturn 0;\n\t}\n\tdp = (char *)(uintptr_t)buf_dataptr(bp);\n\tif (memcmp(dp, "HOUJILOG1\\n", 10) != 0) {\n\t\tbuf_brelse(bp);\n\t\tprintf("ECHO nomagic dsz=%llu\\n", dsz);\n\t\treturn 0;\n\t}\n\tbuf_brelse(bp);\n\tfor (i = 0; i < 64; i++) {\n\t\tbp = buf_getblk(dev, (daddr64_t)((off >> 9) + i), 4096, 0, 0, BLK_READ);\n\t\tif (bp == NULL)\n\t\t\tbreak;\n\t\tdp = (char *)(uintptr_t)buf_dataptr(bp);\n\t\tfor (p = dp; p < dp + 4096; p = nl + 1) {\n\t\t\trem = (size_t)(dp + 4096 - p);\n\t\t\tnl = (char *)memchr(p, 10, rem);\n\t\t\tif (nl == NULL)\n\t\t\t\tbreak;\n\t\t\t*nl = 0;\n\t\t\tif (strnstr(p, "HOUJI USBS", rem) || strnstr(p, "HUSB ", rem) ||\n\t\t\t    strnstr(p, "HOUJI IOK", rem) || strnstr(p, "HOUJI DISPLAY", rem) ||\n\t\t\t    strnstr(p, "HOUJI FB DBG", rem) || strnstr(p, "HOUJI DISK", rem)) {\n\t\t\t\tif (houji_keep_n < 28) {\n\t\t\t\t\tstrlcpy(houji_keep[houji_keep_n], p, 88);\n\t\t\t\t\thouji_keep_n++;\n\t\t\t\t} else {\n\t\t\t\t\tfor (k = 0; k + 1 < 28; k++)\n\t\t\t\t\t\tmemcpy(houji_keep[k], houji_keep[k + 1], 88);\n\t\t\t\t\tstrlcpy(houji_keep[27], p, 88);\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t\tbuf_brelse(bp);\n\t}\n\tprintf("ECHO READ dsz=%llu n=%u\\n", dsz, houji_keep_n);\n\tfor (k = 0; k < houji_keep_n; k++)\n\t\tprintf("ECHO|%s\\n", houji_keep[k]);\n\tprintf("ECHO END\\n");\n\treturn 1;\n}\n\nstatic void houji_disk_dump(void)\n', 'logecho func'),
    ('\tPE_parse_boot_argn("houji_userland_trial", &trial, sizeof(trial));\n', '\tPE_parse_boot_argn("houji_userland_trial", &trial, sizeof(trial));\n\tPE_parse_boot_argn("houji_logecho", &houji_logecho, sizeof(houji_logecho));\n', 'logecho arg'),
    ('\t\t\t(void)tsleep(&chan, PZERO, "houjiusr", 5 * hz);\n', '\t\t\t(void)tsleep(&chan, PZERO, "houjiusr", 5 * hz);\n\t\t\tif (houji_logecho && !houji_echo_done && i >= 2u) {\n\t\t\t\tif (houji_disk_echolog() == 1) {\n\t\t\t\t\thouji_echo_done = 1;\n\t\t\t\t\tfor (;;) (void)tsleep(&chan, PZERO, "houjiecho", 1000 * hz);\n\t\t\t\t}\n\t\t\t}\n', 'logecho loop'),
    ('                    if (i >= 1u && i <= 9u) {\n', '                    if (!houji_logecho && i >= 1u && i <= 9u) {\n', 'logecho gate ladder'),
    ('                    houji_disk_dump();\n', '                    if (!houji_logecho) houji_disk_dump();\n', 'logecho gate dump'),
])

print('PATCHES_OK')
PY
[ $? -eq 0 ] || die "补丁失败"

say "5/6 编译（M4 约 20-40 分钟，日志实时写入 $LOG ）"
export HOUJI_XNU="$W/.deps/kernel"
export HOUJI_BUILD_ROOT="$W/out/kernel"
export HOUJI_JOBS="$(sysctl -n hw.ncpu)"
find "$HOUJI_BUILD_ROOT" -name 'SweetFramebuffer*.cpo' -delete 2>/dev/null
find "$HOUJI_BUILD_ROOT" -name 'subr_log*.cpo' -delete 2>/dev/null
find "$HOUJI_BUILD_ROOT" -name 'sweet_sample*.cpo' -delete 2>/dev/null
find "$HOUJI_BUILD_ROOT" -name 'kern_persona*.cpo' -delete 2>/dev/null
find "$HOUJI_BUILD_ROOT" -name 'serial_console*.cpo' -delete 2>/dev/null
find "$HOUJI_BUILD_ROOT" -name 'IOService*.cpo' -delete 2>/dev/null
cd "$W" || exit 1
START=$(date +%s)
bash tools/build-kernel.sh
RC=$?
echo "构建耗时 $(($(date +%s)-START)) 秒，退出码 $RC"
[ "$RC" = 0 ] || die "构建失败"

say "6/6 校验产物"
K=$(find "$W/out" -type f -size +5M \( -name 'kernel*' -o -name 'mach_kernel' -o -name '*.macho' \) 2>/dev/null | head -1)
[ -n "$K" ] || die "找不到产物"
echo "产物: $K"
for s in 'newUserClient type=%u ok' 'oslog: LOGBUFFERMAP ret=%d' 'SYSTEM_PROXY missing, used SYSTEM' 'HOUJI FB DIAG: poll=%d' 'userclient trace' 'houji: persona type %u created' 'HOUJI PERSONA: calls=%u created=%u' 'HOUJI SPAWN FAIL %s err=%d' 'x16 %llu x0 0x%llx' 'Firehose forced on' 'HOUJI OSLOG: firehose stub' 'persona proxy uid501' 'HOUJI MKDIR %s' 'HOUJI HFS: dirty bit cleared' 'HOUJI DIRS: %d' 'HOUJI P%02d %s' 'HOUJI KEYS1' 'size=%llu persona' 'houji: persona %s uid=%u' 'houji_persona_bootstrap' 'HOUJI FBX'; do
  echo "  命中[$s] = $(LC_ALL=C grep -c "$s" "$K" || true)"
done
shasum -a 256 "$K"; ls -l "$K"

cat <<EOF

把上面这个内核文件用 ToDesk 传回 Windows，放到：
  D:\\houji-port\\work\\payload\\kernel.new.macho
然后跟我说一句「传好了」。
EOF
