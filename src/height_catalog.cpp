#include <algorithm>
#include <array>
#include <cmath>
#include <elysia_geo/height_catalog.hpp>
#include <elysia_geo/map.hpp>
#include <limits>
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
        std::optional<double> maximum;
    };
    std::vector<Heightmap> maps;
    std::vector<HeightLayer> layers;
    std::size_t budget{}, bytes{};
    std::map<Key, Entry> cache;
    std::list<Key> recency;
    mutable std::mutex mutex;
    struct ResidentLayer {
        std::vector<HeightTile> tiles;
        std::vector<std::optional<double>> maxima;
    };
    std::optional<std::vector<ResidentLayer>> resident;

    std::size_t tile_index(std::size_t layer, std::uint32_t x, std::uint32_t y) const {
        const auto &g = layers[layer].grid;
        const auto columns = (std::size_t(g.columns) + g.tile_size - 1) / g.tile_size;
        return std::size_t(y) * columns + x;
    }
    // The owner keeps resident tiles alive. Cached reads retain a temporary
    // shared reference.
    const HeightTile &read_tile(std::size_t layer, std::uint32_t x, std::uint32_t y,
                                std::shared_ptr<const HeightTile> &held) {
        if (resident)
            return (*resident)[layer].tiles[tile_index(layer, x, y)];
        held = tile(layer, x, y);
        return *held;
    }

    static std::optional<double> tile_maximum(const HeightTile &tile) {
        double maximum = -INFINITY;
        for (float value : tile.metres) {
            if (!std::isfinite(value))
                return {};
            maximum = std::max(maximum, double(value));
        }
        return maximum;
    }
    std::shared_ptr<const HeightTile> tile(std::size_t layer, std::uint32_t x, std::uint32_t y) {
        Key key{layer, x, y};
        if (auto it = cache.find(key); it != cache.end()) {
            recency.splice(recency.begin(), recency, it->second.recent);
            return it->second.tile;
        }
        auto data = std::make_shared<HeightTile>(maps[layer].read_tile(x, y));
        const auto maximum = tile_maximum(*data);
        auto size = data->metres.capacity() * sizeof(float);
        // An oversized tile can be read transiently, but never retained over
        // budget.
        if (size > budget)
            return data;
        while (bytes > budget - size) {
            auto it = cache.find(recency.back());
            bytes -= it->second.tile->metres.capacity() * sizeof(float);
            cache.erase(it);
            recency.pop_back();
        }
        recency.push_front(key);
        try {
            cache.emplace(key, Entry{data, recency.begin(), maximum});
        } catch (...) {
            recency.pop_front();
            throw;
        }
        bytes += size;
        return data;
    }
    // Bilinear extrema occur at clipped cell corners and internal grid
    // vertices.
    std::optional<double> clipped_maximum(std::size_t layer, double xmin, double ymin, double xmax,
                                          double ymax) {
        const auto &g = layers[layer].grid;
        const auto x0 = std::uint32_t(std::floor(xmin)), x1 = std::uint32_t(std::ceil(xmax));
        const auto y0 = std::uint32_t(std::floor(ymin)), y1 = std::uint32_t(std::ceil(ymax));
        double maximum = -INFINITY;
        std::shared_ptr<const HeightTile> held;
        const HeightTile *current = nullptr;
        std::uint32_t tile_x = 0, tile_y = 0;
        for (auto yy = y0; yy <= y1; ++yy)
            for (auto xx = x0; xx <= x1; ++xx) {
                const double px = std::clamp(double(xx), xmin, xmax),
                             py = std::clamp(double(yy), ymin, ymax);
                const auto ix = std::uint32_t(px), iy = std::uint32_t(py);
                double value = 0, magnitude = 0;
                for (unsigned j = 0; j < 2; ++j)
                    for (unsigned i = 0; i < 2; ++i) {
                        const double weight =
                            (i ? px - ix : 1 - px + ix) * (j ? py - iy : 1 - py + iy);
                        if (weight == 0)
                            continue;
                        const auto sx = std::min(ix + i, g.columns - 1),
                                   sy = std::min(iy + j, g.rows - 1);
                        if (!current || tile_x != sx / g.tile_size || tile_y != sy / g.tile_size) {
                            tile_x = sx / g.tile_size;
                            tile_y = sy / g.tile_size;
                            current = &read_tile(layer, tile_x, tile_y, held);
                        }
                        const auto &data = current;
                        const auto z = data->metres[std::size_t(sy % g.tile_size) * data->columns +
                                                    sx % g.tile_size];
                        if (!std::isfinite(z))
                            return {};
                        value += weight * z;
                        magnitude = std::max(magnitude, std::abs(double(z)));
                    }
                const double rounding =
                    (px != ix || py != iy)
                        ? 64 * std::numeric_limits<double>::epsilon() * std::max(1., magnitude)
                        : 0;
                maximum = std::max(maximum, value + rounding);
            }
        return maximum;
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
            std::shared_ptr<const HeightTile> held;
            for (unsigned j = 0; j < 2; ++j)
                for (unsigned i = 0; i < 2; ++i) {
                    double weight = (i ? x - x0 : 1 - x + x0) * (j ? y - y0 : 1 - y + y0);
                    if (weight == 0)
                        continue;
                    auto xx = std::min(x0 + i, g.columns - 1), yy = std::min(y0 + j, g.rows - 1);
                    const auto &data = read_tile(layer, xx / g.tile_size, yy / g.tile_size, held);
                    auto z = data.metres[std::size_t(yy % g.tile_size) * data.columns +
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
HeightCatalog HeightCatalog::resident(std::vector<std::filesystem::path> files,
                                      HeightReference reference) {
    HeightCatalog result(std::move(files), reference);
    auto &impl = *result.impl_;
    std::vector<Impl::ResidentLayer> loaded;
    loaded.reserve(impl.maps.size());
    for (const auto &map : impl.maps) {
        Impl::ResidentLayer layer{map.read_tiles(), {}};
        layer.maxima.reserve(layer.tiles.size());
        for (const auto &tile : layer.tiles) {
            layer.maxima.push_back(Impl::tile_maximum(tile));
            impl.bytes += tile.metres.capacity() * sizeof(float);
        }
        loaded.push_back(std::move(layer));
    }
    impl.budget = impl.bytes;
    impl.resident = std::move(loaded);
    return result;
}
HeightCatalog::~HeightCatalog() = default;
HeightCatalog::HeightCatalog(HeightCatalog &&) noexcept = default;
HeightCatalog &HeightCatalog::operator=(HeightCatalog &&) noexcept = default;
const std::vector<HeightLayer> &HeightCatalog::layers() const { return impl_->layers; }
std::optional<CatalogSample> HeightCatalog::sample(Position p) const {
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    if (!impl_->resident)
        lock.lock();
    return impl_->sample(p);
}
std::vector<std::optional<CatalogSample>>
HeightCatalog::sample(std::span<const Position> points) const {
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    if (!impl_->resident)
        lock.lock();
    std::vector<std::optional<CatalogSample>> result;
    result.reserve(points.size());
    for (auto p : points)
        result.push_back(impl_->sample(p));
    return result;
}
HeightCacheStats HeightCatalog::cache_stats() const {
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    if (!impl_->resident)
        lock.lock();
    if (impl_->resident) {
        std::size_t tiles = 0;
        for (const auto &layer : *impl_->resident)
            tiles += layer.tiles.size();
        return {tiles, impl_->bytes, impl_->budget};
    }
    return {impl_->cache.size(), impl_->bytes, impl_->budget};
}
std::optional<double> HeightCatalog::upper_bound(Bounds bounds) const {
    if (!std::isfinite(bounds.west) || !std::isfinite(bounds.east) ||
        !std::isfinite(bounds.south) || !std::isfinite(bounds.north) || bounds.south < -90 ||
        bounds.north > 90 || bounds.south > bounds.north || bounds.west < -180 ||
        bounds.west > 180 || bounds.east < -180 || bounds.east > 180)
        throw std::invalid_argument("Invalid height bounds");
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    if (!impl_->resident)
        lock.lock();
    const bool wraps = bounds.west > bounds.east;
    const std::array intervals{
        std::pair{bounds.west, wraps ? 180. : bounds.east}, std::pair{-180., bounds.east}};
    double maximum = -INFINITY;
    for (std::size_t interval = 0; interval < (wraps ? 2u : 1u); ++interval) {
        const auto [west, east] = intervals[interval];
        bool covered = false;
        for (std::size_t layer = 0; layer < impl_->layers.size(); ++layer) {
            const auto &g = impl_->layers[layer].grid;
            const double right = g.west + (g.columns - 1) * g.longitude_step;
            const double bottom = g.north - (g.rows - 1) * g.latitude_step;
            if (bounds.north < bottom || bounds.south > g.north)
                continue;
            for (int shift : {-360, 0, 360}) {
                const double lo = west + shift, hi = east + shift;
                if (hi < g.west || lo > right)
                    continue;
                const auto x0 = std::uint32_t(std::clamp(
                    std::floor((lo - g.west) / g.longitude_step), 0., double(g.columns - 1)));
                const auto x1 = std::uint32_t(std::clamp(
                    std::ceil((hi - g.west) / g.longitude_step), 0., double(g.columns - 1)));
                const auto y0 =
                    std::uint32_t(std::clamp(std::floor((g.north - bounds.north) / g.latitude_step),
                                             0., double(g.rows - 1)));
                const auto y1 = std::uint32_t(std::clamp(
                    std::ceil((g.north - bounds.south) / g.latitude_step), 0., double(g.rows - 1)));
                // On each bilinear cell the maximum over a rectangle is at a corner.
                // Clip the boundary cells to the requested footprint; a distant high
                // vertex must not raise the bound for every point in its source cell.
                if (std::uint64_t(x1 - x0 + 1) * (y1 - y0 + 1) <= 256) {
                    const double xmin =
                        std::clamp((lo - g.west) / g.longitude_step, 0., double(g.columns - 1));
                    const double xmax =
                        std::clamp((hi - g.west) / g.longitude_step, 0., double(g.columns - 1));
                    const double ymin = std::clamp((g.north - bounds.north) / g.latitude_step, 0.,
                                                   double(g.rows - 1));
                    const double ymax = std::clamp((g.north - bounds.south) / g.latitude_step, 0.,
                                                   double(g.rows - 1));
                    const auto local = impl_->clipped_maximum(layer, xmin, ymin, xmax, ymax);
                    if (!local)
                        return {};
                    maximum = std::max(maximum, *local);
                    covered |= lo >= g.west && hi <= right && bounds.south >= bottom &&
                               bounds.north <= g.north;
                    continue;
                }
                for (auto y = y0 / g.tile_size; y <= y1 / g.tile_size; ++y)
                    for (auto x = x0 / g.tile_size; x <= x1 / g.tile_size; ++x) {
                        std::shared_ptr<const HeightTile> held;
                        const auto *tile = &impl_->read_tile(layer, x, y, held);
                        const auto left = std::max(x0, x * g.tile_size) - x * g.tile_size;
                        const auto right_edge = std::min(x1 - x * g.tile_size, tile->columns - 1);
                        const auto top = std::max(y0, y * g.tile_size) - y * g.tile_size;
                        const auto bottom_edge = std::min(y1 - y * g.tile_size, tile->rows - 1);
                        const std::optional<double> *tile_maximum = nullptr;
                        if (impl_->resident)
                            tile_maximum =
                                &(*impl_->resident)[layer].maxima[impl_->tile_index(layer, x, y)];
                        else if (const auto cached = impl_->cache.find(Impl::Key{layer, x, y});
                                 cached != impl_->cache.end())
                            tile_maximum = &cached->second.maximum;
                        if (left == 0 && top == 0 && right_edge + 1 == tile->columns &&
                            bottom_edge + 1 == tile->rows && tile_maximum) {
                            if (!*tile_maximum)
                                return {};
                            maximum = std::max(maximum, **tile_maximum);
                        } else {
                            for (auto yy = top; yy <= bottom_edge; ++yy)
                                for (auto xx = left; xx <= right_edge; ++xx) {
                                    const auto value =
                                        tile->metres[std::size_t(yy) * tile->columns + xx];
                                    if (!std::isfinite(value))
                                        return {};
                                    maximum = std::max(maximum, double(value));
                                }
                        }
                    }
                covered |= lo >= g.west && hi <= right && bounds.south >= bottom &&
                           bounds.north <= g.north;
            }
        }
        if (!covered)
            return {};
    }
    return std::isfinite(maximum) ? std::optional<double>{maximum} : std::nullopt;
}
} // namespace elysia::geo
