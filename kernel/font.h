#ifndef JDY_FONT_H
#define JDY_FONT_H

#include <stdint.h>

// Original jdy-os glyphs, dedicated to the public domain under CC0-1.0.
// Each hex digit pair is one 5-bit row. GlyphRow expands these to 8x16
// with one blank row at each end and one blank column at the left.
constexpr uint64_t kUppercase[] = {
    0x0e11111f111111, 0x1e11111e11111e, 0x0e11101010110e,
    0x1e11111111111e, 0x1f10101e10101f, 0x1f10101e101010,
    0x0e11101711110f, 0x1111111f111111, 0x0e04040404040e,
    0x0702020212120c, 0x11121418141211, 0x1010101010101f,
    0x111b1515111111, 0x11191915131311, 0x0e11111111110e,
    0x1e11111e101010, 0x0e11111115120d, 0x1e11111e141211,
    0x0f10100e01011e, 0x1f040404040404, 0x1111111111110e,
    0x11111111110a04, 0x11111115151b11, 0x11110a040a1111,
    0x11110a04040404, 0x1f01020408101f,
};

constexpr uint64_t kLowercase[] = {
    0x00000e010f110f, 0x10101e1111111e, 0x00000e1110110e,
    0x01010f1111110f, 0x00000e111f100e, 0x0609081c080808,
    0x000f11110f010e, 0x10101e11111111, 0x04000c0404040e,
    0x0200060202120c, 0x101011121c1211, 0x0c04040404040e,
    0x00001a15151515, 0x00001e11111111, 0x00000e1111110e,
    0x001e11111e1010, 0x000f11110f0101, 0x00001619101010,
    0x00000f100e011e, 0x08081c08080906, 0x0000111111110f,
    0x00001111110a04, 0x0000111115150a, 0x0000110a040a11,
    0x001111110f010e, 0x00001f0204081f,
};

constexpr uint64_t kDigits[] = {
    0x0e11131519110e, 0x040c040404040e, 0x0e11010204081f,
    0x1e01010e01011e, 0x02060a121f0202, 0x1f10101e01011e,
    0x0e10101e11110e, 0x1f010204080808, 0x0e11110e11110e,
    0x0e11110f01010e,
};

constexpr uint64_t Glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return kUppercase[c - 'A'];
    if (c >= 'a' && c <= 'z') return kLowercase[c - 'a'];
    if (c >= '0' && c <= '9') return kDigits[c - '0'];
    switch (c) {
    case ' ': return 0;
    case '-': return 0x0000001f000000;
    case ':': return 0x00040000040000;
    case '!': return 0x04040404040004;
    default:  return 0x0e110102040004;
    }
}

constexpr uint8_t GlyphRow(uint64_t glyph, unsigned row)
{
    if (row == 0 || row == 15) return 0;
    const unsigned source_row = (row - 1) / 2;
    return ((glyph >> (8 * (6 - source_row))) & 0x1f) << 2;
}

#endif
