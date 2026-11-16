#pragma once

#include <cstdint>

namespace bismuth {

// Grow SSBO storage geometrically in whole compute workgroups. This helper is
// intentionally pure so its bounds can be tested on the host without GLES.
constexpr std::uint32_t nextWorkgroupCapacity(std::uint32_t currentGroups,
                                               std::uint32_t requiredGroups,
                                               std::uint32_t maximumGroups) noexcept {
    if (requiredGroups == 0u || maximumGroups == 0u) return 0u;
    if (requiredGroups > maximumGroups) return 0u;
    if (currentGroups >= requiredGroups) return currentGroups <= maximumGroups ? currentGroups : 0u;
    if (currentGroups == 0u) return requiredGroups;
    const std::uint32_t doubled = currentGroups > maximumGroups / 2u
        ? maximumGroups : currentGroups * 2u;
    return doubled > requiredGroups ? doubled : requiredGroups;
}

} // namespace bismuth
