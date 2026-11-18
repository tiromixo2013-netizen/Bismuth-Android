#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bismuth::target_layout {

// Offsets verified from the uploaded ARM64 target's CCSpriteBatchNode::draw()
// disassembly. These are valid only after the module Build ID gate succeeds.
// CCSpriteBatchNode +0xd0 -> CCArray object; CCArray +0x38 -> _ccArray data;
// _ccArray +0x00 -> element count.
constexpr std::size_t kBatchNodeChildrenArrayObjectOffset = 0xd0u;
constexpr std::size_t kCCArrayDataOffset = 0x38u;
constexpr std::size_t kArrayCountOffset = 0x00u;
constexpr std::uint32_t kMaxObservedBatchChildren = 1u << 20u;

// Reads only the child-array metadata; it does not traverse or dereference
// individual child objects. Returns false for null or implausible layouts.
inline bool readBatchChildCount(const void* batchNode, std::uint32_t& count) noexcept {
    count = 0;
    if (!batchNode) return false;

    const auto* nodeBytes = static_cast<const std::uint8_t*>(batchNode);
    const void* childrenArrayObject = nullptr;
    std::memcpy(&childrenArrayObject,
                nodeBytes + kBatchNodeChildrenArrayObjectOffset,
                sizeof(childrenArrayObject));
    if (!childrenArrayObject) return false;

    const auto* arrayObjectBytes = static_cast<const std::uint8_t*>(childrenArrayObject);
    const void* arrayData = nullptr;
    std::memcpy(&arrayData, arrayObjectBytes + kCCArrayDataOffset, sizeof(arrayData));
    if (!arrayData) return false;

    std::uint32_t observed = 0;
    const auto* arrayDataBytes = static_cast<const std::uint8_t*>(arrayData);
    std::memcpy(&observed, arrayDataBytes + kArrayCountOffset, sizeof(observed));
    if (observed > kMaxObservedBatchChildren) return false;

    count = observed;
    return true;
}

} // namespace bismuth::target_layout
