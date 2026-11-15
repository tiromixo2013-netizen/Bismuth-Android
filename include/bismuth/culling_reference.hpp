#pragma once
#include "bismuth/culling_types.hpp"
namespace bismuth {
CullResult cullStable(const std::vector<Candidate>& candidates, const Rect& view);
}
