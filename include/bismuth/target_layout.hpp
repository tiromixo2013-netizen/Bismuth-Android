#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bismuth::target_layout {

// Offsets verified from the uploaded ARM64 target's CCSpriteBatchNode::draw()
// disassembly. These are valid only after the module Build ID gate succeeds.
constexpr std::size_t kBatchNodeChildrenArrayOffset = 0x148u;
constexpr std::size_t kArrayCountOffset = 0x38u;
constexpr std::uint32_t kMaxObservedBatchChildren = 1u << 20u;

// Reads only the child-array pointer and its count; it does not traverse or
// dereference individual child objects. Returns false for null or implausible
// layouts so telemetry fails closed instead of reporting a fabricated count.
inline bool readBatchChildCount(const void* batchNode, std::uint32_t& count) noexcept {
    count = 0;
    if (!batchNode) return false;

    const auto* nodeBytes = static_cast<const std::uint8_t*>(batchNode);
    const void* childArray = nullptr;
    std::memcpy(&childArray, nodeBytes + kBatchNodeChildrenArrayOffset, sizeof(childArray));
    if (!childArray) return false;

    std::uint32_t observed = 0;
    const auto* arrayBytes = static_cast<const std::uint8_t*>(childArray);
    std::memcpy(&observed, arrayBytes + kArrayCountOffset, sizeof(observed));
    if (observed > kMaxObservedBatchChildren) return false;

    count = observed;
    return true;
}

} // namespace bismuth::target_layout
