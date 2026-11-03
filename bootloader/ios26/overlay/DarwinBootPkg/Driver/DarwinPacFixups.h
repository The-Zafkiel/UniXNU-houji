/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Standalone arm64e kernel fixups. The modifier and format-7 decoding follow
 * hakios-arm64e-dada f1ae906f, base/xnuloader/loader.c. Validate every chain
 * before writing; read raw pointers from the immutable file, never a chain
 * that an earlier write has already changed. No allocation or hardware I/O.
 */
#ifndef DARWIN_PAC_FIXUPS_H
#define DARWIN_PAC_FIXUPS_H
typedef unsigned char DpaU8;
typedef unsigned short DpaU16;
typedef unsigned int DpaU32;
typedef unsigned long long DpaU64;
#define DPA_CONFIG_MAGIC 0x3147464341505548ULL /* HUPACFG1 */
#define DPA_TCR_MASK ((0x3fULL << 16) | (1ULL << 38) | (1ULL << 52))
#define DPA_MAX_SEGMENTS 64
#define DPA_MAX_FIXUPS 2097152U
enum { DPA_OK, DPA_IMAGE, DPA_COMMAND, DPA_RANGE, DPA_KEYS, DPA_CONFIG,
       DPA_FIXUPS, DPA_IMPORT, DPA_FORMAT, DPA_CHAIN, DPA_TARGET, DPA_SIGN };
typedef struct {
  DpaU64 Va, Size, File, Bytes;
} DpaSegment;
typedef struct {
  DpaU64 ImageBase, KeysOffset, TcrBits;
  DpaU32 Authenticated, Rebased, Segments;
} DpaReport;
typedef int (*DpaSigner)(void *, DpaU64, DpaU64, DpaU32, DpaU64 *);

static DpaU16 DpaRead16(const DpaU8 *p) {
  return (DpaU16)((DpaU16)p[0] | (DpaU16)p[1] << 8);
}
static DpaU32 DpaRead32(const DpaU8 *p) {
  return (DpaU32)p[0] | (DpaU32)p[1] << 8 | (DpaU32)p[2] << 16 | (DpaU32)p[3] << 24;
}
static DpaU64 DpaRead64(const DpaU8 *p) {
  return (DpaU64)DpaRead32(p) | (DpaU64)DpaRead32(p + 4) << 32;
}
static void DpaWrite64(DpaU8 *p, DpaU64 value) {
  unsigned i;
  for (i = 0; i != 8; ++i) p[i] = (DpaU8)(value >> (8 * i));
}
static int DpaRange(DpaU64 offset, DpaU64 bytes, DpaU64 limit) {
  return offset <= limit && bytes <= limit - offset;
}
static int DpaName(const DpaU8 *p, const char *name) {
  unsigned i;
  for (i = 0; i < 16; ++i) {
    if (p[i] != (DpaU8)name[i]) return 0;
    if (!name[i]) return 1;
  }
  return name[16] == 0;
}
static int DpaMapped(DpaU64 value, const DpaSegment *segments, unsigned count) {
  unsigned i;
  for (i = 0; i < count; ++i)
    if (value >= segments[i].Va && value - segments[i].Va < segments[i].Size) return 1;
  return 0;
}

static int DpaChain(const DpaU8 *file, const DpaSegment *sg, DpaU64 page,
    DpaU64 pageSize, DpaU64 offset, DpaU64 imageBase, DpaU64 loadVa,
    const DpaSegment *segments, unsigned count, DpaU8 *loaded,
    DpaSigner sign, void *cookie, DpaReport *report) {
  DpaU64 raw, next, target, va, modifier, result, diversity;
  for (;;) {
    /* Format 7 has a four-byte stride. A packed eight-byte pointer may
     * straddle a page; its start stays on this page and both halves must
     * still be backed by the same segment's immutable file data. */
    if ((offset & 3) || !DpaRange(offset, 4, pageSize) ||
        !DpaRange(page, offset + 8, sg->Bytes)) return DPA_CHAIN;
    raw = DpaRead64(file + sg->File + page + offset);
    va = sg->Va + page + offset;
    if (va - loadVa < report->KeysOffset + 80 && va - loadVa + 8 > report->KeysOffset)
      return DPA_KEYS;
    next = (raw >> 51) & 0x7ff;
    if ((raw >> 62) & 1) return DPA_IMPORT;
    if ((DpaU64)report->Authenticated + report->Rebased >= DPA_MAX_FIXUPS) return DPA_CHAIN;
    target = raw & ((raw >> 63) ? 0xffffffffULL : ((1ULL << 43) - 1));
    if (target > ~0ULL - imageBase) return DPA_TARGET;
    target += imageBase;
    if (!DpaMapped(target, segments, count)) return DPA_TARGET;
    result = target;
    if (raw >> 63) {
      diversity = (raw >> 32) & 0xffff;
      modifier = diversity;
      if ((raw >> 48) & 1) {
        modifier = va;
        if (diversity) modifier = (va & 0x0000ffffffffffffULL) | (diversity << 48);
      }
      if (loaded && (!sign || sign(cookie, target, modifier, (DpaU32)((raw >> 49) & 3), &result)))
        return DPA_SIGN;
      ++report->Authenticated;
    } else {
      ++report->Rebased;
    }
    if (loaded) DpaWrite64(loaded + va - loadVa, result);
    if (!next) return DPA_OK;
    offset += next * 4;
  }
}

static int DpaWalk(const DpaU8 *file, const DpaU8 *blob, DpaU64 blobBytes,
    const DpaSegment *segments, unsigned count, DpaU64 loadVa, DpaU8 *loaded,
    DpaSigner sign, void *cookie, DpaReport *report) {
  DpaU64 starts, info, infoBytes, pageSize, page, offset, idx, entries;
  DpaU32 segCount, si, pi, pages;
  DpaU16 start, value;
  int status;
  if (blobBytes < 28 || DpaRead32(blob) != 0) return DPA_FIXUPS;
  if (DpaRead32(blob + 16) != 0) return DPA_IMPORT;
  starts = DpaRead32(blob + 4);
  if (starts < 28 || !DpaRange(starts, 4, blobBytes)) return DPA_FIXUPS;
  segCount = DpaRead32(blob + starts);
  if (segCount > count || !DpaRange(starts + 4, (DpaU64)segCount * 4, blobBytes)) return DPA_FIXUPS;
  for (si = 0; si < segCount; ++si) {
    info = DpaRead32(blob + starts + 4 + si * 4);
    if (!info) continue;
    if (info < 4 + (DpaU64)segCount * 4 || !DpaRange(starts, info, blobBytes)) return DPA_FIXUPS;
    info += starts;
    if (!DpaRange(info, 22, blobBytes)) return DPA_FIXUPS;
    infoBytes = DpaRead32(blob + info);
    if (infoBytes < 22 || !DpaRange(info, infoBytes, blobBytes)) return DPA_FIXUPS;
    pageSize = DpaRead16(blob + info + 4);
    if ((pageSize != 4096 && pageSize != 16384) || DpaRead16(blob + info + 6) != 7) return DPA_FORMAT;
    if (DpaRead64(blob + info + 8) != segments[si].Va - report->ImageBase) return DPA_FIXUPS;
    pages = DpaRead16(blob + info + 20);
    entries = (infoBytes - 22) / 2;
    if (pages > entries || (pages && (DpaU64)(pages - 1) * pageSize >= segments[si].Size)) return DPA_FIXUPS;
    for (pi = 0; pi < pages; ++pi) {
      page = (DpaU64)pi * pageSize;
      start = DpaRead16(blob + info + 22 + (DpaU64)pi * 2);
      if (start == 0xffff) continue;
      if (start & 0x8000) {
        idx = start & 0x7fff;
        if (idx < pages) return DPA_CHAIN;
        do {
          if (idx >= entries) return DPA_CHAIN;
          value = DpaRead16(blob + info + 22 + idx++ * 2);
          offset = value & 0x7fff;
          status = DpaChain(file, &segments[si], page, pageSize, offset,
              report->ImageBase, loadVa, segments, count, loaded, sign, cookie, report);
          if (status) return status;
        } while (!(value & 0x8000));
      } else {
        status = DpaChain(file, &segments[si], page, pageSize, start,
            report->ImageBase, loadVa, segments, count, loaded, sign, cookie, report);
        if (status) return status;
      }
    }
  }
  return DPA_OK;
}

/* A null loaded pointer performs validation only. On failure no writes occur
 * except when a hardware signer itself fails after successful validation.
 * In that case the caller must discard the entire image and refuse the boot.
 */
static int DpaProcess(const DpaU8 *file, DpaU64 bytes, DpaU64 loadVa,
    DpaU64 loadBytes, DpaU8 *loaded, DpaSigner sign, void *cookie, DpaReport *report) {
  DpaSegment segments[DPA_MAX_SEGMENTS];
  DpaU64 at, end, length, data, dataBytes, addr, size, offset, config = 0, keys = 0;
  DpaU64 fixups = 0, fixupBytes = 0, imageBase = 0;
  DpaU32 commands, ci, count = 0, sections, sj, previous;
  DpaReport parsed = {0, 0, 0, 0, 0, 0};
  int status;
  if (!file || !report || bytes < 32 || DpaRead32(file) != 0xfeedfacf ||
      DpaRead32(file + 4) != 0x100000c || (DpaRead32(file + 8) & 0xffffff) != 2 ||
      DpaRead32(file + 12) != 2 || loadBytes > ~0ULL - loadVa) return DPA_IMAGE;
  commands = DpaRead32(file + 16);
  end = 32 + (DpaU64)DpaRead32(file + 20);
  if (end > bytes || commands > (end - 32) / 8) return DPA_COMMAND;
  at = 32;
  for (ci = 0; ci < commands; ++ci, at += length) {
    if (!DpaRange(at, 8, end)) return DPA_COMMAND;
    length = DpaRead32(file + at + 4);
    if (length < 8 || (length & 7) || !DpaRange(at, length, end)) return DPA_COMMAND;
    if (DpaRead32(file + at) == 0x80000034U) {
      if (length != 16 || fixups) return DPA_FIXUPS;
      fixups = DpaRead32(file + at + 8);
      fixupBytes = DpaRead32(file + at + 12);
      if (!fixups || !DpaRange(fixups, fixupBytes, bytes)) return DPA_FIXUPS;
    } else if (DpaRead32(file + at) == 0x19) {
      DpaSegment *sg;
      if (length < 72 || count == DPA_MAX_SEGMENTS) return DPA_COMMAND;
      sections = DpaRead32(file + at + 64);
      if (sections > (length - 72) / 80) return DPA_COMMAND;
      sg = &segments[count++];
      sg->Va = DpaRead64(file + at + 24);
      sg->Size = DpaRead64(file + at + 32);
      sg->File = DpaRead64(file + at + 40);
      sg->Bytes = DpaRead64(file + at + 48);
      if ((sg->Size && (sg->Va < loadVa || !DpaRange(sg->Va - loadVa, sg->Size, loadBytes))) ||
          sg->Bytes > sg->Size || !DpaRange(sg->File, sg->Bytes, bytes)) return DPA_RANGE;
      for (previous = 0; previous + 1 < count; ++previous)
        if (sg->Size && segments[previous].Size && sg->Va < segments[previous].Va + segments[previous].Size &&
            segments[previous].Va < sg->Va + sg->Size) return DPA_RANGE;
      if (DpaName(file + at + 8, "__TEXT")) {
        if (imageBase || !sg->Size) return DPA_IMAGE;
        imageBase = sg->Va;
      }
      for (sj = 0; sj < sections; ++sj) {
        const DpaU8 *sc = file + at + 72 + (DpaU64)sj * 80;
        addr = DpaRead64(sc + 32); size = DpaRead64(sc + 40); offset = DpaRead32(sc + 48);
        if (DpaName(sc, "__pac_keys") || DpaName(sc, "__pac_config")) {
          if (!DpaName(sc + 16, "__DATA") || !DpaName(file + at + 8, "__DATA") ||
              addr < sg->Va || !DpaRange(addr - sg->Va, size, sg->Bytes) ||
              offset != sg->File + addr - sg->Va || !DpaRange(offset, size, bytes) || (addr & 7)) return DPA_RANGE;
          if (DpaName(sc, "__pac_keys")) {
            if (keys || size != 80) return DPA_KEYS;
            keys = addr - loadVa; parsed.KeysOffset = keys;
          } else {
            if (config || size != 32) return DPA_CONFIG;
            config = offset;
          }
        }
      }
    }
  }
  if (at != end || !imageBase || !count) return DPA_COMMAND;
  if (!keys) return DPA_KEYS;
  if (!config || DpaRead64(file + config) != DPA_CONFIG_MAGIC ||
      DpaRead64(file + config + 8) != DPA_TCR_MASK || DpaRead64(file + config + 24) != 80) return DPA_CONFIG;
  parsed.TcrBits = DpaRead64(file + config + 16);
  if ((parsed.TcrBits & ~DPA_TCR_MASK) || ((parsed.TcrBits >> 16) & 0x3f) < 16 ||
      ((parsed.TcrBits >> 16) & 0x3f) > 39) return DPA_CONFIG;
  if (!fixups || fixups < end) return DPA_FIXUPS;
  parsed.ImageBase = imageBase; parsed.Segments = count;
  data = fixups; dataBytes = fixupBytes;
  status = DpaWalk(file, file + data, dataBytes, segments, count, loadVa, 0, 0, 0, &parsed);
  if (status) return status;
  if (loaded) {
    if (!sign) return DPA_SIGN;
    parsed.Authenticated = parsed.Rebased = 0;
    status = DpaWalk(file, file + data, dataBytes, segments, count, loadVa, loaded, sign, cookie, &parsed);
    if (status) return status;
  }
  *report = parsed;
  return DPA_OK;
}
#endif
