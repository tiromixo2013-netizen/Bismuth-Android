#pragma once
#include "bismuth/culling_types.hpp"
namespace bismuth {
// Reusable-output variants avoid per-frame vector allocation after warm-up.
void cullStableInto(const std::vector<Candidate>& candidates, const Rect& view,
                    std::vector<std::uint32_t>& visibleIndices);
void cullStablePayloadsInto(const std::vector<Candidate>& candidates, const Rect& view,
                            std::vector<std::uint32_t>& visiblePayloads);
CullResult cullStable(const std::vector<Candidate>& candidates, const Rect& view);
// Returns payloads for visible candidates in stable input/painter order.
std::vector<std::uint32_t> cullStablePayloads(const std::vector<Candidate>& candidates, const Rect& view);
}
