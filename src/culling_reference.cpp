#include "bismuth/culling_reference.hpp"
namespace bismuth {
CullResult cullStable(const std::vector<Candidate>& candidates, const Rect& view) {
    CullResult out;
    if (!isValidRect(view)) return out;
    out.visibleIndices.reserve(candidates.size());
    for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        if (intersects(candidates[i].bounds, view)) out.visibleIndices.push_back(i);
    }
    return out;
}
}
