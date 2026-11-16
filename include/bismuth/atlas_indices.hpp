#pragma once
#include <cstdint>
#include <vector>

namespace bismuth {

// Geometry Dash 2.2081's inspected 16-bit atlas EBO uses four vertices
// and six indices per quad. 16,384 is the largest slot count addressable
// without wrapping a 16-bit vertex index.
constexpr std::uint32_t kAtlasMaxQuads16 = 16384u;

struct IndexedDrawCommand {
    std::uint32_t count;
    std::uint32_t instanceCount;
    std::uint32_t firstIndex;
    std::int32_t baseVertex;
    std::uint32_t reservedMustBeZero;
};
static_assert(sizeof(IndexedDrawCommand) == 20,
              "GLES indexed indirect command must contain five 32-bit words");

// Build the exact target EBO pattern for a stable, painter-ordered list of
// atlas slots. This is a CPU reference/validation helper, not the live GPU path.
inline bool buildAtlasElementIndices16(
    const std::vector<std::uint32_t>& visibleSlots,
    std::uint32_t atlasCapacity,
    std::vector<std::uint16_t>& out) {
    out.clear();
    if (atlasCapacity > kAtlasMaxQuads16 ||
        visibleSlots.size() > atlasCapacity) return false;

    out.reserve(visibleSlots.size() * 6u);
    for (const std::uint32_t slot : visibleSlots) {
        if (slot >= atlasCapacity) {
            out.clear();
            return false;
        }
        const std::uint32_t base = slot * 4u;
        out.push_back(static_cast<std::uint16_t>(base));
        out.push_back(static_cast<std::uint16_t>(base + 1u));
        out.push_back(static_cast<std::uint16_t>(base + 2u));
        out.push_back(static_cast<std::uint16_t>(base + 3u));
        out.push_back(static_cast<std::uint16_t>(base + 2u));
        out.push_back(static_cast<std::uint16_t>(base + 1u));
    }
    return true;
}

// Pack the same six 16-bit indices into three 32-bit words per visible quad.
// This mirrors atlas_indices.comp and keeps the GPU output compatible with
// GL_UNSIGNED_SHORT element fetches without requiring 16-bit SSBO support.
inline bool buildAtlasElementIndices16Packed(
    const std::vector<std::uint32_t>& visibleSlots,
    std::uint32_t atlasCapacity,
    std::vector<std::uint32_t>& out) {
    std::vector<std::uint16_t> indices;
    if (!buildAtlasElementIndices16(visibleSlots, atlasCapacity, indices)) {
        out.clear();
        return false;
    }
    out.clear();
    out.reserve(indices.size() / 2u);
    for (std::size_t i = 0; i < indices.size(); i += 2u) {
        out.push_back(static_cast<std::uint32_t>(indices[i]) |
                      (static_cast<std::uint32_t>(indices[i + 1u]) << 16u));
    }
    return true;
}

inline bool makeIndexedDrawCommand(
    std::uint32_t visibleQuadCount,
    IndexedDrawCommand& out) noexcept {
    if (visibleQuadCount > kAtlasMaxQuads16) {
        out = {};
        return false;
    }
    out = IndexedDrawCommand{
        visibleQuadCount * 6u, visibleQuadCount ? 1u : 0u, 0u, 0, 0u
    };
    return true;
}

} // namespace bismuth
