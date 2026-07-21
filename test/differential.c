/* differential.c -- the proof in one command.
 *
 * For every file in corpus/valid and corpus/hostile, and each channel mode
 * {0,3,4}, decode with the reference (ref_decode) and with qoi_safe_decode
 * and assert they agree:
 *   - both return NULL, or both succeed;
 *   - on success, identical desc and byte-identical pixels.
 * Built under ASan/UBSan, so a latent out-of-bounds access in EITHER decoder
 * trips the sanitizer here. Exit 0 and "PASS n/n" on full agreement, else the
 * first divergence and exit 1.
 */
#define QOI_NO_STDIO
#include "../vendor/qoi.h"       /* qoi_desc + qoi_decode prototype */
#include "../src/qoi_safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

void *ref_decode(const void *data, int size, qoi_desc *desc, int channels);

static unsigned char *slurp(const char *path, long *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)(n ? n : 1));
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    *out_n = n;
    return b;
}

static int compare_one(const char *path) {
    long n = 0;
    unsigned char *data = slurp(path, &n);
    if (!data) { printf("FAIL slurp %s\n", path); return 1; }

    int fails = 0;
    const int chans[3] = { 0, 3, 4 };
    for (int i = 0; i < 3; i++) {
        int ch = chans[i];
        qoi_desc rd; memset(&rd, 0, sizeof rd);
        qoi_safe_desc sd; memset(&sd, 0, sizeof sd);

        void *rp = ref_decode(data, (int)n, &rd, ch);
        void *sp = qoi_safe_decode(data, (int)n, &sd, ch);

        if ((rp == NULL) != (sp == NULL)) {
            printf("FAIL null-parity %s ch=%d ref=%s safe=%s\n",
                   path, ch, rp ? "ok" : "NULL", sp ? "ok" : "NULL");
            fails++;
        } else if (rp && sp) {
            if (rd.width != sd.width || rd.height != sd.height ||
                rd.channels != sd.channels || rd.colorspace != sd.colorspace) {
                printf("FAIL desc %s ch=%d (ref %ux%u c%u s%u / safe %ux%u c%u s%u)\n",
                       path, ch, rd.width, rd.height, rd.channels, rd.colorspace,
                       sd.width, sd.height, sd.channels, sd.colorspace);
                fails++;
            } else {
                int effch = ch ? ch : (int)rd.channels;
                size_t px = (size_t)rd.width * rd.height * (size_t)effch;
                if (memcmp(rp, sp, px) != 0) {
                    size_t k = 0;
                    while (k < px && ((unsigned char *)rp)[k] == ((unsigned char *)sp)[k]) k++;
                    printf("FAIL bytes %s ch=%d first-diff-offset=%zu of %zu\n", path, ch, k, px);
                    fails++;
                }
            }
        }
        free(rp);
        free(sp);
    }
    free(data);
    return fails;
}

static int run_dir(const char *base, const char *sub, int *total) {
    char dirpath[512];
    snprintf(dirpath, sizeof dirpath, "%s/%s", base, sub);
    DIR *d = opendir(dirpath);
    if (!d) { printf("FAIL opendir %s\n", dirpath); return 1; }

    int fails = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dirpath, e->d_name);
        fails += compare_one(path);
        (*total)++;
    }
    closedir(d);
    return fails;
}

int main(int argc, char **argv) {
    const char *base = argc > 1 ? argv[1] : "corpus";
    int total = 0, fails = 0;
    fails += run_dir(base, "valid", &total);
    fails += run_dir(base, "hostile", &total);

    if (fails == 0) {
        printf("PASS %d/%d files: safe == reference (byte-identical), sanitizer-clean\n",
               total, total);
        return 0;
    }
    printf("FAILED: %d divergence(s) over %d files\n", fails, total);
    return 1;
}
