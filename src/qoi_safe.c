/* qoi_safe.c -- memory-safe-C QOI decoder.
 *
 * A behaviour-preserving hardening of the reference QOI decoder
 * (phoboslab/qoi, MIT -- see THIRD_PARTY.md). It produces byte-identical
 * output on every input; the difference is that each memory-relevant step
 * carries its own explicit, local bounds check instead of relying on the
 * reference's global invariants:
 *
 *   H1  bounds-checked cursor  -- an over-read cannot dereference out of
 *       range, independent of the 8-byte read-padding invariant.
 *   H2  width-independent pixel-count + explicit allocation-size guard,
 *       instead of relying on the 400M cap interacting with the platform
 *       `int` width.
 *   H3  asserted output-write bound at the point of every store.
 *   H4  header validation reproduced verbatim, for NULL-parity.
 *
 * This buys spatial memory safety (no OOB) that is auditable line by line.
 * Temporal safety (use-after-free of the returned buffer) is the caller's
 * and is not claimed here.
 */

#include "qoi_safe.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define QS_MAGIC       ((((unsigned)'q') << 24) | (((unsigned)'o') << 16) | \
                        (((unsigned)'i') << 8)  |  ((unsigned)'f'))
#define QS_HEADER_SIZE 14
#define QS_PADDING     8
#define QS_PIXELS_MAX  ((unsigned)400000000)

#define QS_OP_INDEX 0x00
#define QS_OP_DIFF  0x40
#define QS_OP_LUMA  0x80
#define QS_OP_RUN   0xc0
#define QS_OP_RGB   0xfe
#define QS_OP_RGBA  0xff
#define QS_MASK_2   0xc0

/* H1: bounds-checked reader. A read past the end sets ok=0 and returns 0;
 * it can never dereference out of range. Within a truthfully-sized buffer
 * this returns exactly the bytes the reference reads (so output matches);
 * `ok` is the structural backstop that holds even if a caller lies about
 * `size` or omits the trailing padding. */
typedef struct {
    const unsigned char *b;
    int len;
    int p;
    int ok;
} qs_reader;

static unsigned qs_u8(qs_reader *r) {
    if (r->p < 0 || r->p >= r->len) {
        r->ok = 0;
        return 0;
    }
    return r->b[r->p++];
}

static unsigned qs_u32(qs_reader *r) {
    unsigned a = qs_u8(r);
    unsigned b = qs_u8(r);
    unsigned c = qs_u8(r);
    unsigned d = qs_u8(r);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

typedef struct { unsigned char r, g, b, a; } qs_rgba;

void *qoi_safe_decode(const void *data, int size, qoi_safe_desc *desc, int channels) {
    if (data == NULL || desc == NULL ||
        (channels != 0 && channels != 3 && channels != 4) ||
        size < QS_HEADER_SIZE + QS_PADDING) {
        return NULL;
    }

    qs_reader r;
    r.b = (const unsigned char *)data;
    r.len = size;
    r.p = 0;
    r.ok = 1;

    unsigned magic  = qs_u32(&r);
    unsigned width  = qs_u32(&r);
    unsigned height = qs_u32(&r);
    unsigned hchan  = qs_u8(&r);
    unsigned hcspc  = qs_u8(&r);
    if (!r.ok) {
        return NULL; /* unreachable given the size guard; explicit anyway */
    }

    /* H4: reject set reproduced verbatim from the reference (incl. the
     * div-form pixel cap, which also bounds width*height < 400M). */
    if (width == 0 || height == 0 ||
        hchan < 3 || hchan > 4 ||
        hcspc > 1 ||
        magic != QS_MAGIC ||
        height >= QS_PIXELS_MAX / width) {
        return NULL;
    }

    desc->width      = width;
    desc->height     = height;
    desc->channels   = (unsigned char)hchan;
    desc->colorspace = (unsigned char)hcspc;

    if (channels == 0) {
        channels = (int)hchan;
    }

    /* H2: pixel count in a width-independent wide type, plus an explicit
     * allocation-size guard. The cap in H4 already bounds width*height < 400M,
     * so neither check below can fire here -- they make the no-overflow
     * property local and auditable rather than a consequence of `int` width. */
    unsigned long px_count = (unsigned long)width * (unsigned long)height;
    assert(px_count < QS_PIXELS_MAX);
    if (px_count > (unsigned long)INT_MAX / (unsigned long)channels) {
        return NULL;
    }
    size_t px_len = (size_t)px_count * (size_t)channels;

    unsigned char *pixels = (unsigned char *)malloc(px_len);
    if (!pixels) {
        return NULL;
    }

    qs_rgba index[64];
    memset(index, 0, sizeof(index));

    qs_rgba px;
    px.r = 0;
    px.g = 0;
    px.b = 0;
    px.a = 255;

    int chunks_len = size - QS_PADDING;
    int run = 0;

    for (size_t px_pos = 0; px_pos < px_len; px_pos += (size_t)channels) {
        if (run > 0) {
            run--;
        }
        else if (r.p < chunks_len) {
            int b1 = (int)qs_u8(&r);

            if (b1 == QS_OP_RGB) {
                px.r = (unsigned char)qs_u8(&r);
                px.g = (unsigned char)qs_u8(&r);
                px.b = (unsigned char)qs_u8(&r);
            }
            else if (b1 == QS_OP_RGBA) {
                px.r = (unsigned char)qs_u8(&r);
                px.g = (unsigned char)qs_u8(&r);
                px.b = (unsigned char)qs_u8(&r);
                px.a = (unsigned char)qs_u8(&r);
            }
            else if ((b1 & QS_MASK_2) == QS_OP_INDEX) {
                px = index[b1 & 63]; /* b1 <= 0x3f in this branch; mask documents it */
            }
            else if ((b1 & QS_MASK_2) == QS_OP_DIFF) {
                px.r += ((b1 >> 4) & 0x03) - 2;
                px.g += ((b1 >> 2) & 0x03) - 2;
                px.b += ( b1       & 0x03) - 2;
            }
            else if ((b1 & QS_MASK_2) == QS_OP_LUMA) {
                int b2 = (int)qs_u8(&r);
                int vg = (b1 & 0x3f) - 32;
                px.r += vg - 8 + ((b2 >> 4) & 0x0f);
                px.g += vg;
                px.b += vg - 8 +  (b2       & 0x0f);
            }
            else if ((b1 & QS_MASK_2) == QS_OP_RUN) {
                run = b1 & 0x3f;
            }

            index[(px.r * 3 + px.g * 5 + px.b * 7 + px.a * 11) & 63] = px;
        }

        /* H3: output-write bound, explicit at the point of store. */
        assert(px_pos + (size_t)channels <= px_len);
        pixels[px_pos + 0] = px.r;
        pixels[px_pos + 1] = px.g;
        pixels[px_pos + 2] = px.b;
        if (channels == 4) {
            pixels[px_pos + 3] = px.a;
        }
    }

    return pixels;
}
