/* bench.c -- decode throughput: reference C vs the hardened safe-C decoder.
 *
 * Answers the "does the hardening cost performance?" question with a number
 * you can re-run (make bench), not a claim. Built at -O2 WITHOUT sanitizers
 * (sanitizers would dominate and distort the comparison). Encodes one large
 * image with the reference encoder, writes it to <path> for the Rust bench to
 * reuse, then times K decodes through each decoder.
 */
#define _POSIX_C_SOURCE 199309L
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../vendor/qoi.h"
#include "../src/qoi_safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "bench_big.qoi";
    const int W = 1024, H = 1024, C = 4, K = 300;

    unsigned char *img = (unsigned char *)malloc((size_t)W * H * C);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        img[i*4+0] = (unsigned char)(i * 7);
        img[i*4+1] = (unsigned char)(i * 13 + i / 64);
        img[i*4+2] = (unsigned char)(i * 3);
        img[i*4+3] = 255;
    }
    qoi_desc d = { (unsigned)W, (unsigned)H, (unsigned char)C, 0 };
    int enclen = 0;
    void *enc = qoi_encode(img, &d, &enclen);
    if (!enc) { fprintf(stderr, "encode failed\n"); return 1; }

    FILE *f = fopen(out, "wb");
    if (f) { fwrite(enc, 1, (size_t)enclen, f); fclose(f); }

    /* warm-up */
    for (int k = 0; k < 20; k++) {
        qoi_desc dd; free(qoi_decode(enc, enclen, &dd, 4));
        qoi_safe_desc sd; free(qoi_safe_decode(enc, enclen, &sd, 4));
    }

    double t0 = now_s();
    for (int k = 0; k < K; k++) { qoi_desc dd; free(qoi_decode(enc, enclen, &dd, 4)); }
    double t1 = now_s();
    for (int k = 0; k < K; k++) { qoi_safe_desc sd; free(qoi_safe_decode(enc, enclen, &sd, 4)); }
    double t2 = now_s();

    double mp = (double)K * W * H / 1e6;
    double ref = mp / (t1 - t0), safe = mp / (t2 - t1);
    printf("image        : %dx%d RGBA, %d bytes encoded\n", W, H, enclen);
    printf("reference C  : %6.1f Mpx/s\n", ref);
    printf("safe-C       : %6.1f Mpx/s  (%.1f%% of reference)\n", safe, 100.0 * safe / ref);

    free(enc);
    free(img);
    return 0;
}
