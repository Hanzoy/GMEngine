#pragma once
#include "ui_types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>
namespace gm {
// Conservative world-space broad phase. Results retain the caller's painter order.
class SpatialGrid {
  public:
    struct Query {
        std::vector<uint32_t> rows;
        uint32_t candidates = 0, cells = 0;
        bool fallback = false;
    };
    void clear() {
        cells_.clear();
        large_.clear();
        bounds_.clear();
        marks_.clear();
        serial_ = 0;
    }
    void set(uint32_t row, Rect bounds) {
        if (row >= bounds_.size()) {
            bounds_.resize(size_t(row) + 1);
            marks_.resize(size_t(row) + 1);
        }
        if (bounds_[row].w > 0 && bounds_[row].h > 0)
            visit(
                bounds_[row],
                [&](uint64_t key) {
                    auto it = cells_.find(key);
                    if (it != cells_.end()) {
                        std::erase(it->second, row);
                        if (it->second.empty())
                            cells_.erase(it);
                    }
                },
                [&] { std::erase(large_, row); });
        bounds_[row] = bounds;
        if (bounds.w > 0 && bounds.h > 0)
            visit(bounds, [&](uint64_t key) { cells_[key].push_back(row); }, [&] { large_.push_back(row); });
    }
    Query query(Rect area) const {
        Query result;
        if (area.w <= 0 || area.h <= 0)
            return result;
        if (++serial_ == 0) {
            std::fill(marks_.begin(), marks_.end(), 0);
            serial_ = 1;
        }
        auto add = [&](uint32_t row) {
            if (marks_[row] == serial_)
                return;
            marks_[row] = serial_;
            ++result.candidates;
            auto hit = intersect(bounds_[row], area);
            if (hit.w > 0 && hit.h > 0)
                result.rows.push_back(row);
        };
        auto range = coordinates(area);
        if (!range.valid || range.count > 4096) {
            result.fallback = true;
            for (uint32_t row = 0; row < bounds_.size(); ++row)
                add(row);
        } else {
            for (int64_t y = range.y0; y <= range.y1; ++y)
                for (int64_t x = range.x0; x <= range.x1; ++x) {
                    ++result.cells;
                    auto it = cells_.find(key(int32_t(x), int32_t(y)));
                    if (it != cells_.end())
                        for (auto row : it->second)
                            add(row);
                }
            for (auto row : large_)
                add(row);
        }
        std::sort(result.rows.begin(), result.rows.end());
        return result;
    }

  private:
    struct Range {
        int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint64_t count = 0;
        bool valid = false;
    };
    static Range coordinates(Rect b) {
        Range r;
        double values[] = {std::floor(double(b.x) / 256), std::floor(double(b.y) / 256),
                           std::floor((double(b.x) + b.w) / 256), std::floor((double(b.y) + b.h) / 256)};
        for (auto v : values)
            if (!std::isfinite(v) || v < INT32_MIN || v > INT32_MAX)
                return r;
        r.x0 = int32_t(values[0]);
        r.y0 = int32_t(values[1]);
        r.x1 = int32_t(values[2]);
        r.y1 = int32_t(values[3]);
        double count = (double(r.x1) - r.x0 + 1) * (double(r.y1) - r.y0 + 1);
        r.valid = count > 0 && count <= double(UINT32_MAX);
        if (r.valid)
            r.count = uint64_t(count);
        return r;
    }
    static uint64_t key(int32_t x, int32_t y) {
        return uint32_t(x) | (uint64_t(uint32_t(y)) << 32);
    }
    template <class F, class G> static void visit(Rect b, F cell, G large) {
        auto r = coordinates(b);
        if (!r.valid || r.count > 64) {
            large();
            return;
        }
        for (int64_t y = r.y0; y <= r.y1; ++y)
            for (int64_t x = r.x0; x <= r.x1; ++x)
                cell(key(int32_t(x), int32_t(y)));
    }
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells_;
    std::vector<uint32_t> large_;
    std::vector<Rect> bounds_;
    mutable std::vector<uint32_t> marks_;
    mutable uint32_t serial_ = 0;
};
} // namespace gm
