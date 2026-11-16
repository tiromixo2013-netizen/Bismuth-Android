#pragma once

#include <cstdint>

namespace bismuth {

// Host-side cache validity tracker for a future live atlas observer.
// It does not hook or inspect the game's objects by itself.
struct AtlasRevision {
    std::uint64_t slots = 0;
    std::uint64_t content = 0;
};

class AtlasRevisionTracker {
public:
    // Call after insert/remove/move/reorder/resize/remove-all operations.
    // Structural changes invalidate both slot mappings and quad-derived data.
    void recordStructuralMutation() noexcept {
        ++revision_.slots;
        ++revision_.content;
    }

    // Call after a quad is rewritten in-place without changing its atlas slot.
    void recordQuadWrite() noexcept {
        ++revision_.content;
    }

    AtlasRevision snapshot() const noexcept { return revision_; }

    bool slotMappingCurrent(AtlasRevision snapshot) const noexcept {
        return snapshot.slots == revision_.slots;
    }

    bool quadDataCurrent(AtlasRevision snapshot) const noexcept {
        return snapshot.slots == revision_.slots &&
               snapshot.content == revision_.content;
    }

private:
    AtlasRevision revision_{1, 1};
};

} // namespace bismuth
