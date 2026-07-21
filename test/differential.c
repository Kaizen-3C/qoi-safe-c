/* differential.c -- the proof in one command.
 *
 * For every file in corpus/valid and corpus/hostile, and each channel mode
 * {0,3,4}, decode with the reference (ref_decode) and compare it, byte for
 * byte, against:
 *   - qoi_safe_decode        (in-process, the hardened C decoder)
 *   - the safe-Rust decoder  (subprocess, if a binary path is given)
 * Both must agree with the reference: both-NULL or both-success, and on
 * success identical desc + byte-identical pixels.
 *
 * Built under ASan/UBSan, so a latent out-of-bounds access in the C decoders
 * trips the sanitizer here. Prints one PASS line per track, or the first
 * divergence and a non-zero exit.
 *
 *   differential <corpus_dir> [path-to-qoi_rs]
 */
#define QOI_NO_STDIO
#include "../vendor/qoi.h"       /* qoi_desc + qoi_decode prototype */
#include "../src/qoi_safe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>

void *ref_decode(const void *data, int size, qoi_desc *desc, int channels);

static const char *g_rust_bin = NULL;

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

/* Run the Rust decoder as a subprocess.
 * returns: 1 = decoded (fills desc + *pixels/*pxlen, caller frees),
 *          0 = NULL (invalid input), -1 = harness/IO error. */
static int rust_decode(const char *path, int ch, qoi_safe_desc *desc,
                       unsigned char **pixels, size_t *pxlen) {
    char tmp[] = "/tmp/qoi_rs_out_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) return -1;
    close(fd);

    char cmd[1400];
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" %d > \"%s\"", g_rust_bin, path, ch, tmp);
    int rc = system(cmd);
    int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;

    if (code == 2) { unlink(tmp); return 0; }        /* reference-NULL parity */
    if (code != 0) { unlink(tmp); return -1; }

    long n = 0;
    unsigned char *buf = slurp(tmp, &n);
    unlink(tmp);
    if (!buf || n < 10) { free(buf); return -1; }

    desc->width  = (unsigned)buf[0] | ((unsigned)buf[1] << 8)
                 | ((unsigned)buf[2] << 16) | ((unsigned)buf[3] << 24);
    desc->height = (unsigned)buf[4] | ((unsigned)buf[5] << 8)
                 | ((unsigned)buf[6] << 16) | ((unsigned)buf[7] << 24);
    desc->channels   = buf[8];
    desc->colorspace = buf[9];

    size_t pl = (size_t)n - 10;
    unsigned char *px = (unsigned char *)malloc(pl ? pl : 1);
    memcpy(px, buf + 10, pl);
    free(buf);
    *pixels = px;
    *pxlen = pl;
    return 1;
}

/* Compare one file across the three channel modes. Adds any C-track failures
 * to *safe_fails and any Rust-track failures to *rust_fails. */
static void compare_one(const char *path, int *safe_fails, int *rust_fails) {
    long n = 0;
    unsigned char *data = slurp(path, &n);
    if (!data) { printf("FAIL slurp %s\n", path); (*safe_fails)++; return; }

    const int chans[3] = { 0, 3, 4 };
    for (int i = 0; i < 3; i++) {
        int ch = chans[i];
        qoi_desc rd; memset(&rd, 0, sizeof rd);
        qoi_safe_desc sd; memset(&sd, 0, sizeof sd);

        void *rp = ref_decode(data, (int)n, &rd, ch);
        void *sp = qoi_safe_decode(data, (int)n, &sd, ch);

        int effch = ch ? ch : (int)rd.channels;
        size_t px = rp ? (size_t)rd.width * rd.height * (size_t)effch : 0;

        /* ---- safe-C track ---- */
        if ((rp == NULL) != (sp == NULL)) {
            printf("FAIL safe-C null-parity %s ch=%d\n", path, ch);
            (*safe_fails)++;
        } else if (rp && sp) {
            if (rd.width != sd.width || rd.height != sd.height ||
                rd.channels != sd.channels || rd.colorspace != sd.colorspace) {
                printf("FAIL safe-C desc %s ch=%d\n", path, ch);
                (*safe_fails)++;
            } else if (memcmp(rp, sp, px) != 0) {
                size_t k = 0;
                while (k < px && ((unsigned char *)rp)[k] == ((unsigned char *)sp)[k]) k++;
                printf("FAIL safe-C bytes %s ch=%d off=%zu\n", path, ch, k);
                (*safe_fails)++;
            }
        }

        /* ---- safe-Rust track (subprocess) ---- */
        if (g_rust_bin) {
            qoi_safe_desc td; memset(&td, 0, sizeof td);
            unsigned char *tp = NULL; size_t tpl = 0;
            int rr = rust_decode(path, ch, &td, &tp, &tpl);
            if (rr < 0) {
                printf("FAIL rust harness %s ch=%d\n", path, ch);
                (*rust_fails)++;
            } else if ((rp == NULL) != (rr == 0)) {
                printf("FAIL rust null-parity %s ch=%d (ref=%s rust=%s)\n",
                       path, ch, rp ? "ok" : "NULL", rr ? "ok" : "NULL");
                (*rust_fails)++;
            } else if (rp && rr == 1) {
                if (rd.width != td.width || rd.height != td.height ||
                    rd.channels != td.channels || rd.colorspace != td.colorspace) {
                    printf("FAIL rust desc %s ch=%d\n", path, ch);
                    (*rust_fails)++;
                } else if (tpl != px || memcmp(rp, tp, px) != 0) {
                    size_t k = 0;
                    while (k < px && k < tpl && ((unsigned char *)rp)[k] == tp[k]) k++;
                    printf("FAIL rust bytes %s ch=%d off=%zu\n", path, ch, k);
                    (*rust_fails)++;
                }
            }
            free(tp);
        }

        free(rp);
        free(sp);
    }
    free(data);
}

static int run_dir(const char *base, const char *sub, int *total,
                   int *safe_fails, int *rust_fails) {
    char dirpath[512];
    snprintf(dirpath, sizeof dirpath, "%s/%s", base, sub);
    DIR *d = opendir(dirpath);
    if (!d) { printf("FAIL opendir %s\n", dirpath); return 1; }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", dirpath, e->d_name);
        compare_one(path, safe_fails, rust_fails);
        (*total)++;
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv) {
    const char *base = argc > 1 ? argv[1] : "corpus";
    g_rust_bin = argc > 2 ? argv[2] : NULL;

    int total = 0, safe_fails = 0, rust_fails = 0;
    run_dir(base, "valid", &total, &safe_fails, &rust_fails);
    run_dir(base, "hostile", &total, &safe_fails, &rust_fails);

    printf("\n");
    printf("safe-C : %s -- %d/%d files, %d decode-cases, byte-identical to reference, sanitizer-clean\n",
           safe_fails ? "FAIL" : "PASS", safe_fails ? 0 : total, total, total * 3);
    if (g_rust_bin) {
        printf("Rust   : %s -- %d/%d files, %d decode-cases, byte-identical to reference\n",
               rust_fails ? "FAIL" : "PASS", rust_fails ? 0 : total, total, total * 3);
    } else {
        printf("Rust   : SKIP -- no qoi_rs binary passed (build rust/ and pass its path)\n");
    }

    return (safe_fails || rust_fails) ? 1 : 0;
}
