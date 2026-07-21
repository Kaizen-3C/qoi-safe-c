/* fuzz_safe.c -- structure-aware libFuzzer target for qoi_safe_decode.
 *
 * The magic-gated header keeps a naive byte fuzzer shallow, so we synthesise
 * a small VALID header (width/height 1..64, channels 3|4 from the first three
 * bytes) and let the fuzzer drive the entire chunk stream. This exercises the
 * decode write-loop -- every opcode, the index table, run-length, 3/4
 * channels -- at ~35k exec/s under ASan/UBSan.
 *
 *   clang -fsanitize=address,undefined,fuzzer src/qoi_safe.c test/fuzz_safe.c -o fuzz_safe
 *   ./fuzz_safe corpus/valid corpus/hostile
 */
#include "../src/qoi_safe.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static void put32(unsigned char *b, unsigned v) {
    b[0] = (unsigned char)(v >> 24); b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);  b[3] = (unsigned char)v;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;
    unsigned w   = 1u + (data[0] & 0x3f);   /* 1..64 */
    unsigned h   = 1u + (data[1] & 0x3f);   /* 1..64 */
    unsigned fch = (data[2] & 1) ? 4u : 3u;
    data += 3; size -= 3;

    size_t cap = 14 + size + 8;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return 0;
    memcpy(buf, "qoif", 4);
    put32(buf + 4, w);
    put32(buf + 8, h);
    buf[12] = (unsigned char)fch;
    buf[13] = 0;
    memcpy(buf + 14, data, size);
    memset(buf + 14 + size, 0, 8);
    buf[cap - 1] = 1;                       /* qoi_padding end marker */

    qoi_safe_desc d;
    free(qoi_safe_decode(buf, (int)cap, &d, 0));
    free(qoi_safe_decode(buf, (int)cap, &d, 3));
    free(qoi_safe_decode(buf, (int)cap, &d, 4));

    free(buf);
    return 0;
}
