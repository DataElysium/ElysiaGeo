#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace elysia::geo {
// Level zero is borrowed from the resident raster. Each owned level reduces
// 2x2 samples to their maximum. Missing data propagates as +infinity, so a
// partially covered query descends until unrelated missing samples are excluded.
class HeightPyramid {
    struct Level {
        std::uint32_t columns, rows;
        std::vector<float> maxima;
    };
    std::uint32_t columns_{}, rows_{};
    std::vector<Level> levels_;

  public:
    HeightPyramid() = default;
    template <class Read>
    HeightPyramid(std::uint32_t columns, std::uint32_t rows, const Read &read)
        : columns_(columns), rows_(rows) {
        while (columns > 1 || rows > 1) {
            const auto nx = columns / 2 + columns % 2, ny = rows / 2 + rows % 2;
            Level next{nx, ny, std::vector<float>(std::size_t(nx) * ny, -INFINITY)};
            for (std::uint32_t y = 0; y < rows; ++y)
                for (std::uint32_t x = 0; x < columns; ++x) {
                    const float value = levels_.empty()
                                            ? read(x, y)
                                            : levels_.back().maxima[std::size_t(y) * columns + x];
                    auto &maximum = next.maxima[std::size_t(y / 2) * nx + x / 2];
                    maximum = std::max(maximum, std::isfinite(value) ? value : INFINITY);
                }
            levels_.push_back(std::move(next));
            columns = nx;
            rows = ny;
        }
    }

    std::size_t bytes() const {
        std::size_t total = 0;
        for (const auto &level : levels_)
            total += level.maxima.capacity() * sizeof(float);
        return total;
    }

    // Inclusive source-sample rectangle. Fully contained nodes need one read;
    // only the rectangle boundary descends. stop_below allows a wider safe bound
    // or +infinity when clearance fails. Queries allocate no scratch storage.
    template <class Read>
    float maximum(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom,
                  const Read &read, double stop_below = -INFINITY) const {
        const auto visit = [&](auto &&self, std::size_t level, std::uint32_t x,
                               std::uint32_t y) -> float {
            const auto span = std::uint64_t{1} << level;
            const auto x0 = std::uint64_t(x) * span, y0 = std::uint64_t(y) * span;
            const auto x1 = std::min(x0 + span, std::uint64_t(columns_));
            const auto y1 = std::min(y0 + span, std::uint64_t(rows_));
            if (x0 > right || y0 > bottom || x1 <= left || y1 <= top)
                return -INFINITY;
            if (!level) {
                const auto value = read(x, y);
                return std::isfinite(value) ? value : INFINITY;
            }
            const auto &data = levels_[level - 1];
            const float bound = data.maxima[std::size_t(y) * data.columns + x];
            // Clearance can accept a wider bound already below its ceiling.
            if (bound < stop_below ||
                (left <= x0 && top <= y0 && right >= x1 - 1 && bottom >= y1 - 1))
                return bound;
            const auto nx = level == 1 ? columns_ : levels_[level - 2].columns;
            const auto ny = level == 1 ? rows_ : levels_[level - 2].rows;
            float maximum = -INFINITY;
            for (unsigned j = 0; j < 2; ++j)
                for (unsigned i = 0; i < 2; ++i)
                    if (std::uint64_t(x) * 2 + i < nx && std::uint64_t(y) * 2 + j < ny) {
                        maximum = std::max(maximum, self(self, level - 1, x * 2 + i, y * 2 + j));
                        if (maximum == INFINITY ||
                            (std::isfinite(stop_below) && maximum >= stop_below))
                            return INFINITY;
                    }
            return maximum;
        };
        // The highest differing coordinate bit gives the smallest dyadic square
        // covering the query. Local queries skip unrelated global ancestors.
        const auto level = std::bit_width((left ^ right) | (top ^ bottom));
        return visit(visit, level, std::uint64_t(left) >> level, std::uint64_t(top) >> level);
    }
};
} // namespace elysia::geo
