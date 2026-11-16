#include "bismuth/android_api.h"

typedef int (*BismuthUploadSignature)(void*, const BismuthCandidate*, uint32_t, uint32_t);
_Static_assert(_Generic(&bismuth_upload, BismuthUploadSignature: 1, default: 0),
               "bismuth_upload signature must include atlas capacity");

int main(void) {
    if (BISMUTH_MAX_CANDIDATES != 16384u) return 1;
    BismuthCandidate candidate = {0};
    candidate.left = -1.0f;
    candidate.right = 1.0f;
    return sizeof(candidate) == 32 && BISMUTH_CAP_GLES31_COMPUTE == 2u && BISMUTH_CAP_ATLAS_INDEX_OUTPUT == 4u ? 0 : 1;
}
