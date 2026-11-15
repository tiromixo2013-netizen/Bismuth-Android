#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
namespace bismuth {
struct Rect { float left, bottom, right, top; };
struct Bounds { float left, bottom, right, top; };
// 32-byte std430-compatible record: vec4 bounds + four uint32 fields.
struct Candidate { Bounds bounds; std::uint32_t payload; std::uint32_t pad0 = 0, pad1 = 0, pad2 = 0; };
static_assert(sizeof(Candidate) == 32, "Candidate must match the std430 shader record layout");
struct CullResult { std::vector<std::uint32_t> visibleIndices; };
inline bool isValidBounds(const Bounds& b) noexcept {
    return std::isfinite(b.left) && std::isfinite(b.bottom) &&
           std::isfinite(b.right) && std::isfinite(b.top) &&
           b.left <= b.right && b.bottom <= b.top;
}
inline bool isValidRect(const Rect& r) noexcept {
    return std::isfinite(r.left) && std::isfinite(r.bottom) &&
           std::isfinite(r.right) && std::isfinite(r.top) &&
           r.left <= r.right && r.bottom <= r.top;
}
inline bool intersects(const Bounds& a, const Rect& r) noexcept {
    if (!isValidBounds(a) || !isValidRect(r)) return false;
    return a.right >= r.left && a.left <= r.right &&
           a.top >= r.bottom && a.bottom <= r.top;
}
}
