/* original.c -- reference baseline.
 *
 * The decoder under test-against is phoboslab/qoi (MIT), included
 * unmodified from vendor/qoi.h (see THIRD_PARTY.md). This wrapper adds no
 * logic; it only exposes the reference decoder under a stable name for the
 * differential harness.
 */
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../vendor/qoi.h"

void *ref_decode(const void *data, int size, qoi_desc *desc, int channels) {
    return qoi_decode(data, size, desc, channels);
}
