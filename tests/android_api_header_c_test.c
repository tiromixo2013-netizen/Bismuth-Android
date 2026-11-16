#include "bismuth/android_api.h"

typedef int (*BismuthUploadSignature)(void*, const BismuthCandidate*, uint32_t, uint32_t);
typedef int (*BismuthInstallHooksSignature)(void*, char*, uint32_t);
typedef int (*BismuthAutoHookStatusSignature)(void);
typedef const char* (*BismuthAutoHookErrorSignature)(void);
typedef int (*BismuthObserverSnapshotSignature)(void*, BismuthAtlasTelemetry*, uint64_t*, uint64_t*);
typedef void (*BismuthObserverMutationSignature)(void*, int);
_Static_assert(_Generic(&bismuth_upload, BismuthUploadSignature: 1, default: 0),
               "bismuth_upload signature must include atlas capacity");
_Static_assert(_Generic(&bismuth_install_cocos_hooks, BismuthInstallHooksSignature: 1, default: 0),
               "Cocos hook installation C ABI signature must remain stable");
_Static_assert(_Generic(&bismuth_auto_hook_status, BismuthAutoHookStatusSignature: 1, default: 0),
               "automatic hook status C ABI signature must remain stable");
_Static_assert(_Generic(&bismuth_auto_hook_error, BismuthAutoHookErrorSignature: 1, default: 0),
               "automatic hook error C ABI signature must remain stable");
_Static_assert(_Generic(&bismuth_observer_snapshot, BismuthObserverSnapshotSignature: 1, default: 0),
               "observer snapshot C ABI signature must remain stable");
_Static_assert(_Generic(&bismuth_observer_record_structural_mutation,
                        BismuthObserverMutationSignature: 1, default: 0),
               "observer mutation C ABI signature must remain stable");
_Static_assert(sizeof(BismuthAtlasTelemetry) == 64,
               "BismuthAtlasTelemetry must have a stable 64-byte C layout");

int main(void) {
    if (BISMUTH_MAX_CANDIDATES != 16384u) return 1;
    BismuthCandidate candidate = {0};
    candidate.left = -1.0f;
    candidate.right = 1.0f;
    return sizeof(candidate) == 32 &&
           BISMUTH_CAP_GLES31_COMPUTE == 2u &&
           BISMUTH_CAP_ATLAS_INDEX_OUTPUT == 4u &&
           BISMUTH_CAP_PASSIVE_OBSERVER_API == 8u &&
           BISMUTH_CAP_COCOS_PASSIVE_HOOKS == 16u ? 0 : 1;
}
