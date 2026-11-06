/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * One bounded RAM-only failure record, identified by the current trial.
 */
#ifndef HOUJI_PANIC26_H
#define HOUJI_PANIC26_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t magic[8], run_id[16];
    uint32_t length, crc32;
    char text[512];
} Hp26Record;
#ifdef __cplusplus
static_assert(sizeof(Hp26Record) == 544, "panic record ABI");
#else
_Static_assert(sizeof(Hp26Record) == 544, "panic record ABI");
#endif

static inline uint32_t hp26_crc(const Hp26Record *record) {
    const uint8_t *p = (const uint8_t *)record;
    uint32_t crc = ~0U;
    for (size_t i = 0; i < sizeof(*record); ++i) {
        crc ^= i >= offsetof(Hp26Record, crc32) && i < offsetof(Hp26Record, crc32) + 4 ? 0 : p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static inline int hp26_envelope(const Hp26Record *record, const uint8_t run_id[16]) {
    static const uint8_t magic[8] = {'H','J','P','A','N','0','2','6'};
    if (!record || !run_id || record->length >= sizeof(record->text)) return 0;
    for (unsigned i = 0; i < 8; ++i) if (record->magic[i] != magic[i]) return 0;
    for (unsigned i = 0; i < 16; ++i) if (record->run_id[i] != run_id[i]) return 0;
    return !record->text[record->length] && hp26_crc(record) == record->crc32;
}

static inline int hp26_valid(const Hp26Record *record, const uint8_t run_id[16]) {
    return record && record->length && hp26_envelope(record, run_id);
}

static inline int hp26_range(uint64_t address, uint64_t owned_base, uint64_t owned_bytes,
    uint64_t system_base, uint64_t system_bytes) {
    const uint64_t bytes = sizeof(Hp26Record);
    if (owned_base < 0x80000000ULL || owned_bytes != (8ULL << 30) ||
        owned_bytes > UINT64_MAX - owned_base || system_bytes < bytes ||
        system_bytes > UINT64_MAX - system_base || address > 0x0000ffffffffffffULL - bytes ||
        address < owned_base || address - owned_base > owned_bytes - bytes ||
        address < system_base || address - system_base > system_bytes - bytes) return 0;
    return 1;
}
#endif
