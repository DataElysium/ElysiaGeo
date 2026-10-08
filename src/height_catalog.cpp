#include <algorithm>
#include <cmath>
#include <elysia_geo/height_catalog.hpp>
#include <elysia_geo/map.hpp>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace elysia::geo {
struct HeightCatalog::Impl {
    using Key = std::tuple<std::size_t, std::uint32_t, std::uint32_t>;
    struct Entry {
        std::shared_ptr<const HeightTile> tile;
        std::list<Key>::iterator recent;
    };
    std::vector<Heightmap> maps;
    std::vector<HeightLayer> layers;
    std::size_t budget{}, bytes{};
    std::map<Key, Entry> cache;
    std::list<Key> recency;
    mutable std::mutex mutex;

    std::shared_ptr<const HeightTile> tile(std::size_t layer, std::uint32_t x, std::uint32_t y) {
        Key key{layer, x, y};
        if (auto it = cache.find(key); it != cache.end()) {
            recency.splice(recency.begin(), recency, it->second.recent);
            return it->second.tile;
        }
        auto data = std::make_shared<HeightTile>(maps[layer].read_tile(x, y));
        auto size = data->metres.size() * sizeof(float);
        // An oversized tile can be read transiently, but never retained over
        // budget.
        if (size > budget)
            return data;
        while (bytes > budget - size) {
            auto it = cache.find(recency.back());
            bytes -= it->second.tile->metres.size() * sizeof(float);
            cache.erase(it);
            recency.pop_back();
        }
        recency.push_front(key);
        try {
            cache.emplace(key, Entry{data, recency.begin()});
        } catch (...) {
            recency.pop_front();
            throw;
        }
        bytes += size;
        return data;
    }
    std::optional<CatalogSample> sample(Position p) {
        validate(p);
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            const auto &g = layers[layer].grid;
            double lon = wrap_longitude(p.longitude_deg);
            lon += 360 * std::ceil((g.west - lon) / 360);
            double x = (lon - g.west) / g.longitude_step;
            double y = (g.north - p.latitude_deg) / g.latitude_step;
            if (x < 0 || y < 0 || x > g.columns - 1 || y > g.rows - 1)
                continue;
            auto x0 = static_cast<std::uint32_t>(x), y0 = static_cast<std::uint32_t>(y);
            double value = 0;
            bool missing = false;
            for (unsigned j = 0; j < 2; ++j)
                for (unsigned i = 0; i < 2; ++i) {
                    double weight = (i ? x - x0 : 1 - x + x0) * (j ? y - y0 : 1 - y + y0);
                    if (weight == 0)
                        continue;
                    auto xx = std::min(x0 + i, g.columns - 1), yy = std::min(y0 + j, g.rows - 1);
                    auto data = tile(layer, xx / g.tile_size, yy / g.tile_size);
                    auto z = data->metres[std::size_t(yy % g.tile_size) * data->columns +
                                          xx % g.tile_size];
                    if (std::isnan(z)) {
                        missing = true;
                        continue;
                    }
                    value += weight * z;
                }
            if (!missing)
                return CatalogSample{{value, g.longitude_step, g.latitude_step, g.reference},
                                     layer};
        }
        return {};
    }
};
HeightCatalog::HeightCatalog(std::vector<std::filesystem::path> files, HeightReference reference,
                             std::size_t cache_bytes)
    : impl_(std::make_unique<Impl>()) {
    if (reference == HeightReference::unspecified || static_cast<unsigned>(reference) > 4)
        throw std::invalid_argument("Height catalogue requires a known vertical reference");
    impl_->budget = cache_bytes;
    for (const auto &file : files) {
        auto map = Heightmap::open(file);
        if (map.grid().reference != reference)
            throw std::invalid_argument("Height catalogue vertical reference mismatch: " +
                                        file.string());
        impl_->layers.push_back({std::filesystem::absolute(file), map.grid()});
    }
    std::stable_sort(impl_->layers.begin(), impl_->layers.end(), [](const auto &a, const auto &b) {
        auto area = [](const auto &g) { return g.longitude_step * g.latitude_step; };
        auto ax = area(a.grid), bx = area(b.grid);
        if (ax != bx)
            return ax < bx;
        return double(a.grid.columns) * a.grid.rows < double(b.grid.columns) * b.grid.rows;
    });
    for (const auto &layer : impl_->layers)
        impl_->maps.push_back(Heightmap::open(layer.source));
}
HeightCatalog::~HeightCatalog() = default;
HeightCatalog::HeightCatalog(HeightCatalog &&) noexcept = default;
HeightCatalog &HeightCatalog::operator=(HeightCatalog &&) noexcept = default;
const std::vector<HeightLayer> &HeightCatalog::layers() const { return impl_->layers; }
std::optional<CatalogSample> HeightCatalog::sample(Position p) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->sample(p);
}
std::vector<std::optional<CatalogSample>>
HeightCatalog::sample(std::span<const Position> points) const {
    std::lock_guard lock(impl_->mutex);
    std::vector<std::optional<CatalogSample>> result;
    result.reserve(points.size());
    for (auto p : points)
        result.push_back(impl_->sample(p));
    return result;
}
HeightCacheStats HeightCatalog::cache_stats() const {
    std::lock_guard lock(impl_->mutex);
    return {impl_->cache.size(), impl_->bytes, impl_->budget};
}
std::optional<double> HeightCatalog::upper_bound(Bounds bounds) const {
    if (!std::isfinite(bounds.west) || !std::isfinite(bounds.east) ||
        !std::isfinite(bounds.south) || !std::isfinite(bounds.north) ||
        bounds.south < -90 || bounds.north > 90 || bounds.south > bounds.north ||
        bounds.west < -180 || bounds.west > 180 || bounds.east < -180 || bounds.east > 180)
        throw std::invalid_argument("Invalid height bounds");
    std::lock_guard lock(impl_->mutex);
    const std::vector<std::pair<double, double>> intervals = bounds.west <= bounds.east
        ? std::vector<std::pair<double, double>>{{bounds.west, bounds.east}}
        : std::vector<std::pair<double, double>>{{bounds.west, 180}, {-180, bounds.east}};
    double maximum = -INFINITY;
    for (const auto& [west, east] : intervals) {
        bool covered = false;
        for (std::size_t layer = 0; layer < impl_->layers.size(); ++layer) {
            const auto& g = impl_->layers[layer].grid;
            const double right = g.west + (g.columns - 1) * g.longitude_step;
            const double bottom = g.north - (g.rows - 1) * g.latitude_step;
            if (bounds.north < bottom || bounds.south > g.north) continue;
            for (int shift : {-360, 0, 360}) {
                const double lo = west + shift, hi = east + shift;
                if (hi < g.west || lo > right) continue;
                const auto x0 = std::uint32_t(std::clamp(std::floor((lo - g.west) / g.longitude_step), 0., double(g.columns - 1)));
                const auto x1 = std::uint32_t(std::clamp(std::ceil((hi - g.west) / g.longitude_step), 0., double(g.columns - 1)));
                const auto y0 = std::uint32_t(std::clamp(std::floor((g.north - bounds.north) / g.latitude_step), 0., double(g.rows - 1)));
                const auto y1 = std::uint32_t(std::clamp(std::ceil((g.north - bounds.south) / g.latitude_step), 0., double(g.rows - 1)));
                for (auto y = y0 / g.tile_size; y <= y1 / g.tile_size; ++y)
                    for (auto x = x0 / g.tile_size; x <= x1 / g.tile_size; ++x) {
                        const auto tile = impl_->tile(layer, x, y);
                        for (float value : tile->metres) {
                            if (!std::isfinite(value)) return {};
                            maximum = std::max(maximum, double(value));
                        }
                    }
                covered |= lo >= g.west && hi <= right && bounds.south >= bottom && bounds.north <= g.north;
            }
        }
        if (!covered) return {};
    }
    return std::isfinite(maximum) ? std::optional<double>{maximum} : std::nullopt;
}
} // namespace elysia::geo
