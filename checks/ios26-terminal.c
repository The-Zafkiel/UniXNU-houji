/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <string.h>
#include "HoujiTerminal26.h"

static uint32_t pixels[32 + 96 * 160 + 32];
static unsigned char font[256 * 16];

int main(void) {
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
