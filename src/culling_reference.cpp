#include "bismuth/culling_reference.hpp"

namespace bismuth {

void cullStableInto(const std::vector<Candidate>& candidates, const Rect& view,
                    std::vector<std::uint32_t>& visibleIndices) {
    visibleIndices.clear();
    if (visibleIndices.capacity() < candidates.size())
        visibleIndices.reserve(candidates.size());
    for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        if (intersects(candidates[i].bounds, view))
            visibleIndices.push_back(i);
    }
}

void cullStablePayloadsInto(const std::vector<Candidate>& candidates, const Rect& view,
                            std::vector<std::uint32_t>& visiblePayloads) {
    visiblePayloads.clear();
    if (visiblePayloads.capacity() < candidates.size())
        visiblePayloads.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        if (intersects(candidate.bounds, view))
            visiblePayloads.push_back(candidate.payload);
    }
}

CullResult cullStable(const std::vector<Candidate>& candidates, const Rect& view) {
    CullResult out;
    cullStableInto(candidates, view, out.visibleIndices);
    return out;
}

std::vector<std::uint32_t> cullStablePayloads(const std::vector<Candidate>& candidates, const Rect& view) {
    std::vector<std::uint32_t> out;
    cullStablePayloadsInto(candidates, view, out);
    return out;
}

} // namespace bismuth
