#pragma once

#include <atomic>
#include <cstdint>

namespace bismuth {

// A successful observer acquisition increments the active count exactly once.
// A null acquisition must not decrement it: no reference was acquired.
inline void releaseObserverCallback(std::atomic<std::uint32_t>& activeCallbacks,
                                    const void* observer) noexcept {
    if (observer)
        activeCallbacks.fetch_sub(1, std::memory_order_release);
}

} // namespace bismuth
