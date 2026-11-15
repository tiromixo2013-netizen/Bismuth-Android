#include "bismuth/android_api.h"

int main(void) {
    if (BISMUTH_MAX_CANDIDATES != 16384u) return 1;
    BismuthCandidate candidate = {0};
    candidate.left = -1.0f;
    candidate.right = 1.0f;
    return sizeof(candidate) == 32 && BISMUTH_CAP_GLES31_COMPUTE == 2u ? 0 : 1;
}
