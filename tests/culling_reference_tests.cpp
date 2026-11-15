#include "bismuth/culling_reference.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
using namespace bismuth;
int main() {
    const Rect view{0, 0, 100, 100};
    std::vector<Candidate> candidates = {
        {{-10, 10, 10, 20}, 0}, {{110, 10, 120, 20}, 1},
        {{20, 20, 30, 30}, 2}, {{100, 100, 110, 110}, 3},
        {{40, 40, 40, 40}, 4}
    };
    auto result = cullStable(candidates, view);
    assert((result.visibleIndices == std::vector<std::uint32_t>{0, 2, 3, 4}));
    auto empty = cullStable({}, view);
    assert(empty.visibleIndices.empty());
    const Rect inverted{10, 10, 0, 0};
    auto none = cullStable(candidates, inverted);
    assert(none.visibleIndices.empty());
    const Rect touching{10, 10, 10, 20};
    auto edge = cullStable(candidates, touching);
    assert((edge.visibleIndices == std::vector<std::uint32_t>{0}));
    std::vector<Candidate> ordered = {
        {{0, 0, 1, 1}, 9}, {{2, 2, 3, 3}, 8}, {{4, 4, 5, 5}, 7}
    };
    auto stable = cullStable(ordered, Rect{-1, -1, 10, 10});
    assert((stable.visibleIndices == std::vector<std::uint32_t>{0, 1, 2}));
    // Invalid and non-finite bounds must be rejected consistently with the GPU classifier.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<Candidate> invalidBounds = {
        {{5, 5, 1, 1}, 10}, {{nan, 0, 10, 10}, 11},
        {{0, 0, 10, 10}, 12}
    };
    auto validOnly = cullStable(invalidBounds, view);
    assert((validOnly.visibleIndices == std::vector<std::uint32_t>{2}));
    auto invalidView = cullStable(candidates, Rect{nan, 0, 100, 100});
    assert(invalidView.visibleIndices.empty());
    std::cout << "Bismuth-Android reference tests passed\n";
}
