/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Bounded terminal text on an existing console mapping. No allocation or I/O.
 */
#ifndef HOUJI_TERMINAL26_H
#define HOUJI_TERMINAL26_H
#include <stdint.h>
#include <stddef.h>
#include "HoujiPanic26.h"

typedef struct {
    volatile uint32_t *base;
    uint32_t width, height, row_words;
} Hjt26Surface;

typedef struct { unsigned state; Hp26Record record; } Hjt26Reason;

static inline void hjt26_capture(Hjt26Reason *reason, const char *text, const uint8_t run_id[16]) {
    if (!reason || !text || !text[0] || !run_id) return;
    unsigned empty = 0;
    if (!__atomic_compare_exchange_n(&reason->state, &empty, 1U, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    Hp26Record *record = &reason->record;
    static const uint8_t magic[8] = {'H','J','P','A','N','0','2','6'};
    for (unsigned i = 0; i < 8; ++i) record->magic[i] = magic[i];
    for (unsigned i = 0; i < 16; ++i) record->run_id[i] = run_id[i];
    size_t i = 0;
    while (i + 1 < sizeof(record->text) && text[i]) { record->text[i] = text[i]; ++i; }
    record->length = (uint32_t)i;
    while (i < sizeof(record->text)) record->text[i++] = 0;
    record->crc32 = hp26_crc(record);
    __atomic_store_n(&reason->state, 2U, __ATOMIC_RELEASE);
}

static inline int hjt26_surface(Hjt26Surface *out, uintptr_t base, uint32_t width,
    uint32_t height, uint32_t row_bytes, uint32_t depth, size_t capacity) {
    if (!out || !base || (base & 3) || depth != 32 || width < 32 || height < 64 ||
        width > 16384 || height > 16384 || (row_bytes & 3) || row_bytes < width * 4 ||
        row_bytes > 65536) return 0;
    uint64_t bytes = (uint64_t)row_bytes * height;
    if (bytes > 64 * 1024 * 1024 || bytes > capacity || bytes > UINTPTR_MAX - base) return 0;
    *out = (Hjt26Surface){(volatile uint32_t *)base, width, height, row_bytes / 4};
    return 1;
}

static inline void hjt26_text(Hjt26Surface *s, const unsigned char *font,
    const char *text, size_t length, uint32_t *column, uint32_t *line) {
    const uint32_t columns = (s->width - 16) / 16, lines = (s->height - 16) / 32;
    if (!font || !text) return;
    if (length > 4096) length = 4096;
    for (size_t i = 0; i < length && text[i] && *line < lines; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == '\r') continue;
        if (ch == '\n') { *column = 0; ++*line; continue; }
        if (*column >= columns) { *column = 0; ++*line; }
        if (*line >= lines) break;
        if (ch < 32 || ch > 126) ch = '?';
        uint32_t x = 8 + *column * 16, y = 8 + *line * 32;
        for (uint32_t r = 0; r < 16; ++r) {
            unsigned bits = font[(unsigned)ch * 16 + r];
            for (uint32_t c = 0; c < 8; ++c) {
                uint32_t pixel = (bits & (0x80U >> c)) ? 0xffeeeeeeU : 0xff000000U;
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx)
                        s->base[(size_t)(y + 2 * r + dy) * s->row_words + x + 2 * c + dx] = pixel;
            }
        }
        ++*column;
    }
}

static inline void hjt26_render(Hjt26Surface *s, const unsigned char *font,
    const char *title, size_t title_length, const char *detail, size_t detail_length) {
    for (uint32_t y = 0; y < s->height; ++y)
        for (uint32_t x = 0; x < s->width; ++x)
            s->base[(size_t)y * s->row_words + x] = 0xff000000U;
    uint32_t column = 0, line = 0;
    hjt26_text(s, font, title, title_length, &column, &line);
    column = 0; line += 2;
    hjt26_text(s, font, detail, detail_length, &column, &line);
}
#endif
