#pragma once

#include "bismuth/culling_types.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace bismuth {

struct QuadPosition3 {
    float x, y, z;
};

// OpenGL column-major matrix, matching glUniformMatrix4fv(..., GL_FALSE, ...).
using ClipMatrix4 = std::array<float, 16>;

// Convert the four already-transformed quad vertices to a conservative NDC
// rectangle. This helper deliberately does not read Cocos objects or GL
// buffers: callers must supply a verified matrix and the exact four positions.
//
// If projection is numerically unsafe (non-finite values or a vertex at/behind
// the eye plane), return the full NDC view. That may miss an optimization but
// cannot incorrectly cull a quad because of a bad perspective divide.
inline Bounds conservativeQuadClipBounds(
    const std::array<QuadPosition3, 4>& vertices,
    const ClipMatrix4& mvp) noexcept {
    constexpr float kMinW = 1.0e-6f;
    for (float value : mvp) {
        if (!std::isfinite(value)) return Bounds{-1.0f, -1.0f, 1.0f, 1.0f};
    }

    Bounds result{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    for (const QuadPosition3& p : vertices) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return Bounds{-1.0f, -1.0f, 1.0f, 1.0f};

        const float x = mvp[0] * p.x + mvp[4] * p.y + mvp[8] * p.z + mvp[12];
        const float y = mvp[1] * p.x + mvp[5] * p.y + mvp[9] * p.z + mvp[13];
        const float w = mvp[3] * p.x + mvp[7] * p.y + mvp[11] * p.z + mvp[15];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || w <= kMinW)
            return Bounds{-1.0f, -1.0f, 1.0f, 1.0f};

        const float ndcX = x / w;
        const float ndcY = y / w;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
            return Bounds{-1.0f, -1.0f, 1.0f, 1.0f};

        if (ndcX < result.left) result.left = ndcX;
        if (ndcX > result.right) result.right = ndcX;
        if (ndcY < result.bottom) result.bottom = ndcY;
        if (ndcY > result.top) result.top = ndcY;
    }
    return result;
}

} // namespace bismuth
