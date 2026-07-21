/* gen_corpus.c -- deterministic corpus generator.
 *
 * Writes corpus/valid/*  (roundtrip-encoded well-formed images) and
 * corpus/hostile/*  (hand-crafted adversarial inputs). Uses the reference
 * encoder for the valid set so the happy path is genuine QOI. No randomness:
 * the corpus is reproducible byte-for-byte.
 */
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../vendor/qoi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[400];

static void wr(const char *sub, const char *name, const void *buf, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s/%s", g_dir, sub, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    if (n && fwrite(buf, 1, n, f) != n) { perror("fwrite"); exit(1); }
    fclose(f);
}

static void gen_valid(const char *name, int w, int h, int ch, int mode) {
    unsigned char *img = (unsigned char *)malloc((size_t)w * h * ch);
    for (int i = 0; i < w * h; i++) {
        unsigned char *p = img + (size_t)i * ch;
        switch (mode) {
            case 0: p[0] = (i * 7) & 0xff; p[1] = (i * 5) & 0xff; p[2] = (i * 3) & 0xff; break;
            case 1: p[0] = 128; p[1] = 64; p[2] = 32; break;
            case 2: p[0] = (i & 1) ? 200 : 10; p[1] = (i & 2) ? 200 : 10; p[2] = (i & 4) ? 200 : 10; break;
            default: p[0] = p[1] = p[2] = 0; break;
        }
        if (ch == 4) p[3] = (mode == 2) ? (unsigned char)((i * 11) & 0xff) : 255;
    }
    qoi_desc d;
    d.width = (unsigned)w; d.height = (unsigned)h;
    d.channels = (unsigned char)ch; d.colorspace = 0;
    int outlen = 0;
    void *enc = qoi_encode(img, &d, &outlen);
    if (!enc) { fprintf(stderr, "encode failed %s\n", name); exit(1); }
    wr("valid", name, enc, (size_t)outlen);
    free(enc);
    free(img);
}

static void put32(unsigned char *b, unsigned v) {
    b[0] = (unsigned char)(v >> 24); b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);  b[3] = (unsigned char)v;
}

/* write a 14-byte header into b, return the offset past it */
static int hdr(unsigned char *b, unsigned w, unsigned h, unsigned char ch, unsigned char cs) {
    memcpy(b, "qoif", 4);
    put32(b + 4, w);
    put32(b + 8, h);
    b[12] = ch;
    b[13] = cs;
    return 14;
}

static const unsigned char PAD[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };

int main(int argc, char **argv) {
    snprintf(g_dir, sizeof g_dir, "%s", argc > 1 ? argv[1] : "corpus");

    /* ---- valid (roundtrip-encoded) ---- */
    gen_valid("gradient_rgb.qoi",  16, 16, 3, 0);
    gen_valid("gradient_rgba.qoi", 16, 16, 4, 0);
    gen_valid("solid_rle.qoi",     32, 32, 3, 1);
    gen_valid("indexed_rgba.qoi",  20, 20, 4, 2);
    gen_valid("tiny.qoi",           1,  1, 3, 0);

    /* ---- hostile (hand-crafted) ---- */
    unsigned char b[256];
    int n;

    /* size below the 22-byte floor */
    hdr(b, 4, 4, 3, 0);
    wr("hostile", "trunc_header.qoi", b, 10);

    /* header claims 8x8 but the chunk stream ends almost immediately */
    n = hdr(b, 8, 8, 3, 0);
    b[n++] = 0xfe; b[n++] = 1; b[n++] = 2; b[n++] = 3;
    memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "trunc_stream.qoi", b, (size_t)n);

    /* valid-length stream (>=22 bytes) but no trailing 00..01 end marker */
    n = hdr(b, 2, 2, 3, 0);
    for (int i = 0; i < 4; i++) { b[n++] = 0xfe; b[n++] = (unsigned char)(i*10);
                                  b[n++] = (unsigned char)(i*10+1); b[n++] = (unsigned char)(i*10+2); }
    wr("hostile", "no_padding.qoi", b, (size_t)n);

    /* width*height overflow attempt */
    n = hdr(b, 0xffffffffu, 0xffffffffu, 3, 0);
    memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "dim_overflow.qoi", b, (size_t)n);

    /* zero dimensions */
    n = hdr(b, 0, 4, 3, 0); memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "dim_zero_w.qoi", b, (size_t)n);
    n = hdr(b, 4, 0, 3, 0); memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "dim_zero_h.qoi", b, (size_t)n);

    /* invalid channel counts */
    {
        static const unsigned char badc[] = { 0, 1, 2, 5, 255 };
        for (size_t i = 0; i < sizeof badc; i++) {
            char nm[64];
            snprintf(nm, sizeof nm, "chan_bad_%u.qoi", (unsigned)badc[i]);
            n = hdr(b, 4, 4, badc[i], 0); memcpy(b + n, PAD, 8); n += 8;
            wr("hostile", nm, b, (size_t)n);
        }
    }

    /* invalid colorspace / magic */
    n = hdr(b, 4, 4, 3, 2); memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "colorspace_bad.qoi", b, (size_t)n);
    n = hdr(b, 4, 4, 3, 0); b[0] = 'x'; memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "magic_bad.qoi", b, (size_t)n);

    /* max-count RUN on a 1-pixel image */
    n = hdr(b, 1, 1, 3, 0); b[n++] = 0xc0 | 0x3f; memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "run_overrun.qoi", b, (size_t)n);

    /* RGBA opcode as the last byte before chunks_len (max +3 read overshoot) */
    n = hdr(b, 2, 1, 4, 0);
    b[n++] = 0xff; b[n++] = 1; b[n++] = 2; b[n++] = 3; b[n++] = 4;
    memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "rgba_at_edge.qoi", b, (size_t)n);

    /* LUMA opcode at the edge (+1 read overshoot) */
    n = hdr(b, 2, 1, 3, 0);
    b[n++] = 0x80 | 0x20; b[n++] = 0x88;
    memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "luma_at_edge.qoi", b, (size_t)n);

    /* one of every opcode in a small image */
    n = hdr(b, 8, 1, 4, 0);
    b[n++] = 0xfe; b[n++] = 10; b[n++] = 20; b[n++] = 30;          /* RGB   */
    b[n++] = 0xff; b[n++] = 40; b[n++] = 50; b[n++] = 60; b[n++] = 70; /* RGBA */
    b[n++] = 0x40 | 0x2d;                                          /* DIFF  */
    b[n++] = 0x80 | 0x10; b[n++] = 0x83;                           /* LUMA  */
    b[n++] = 0x00;                                                 /* INDEX */
    b[n++] = 0xc0 | 0x01;                                          /* RUN   */
    memcpy(b + n, PAD, 8); n += 8;
    wr("hostile", "all_ops.qoi", b, (size_t)n);

    printf("corpus generated under %s\n", g_dir);
    return 0;
}
