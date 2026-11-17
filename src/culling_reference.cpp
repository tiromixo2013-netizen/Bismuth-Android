#include "bismuth/culling_reference.hpp"
namespace bismuth {
CullResult cullStable(const std::vector<Candidate>& candidates, const Rect& view) {
    CullResult out;
    out.visibleIndices.reserve(candidates.size());
    for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        if (intersects(candidates[i].bounds, view)) out.visibleIndices.push_back(i);
    }
    return out;
}

std::vector<std::uint32_t> cullStablePayloads(const std::vector<Candidate>& candidates, const Rect& view) {
    std::vector<std::uint32_t> out;
    out.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        if (intersects(candidate.bounds, view)) out.push_back(candidate.payload);
    }
    return out;
}
}
