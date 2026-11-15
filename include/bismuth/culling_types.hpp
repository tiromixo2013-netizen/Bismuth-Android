#pragma once
#include <cstdint>
#include <vector>
namespace bismuth {
struct Rect { float left, bottom, right, top; };
struct Bounds { float left, bottom, right, top; };
// 32-byte std430-compatible record: vec4 bounds + four uint32 fields.
struct Candidate { Bounds bounds; std::uint32_t payload; std::uint32_t pad0 = 0, pad1 = 0, pad2 = 0; };
static_assert(sizeof(Candidate) == 32, "Candidate must match the std430 shader record layout");
struct CullResult { std::vector<std::uint32_t> visibleIndices; };
inline bool intersects(const Bounds& a, const Rect& r) noexcept {
    return a.right >= r.left && a.left <= r.right &&
           a.top >= r.bottom && a.bottom <= r.top;
}
}
