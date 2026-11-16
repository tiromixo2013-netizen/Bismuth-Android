#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace bismuth {

// Fixed-size, allocation-free rolling window of monotonic frame-start intervals.
// The caller must supply timestamps from one monotonic clock and one frame thread.
struct FrameIntervalSnapshot {
    std::uint32_t sampleCount = 0;
    std::uint64_t lastIntervalNs = 0;
    std::uint64_t meanIntervalNs = 0;
    std::uint64_t p50IntervalNs = 0;
    std::uint64_t p95IntervalNs = 0;
    std::uint64_t p99IntervalNs = 0;
};

class FrameIntervalWindow {
public:
    static constexpr std::size_t kWindowSize = 120;

    void reset() noexcept {
        samples_ = {};
        next_ = 0;
        count_ = 0;
        previousStartNs_ = 0;
        hasPrevious_ = false;
    }

    // Returns false for a non-monotonic timestamp. The first timestamp primes
    // the clock and does not create an interval sample.
    bool recordFrameStart(std::uint64_t monotonicNs) noexcept {
        if (!hasPrevious_) {
            previousStartNs_ = monotonicNs;
            hasPrevious_ = true;
            return true;
        }
        if (monotonicNs <= previousStartNs_) return false;
        samples_[next_] = monotonicNs - previousStartNs_;
        next_ = (next_ + 1u) % kWindowSize;
        if (count_ < kWindowSize) ++count_;
        previousStartNs_ = monotonicNs;
        return true;
    }

    FrameIntervalSnapshot snapshot() const noexcept {
        FrameIntervalSnapshot result{};
        result.sampleCount = static_cast<std::uint32_t>(count_);
        if (count_ == 0) return result;

        std::array<std::uint64_t, kWindowSize> sorted{};
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            sorted[i] = samples_[i];
            total += samples_[i];
        }
        std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count_));
        result.lastIntervalNs = samples_[(next_ + kWindowSize - 1u) % kWindowSize];
        result.meanIntervalNs = total / count_;
        result.p50IntervalNs = percentile(sorted, count_, 50u);
        result.p95IntervalNs = percentile(sorted, count_, 95u);
        result.p99IntervalNs = percentile(sorted, count_, 99u);
        return result;
    }

private:
    static std::uint64_t percentile(const std::array<std::uint64_t, kWindowSize>& values,
                                    std::size_t count, std::size_t percentileValue) noexcept {
        const std::size_t rank = (percentileValue * count + 99u) / 100u;
        const std::size_t index = rank == 0 ? 0 : rank - 1u;
        return values[index < count ? index : count - 1u];
    }

    std::array<std::uint64_t, kWindowSize> samples_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
    std::uint64_t previousStartNs_ = 0;
    bool hasPrevious_ = false;
};

} // namespace bismuth
