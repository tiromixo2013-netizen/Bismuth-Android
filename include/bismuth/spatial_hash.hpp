#pragma once

#include "bismuth/culling_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bismuth {

// Dynamic 2D broad-phase index for conservative visibility queries.
// IDs are returned in insertion order so callers can preserve painter order.
// Objects spanning too many cells are kept in an overflow set and tested on
// every query; huge queries fall back to a linear scan instead of iterating
// an unbounded number of cells.
class SpatialHash final {
public:
    explicit SpatialHash(float cellSize = 256.0f,
                         std::uint64_t maxCellsPerEntry = 256u,
                         std::uint64_t maxCellsPerQuery = 4096u) noexcept
        : cellSize_(std::isfinite(cellSize) && cellSize > 0.0f ? cellSize : 256.0f),
          maxCellsPerEntry_(maxCellsPerEntry ? maxCellsPerEntry : 1u),
          maxCellsPerQuery_(maxCellsPerQuery ? maxCellsPerQuery : 1u) {}

    bool insert(std::uint32_t id, const Bounds& bounds) {
        if (entries_.find(id) != entries_.end()) return update(id, bounds);
        Entry entry{};
        entry.bounds = bounds;
        entry.order = nextOrder_++;
        if (!isValidBounds(bounds) || !makeKeys(bounds, entry.keys, maxCellsPerEntry_)) {
            entry.overflow = true;
            overflow_.insert(id);
        } else {
            addToCells(id, entry.keys);
        }
        entries_.emplace(id, std::move(entry));
        return true;
    }

    bool update(std::uint32_t id, const Bounds& bounds) {
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        eraseMembership(id, it->second);
        it->second.bounds = bounds;
        it->second.keys.clear();
        it->second.overflow = !isValidBounds(bounds) || !makeKeys(bounds, it->second.keys, maxCellsPerEntry_);
        if (it->second.overflow) overflow_.insert(id);
        else addToCells(id, it->second.keys);
        return true;
    }

    bool remove(std::uint32_t id) {
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        eraseMembership(id, it->second);
        entries_.erase(it);
        return true;
    }

    void clear() noexcept {
        cells_.clear();
        entries_.clear();
        overflow_.clear();
        nextOrder_ = 0;
    }

    std::size_t size() const noexcept { return entries_.size(); }

    void query(const Rect& view, std::vector<std::uint32_t>& out) const {
        out.clear();
        if (entries_.empty()) return;

        std::vector<std::uint32_t> candidates;
        std::uint64_t queryCellCount = 0;
        std::vector<CellKey> keys;
        if (!isValidRect(view) || !makeKeys(
                Bounds{view.left, view.bottom, view.right, view.top}, keys,
                maxCellsPerQuery_, &queryCellCount)) {
            appendAll(candidates);
        } else {
            candidates.reserve(overflow_.size() + 32u);
            for (std::uint32_t id : overflow_) candidates.push_back(id);
            for (const CellKey& key : keys) {
                const auto cell = cells_.find(key);
                if (cell != cells_.end())
                    candidates.insert(candidates.end(), cell->second.begin(), cell->second.end());
            }
        }

        // De-duplicate candidates and apply exact AABB intersection.
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(candidates.size());
        for (std::uint32_t id : candidates) {
            if (!seen.insert(id).second) continue;
            const auto it = entries_.find(id);
            if (it != entries_.end() && intersects(it->second.bounds, view))
                out.push_back(id);
        }

        std::sort(out.begin(), out.end(), [this](std::uint32_t a, std::uint32_t b) {
            return entries_.at(a).order < entries_.at(b).order;
        });
    }

private:
    struct CellKey {
        std::int64_t x, y;
        bool operator==(const CellKey& other) const noexcept {
            return x == other.x && y == other.y;
        }
    };
    struct CellHash {
        std::size_t operator()(const CellKey& key) const noexcept {
            const auto a = static_cast<std::uint64_t>(key.x);
            const auto b = static_cast<std::uint64_t>(key.y);
            std::uint64_t h = a + 0x9e3779b97f4a7c15ull;
            h ^= b + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
            return static_cast<std::size_t>(h);
        }
    };
    struct Entry {
        Bounds bounds{};
        std::vector<CellKey> keys;
        std::uint64_t order = 0;
        bool overflow = false;
    };

    bool makeKeys(const Bounds& b, std::vector<CellKey>& out,
                  std::uint64_t limit = std::numeric_limits<std::uint64_t>::max(),
                  std::uint64_t* countOut = nullptr) const {
        out.clear();
        if (!isValidBounds(b)) return false;
        const long double inv = 1.0L / static_cast<long double>(cellSize_);
        const long double minXd = std::floor(static_cast<long double>(b.left) * inv);
        const long double maxXd = std::floor(static_cast<long double>(b.right) * inv);
        const long double minYd = std::floor(static_cast<long double>(b.bottom) * inv);
        const long double maxYd = std::floor(static_cast<long double>(b.top) * inv);
        constexpr long double lo =
            static_cast<long double>(std::numeric_limits<std::int64_t>::min());
        constexpr long double hi =
            static_cast<long double>(std::numeric_limits<std::int64_t>::max());
        if (minXd < lo || maxXd > hi || minYd < lo || maxYd > hi) return false;
        const auto minX = static_cast<std::int64_t>(minXd);
        const auto maxX = static_cast<std::int64_t>(maxXd);
        const auto minY = static_cast<std::int64_t>(minYd);
        const auto maxY = static_cast<std::int64_t>(maxYd);
        const long double nxWide =
            static_cast<long double>(maxX) - static_cast<long double>(minX) + 1.0L;
        const long double nyWide =
            static_cast<long double>(maxY) - static_cast<long double>(minY) + 1.0L;
        if (nxWide < 1.0L || nyWide < 1.0L ||
            nxWide > static_cast<long double>(limit) ||
            nyWide > static_cast<long double>(limit)) return false;
        const auto nx = static_cast<std::uint64_t>(nxWide);
        const auto ny = static_cast<std::uint64_t>(nyWide);
        if (nx > limit / ny) return false;
        const std::uint64_t count = nx * ny;
        if (countOut) *countOut = count;
        out.reserve(static_cast<std::size_t>(count));
        for (std::int64_t y = minY;; ++y) {
            for (std::int64_t x = minX;; ++x) {
                out.push_back(CellKey{x, y});
                if (x == maxX) break;
            }
            if (y == maxY) break;
        }
        return true;
    }

    void addToCells(std::uint32_t id, const std::vector<CellKey>& keys) {
        for (const CellKey& key : keys) cells_[key].push_back(id);
    }

    void eraseMembership(std::uint32_t id, const Entry& entry) {
        if (entry.overflow) {
            overflow_.erase(id);
            return;
        }
        for (const CellKey& key : entry.keys) {
            auto cell = cells_.find(key);
            if (cell == cells_.end()) continue;
            auto& ids = cell->second;
            ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
            if (ids.empty()) cells_.erase(cell);
        }
    }

    void appendAll(std::vector<std::uint32_t>& out) const {
        out.reserve(entries_.size());
        for (const auto& item : entries_) out.push_back(item.first);
    }

    float cellSize_;
    std::uint64_t maxCellsPerEntry_;
    std::uint64_t maxCellsPerQuery_;
    std::uint64_t nextOrder_ = 0;
    std::unordered_map<CellKey, std::vector<std::uint32_t>, CellHash> cells_;
    std::unordered_map<std::uint32_t, Entry> entries_;
    std::unordered_set<std::uint32_t> overflow_;
};

} // namespace bismuth
