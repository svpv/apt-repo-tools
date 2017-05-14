// Copyright (c) 2017 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <string.h>
#include <endian.h>

static inline unsigned XXH_read32(void *p)
{
    unsigned x;
    memcpy(&x, p, 4);
    return le32toh(x);
}

static inline unsigned XXH_rotl32(unsigned x, int r)
{
    return (x << r) | (x >> (32 - r));
}

// Hash 10 bytes with XXHash32.
static unsigned XXH_hash10(unsigned char *p)
{
    const unsigned PRIME32_1 = 2654435761U;
    const unsigned PRIME32_2 = 2246822519U;
    const unsigned PRIME32_3 = 3266489917U;
    const unsigned PRIME32_4 =  668265263U;
    const unsigned PRIME32_5 =  374761393U;

    unsigned h = PRIME32_5 + 10;
    h += XXH_read32(p + 0) * PRIME32_3;
    h  = XXH_rotl32(h, 17) * PRIME32_4;
    h += XXH_read32(p + 4) * PRIME32_3;
    h  = XXH_rotl32(h, 17) * PRIME32_4;

    h += p[8] * PRIME32_5;
    h  = XXH_rotl32(h, 11) * PRIME32_1;
    h += p[9] * PRIME32_5;
    h  = XXH_rotl32(h, 11) * PRIME32_1;

    h ^= h >> 15;
    h *= PRIME32_2;
    h ^= h >> 13;
    h *= PRIME32_3;
    h ^= h >> 16;
    return h;
}

#include <stdbool.h>
#include <stdint.h>
#include <lz4frame.h>

// Store the content size field in LZ4 frame header.
static bool lz4fix(unsigned char frameHeader[LZ4F_HEADER_SIZE_MAX], uint64_t contentSize)
{
    // See lz4_Frame_format.md.
    unsigned magic = htole32(0x184D2204);
    if (memcmp(frameHeader, &magic, 4))
	return false;

    // Set the Content Size flag.
    if (frameHeader[4] & 8)
	return false;
    frameHeader[4] |= 8;

    contentSize = htole64(contentSize);
    memcpy(frameHeader + 6, &contentSize, 8);

    frameHeader[14] = XXH_hash10(frameHeader + 4) >> 8;
    return true;
}

// ex:set ts=8 sts=4 sw=4 noet:
