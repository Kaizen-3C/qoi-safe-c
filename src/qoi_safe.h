#ifndef QOI_SAFE_H
#define QOI_SAFE_H

#include <stddef.h>

/* Image description filled in by the decoder from the file header. */
typedef struct {
    unsigned int  width;
    unsigned int  height;
    unsigned char channels;    /* 3 or 4, as declared in the file */
    unsigned char colorspace;  /* 0 = sRGB w/ linear alpha, 1 = all linear */
} qoi_safe_desc;

/* Spatial-memory-safe QOI decoder.
 *
 * Behaviour is identical, byte-for-byte, to the reference decoder
 * (phoboslab/qoi, MIT) on every input -- see THIRD_PARTY.md. Every
 * memory-relevant step carries its own explicit, local bounds check, so
 * safety does not depend on the reference's global invariants (read
 * padding, the pixel-cap x int-width overflow coincidence).
 *
 * `data`/`size` : the encoded QOI bytes and their length.
 * `desc`        : receives width/height/channels/colorspace on success.
 * `channels`    : 0 to use the file's channel count, or force 3 or 4.
 *
 * Returns a malloc'd pixel buffer of width*height*channels bytes (caller
 * frees), or NULL on invalid input or allocation failure.
 *
 * Scope: spatial safety (no out-of-bounds access). The returned buffer's
 * lifetime is the caller's; temporal safety is not claimed here.
 */
void *qoi_safe_decode(const void *data, int size, qoi_safe_desc *desc, int channels);

#endif /* QOI_SAFE_H */
