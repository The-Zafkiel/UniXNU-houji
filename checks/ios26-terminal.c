/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <string.h>
#include "HoujiTerminal26.h"

static uint32_t pixels[32 + 96 * 160 + 32];
static unsigned char font[256 * 16];

int main(void) {
    Hjt26Reason reason = {0}, truncated = {0};
    const uint8_t run_id[16] = {1,2,3}, other[16] = {4,5,6};
    Hjt26Reason initialized = {0};
    hjt26_initialize(&initialized, run_id);
    assert(initialized.state == 0 && hp26_envelope(&initialized.record, run_id));
    assert(!hp26_valid(&initialized.record, run_id));
    hjt26_capture(&initialized, "first failure after initialization", run_id);
    hjt26_initialize(&initialized, other);
    assert(initialized.state == 2 && hp26_valid(&initialized.record, run_id));
    hjt26_capture(&reason, NULL, run_id); hjt26_capture(&reason, "", run_id);
    assert(reason.state == 0);
    hjt26_capture(&reason, "original panic: errno 8", run_id);
    hjt26_capture(&reason, "nested panic must not replace the first cause", other);
    assert(reason.state == 2 && !strcmp(reason.record.text, "original panic: errno 8"));
    assert(hp26_valid(&reason.record, run_id) && !hp26_valid(&reason.record, other));
    reason.record.text[0] ^= 1; assert(!hp26_valid(&reason.record, run_id));
    reason.record.text[0] ^= 1; assert(hp26_valid(&reason.record, run_id));
    char large[1024]; memset(large, 'x', sizeof large);
    hjt26_capture(&truncated, large, run_id);
    assert(truncated.state == 2 && strlen(truncated.record.text) == 511);
    assert(hp26_valid(&truncated.record, run_id));
    assert(hp26_range(0x80004004, 0x80000000, 8ULL << 30, 0x80000000, 0x10000000));
    assert(!hp26_range(0x80004004, 0x80000000, 2ULL << 30, 0x80000000, 0x10000000));
    assert(!hp26_range(0x80004004, 0x80008000, 8ULL << 30, 0x80000000, 0x10000000));
    assert(!hp26_range(0x80004004, 0x80000000, 8ULL << 30, 0x80004008, 0x10000000));
    assert(!hp26_range(0x80004004, 0x80000000, 8ULL << 30, 0x80004000, 32));
    assert(!hp26_range(UINT64_MAX - 1, 0x80000000, 8ULL << 30, 0, UINT64_MAX));
    Hjt26Surface s = {0}, before;
    uintptr_t base = (uintptr_t)(pixels + 32);
    size_t capacity = 96 * 160 * 4;
    assert(hjt26_surface(&s, base, 80, 160, 384, 32, capacity));
    before = s;
    assert(!hjt26_surface(&s, 0, 80, 160, 384, 32, capacity));
    assert(!hjt26_surface(&s, base + 1, 80, 160, 384, 32, capacity));
    assert(!hjt26_surface(&s, UINTPTR_MAX - 3, 80, 160, 384, 32, capacity));
    assert(!hjt26_surface(&s, base, 0, 160, 384, 32, capacity));
    assert(!hjt26_surface(&s, base, 80, 0, 384, 32, capacity));
    assert(!hjt26_surface(&s, base, 80, 160, 384, 16, capacity));
    assert(!hjt26_surface(&s, base, 80, 160, 319, 32, capacity));
    assert(!hjt26_surface(&s, base, 80, 160, 322, 32, capacity));
    assert(!hjt26_surface(&s, base, 80, 160, 384, 32, capacity - 1));
    assert(!hjt26_surface(&s, base, 16384, 16384, 65536, 32, SIZE_MAX));
    assert(!hjt26_surface(NULL, base, 80, 160, 384, 32, capacity));
    assert(!memcmp(&s, &before, sizeof s));
    for (size_t i = 0; i < sizeof pixels / sizeof pixels[0]; ++i) pixels[i] = 0x12345678;
    memset(font + 'A' * 16, 0x80, 16);
    memset(font + '?' * 16, 0x40, 16);
    const char title[] = {'A', '\r', '\n', (char)0xff, 'B'};
    char detail[8192]; memset(detail, 'x', sizeof detail);
    hjt26_render(&s, font, title, sizeof title, detail, sizeof detail);
    assert(s.base[8 * 96 + 8] == 0xffeeeeeeU);
    assert(s.base[8 * 96 + 10] == 0xff000000U);
    assert(s.base[40 * 96 + 8] == 0xff000000U);
    assert(s.base[40 * 96 + 10] == 0xffeeeeeeU);
    for (unsigned i = 0; i < 32; ++i) {
        assert(pixels[i] == 0x12345678);
        assert(pixels[32 + 96 * 160 + i] == 0x12345678);
    }
    for (unsigned y = 0; y < 160; ++y)
        for (unsigned x = 80; x < 96; ++x) assert(s.base[y * 96 + x] == 0x12345678);
    return 0;
}
