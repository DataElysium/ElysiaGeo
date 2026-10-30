#pragma once
#include "heightmap.hpp"
#include <memory>
#include <span>

namespace elysia::geo {
struct Bounds;
struct HeightLayer {
    std::filesystem::path source;
    HeightGrid grid;
};
struct CatalogSample {
    HeightSample height;
    std::size_t layer; // Index into layers(); stable for this catalogue's lifetime.
};
struct HeightCacheStats {
    std::size_t tiles{}, bytes{}, budget_bytes{};
    /// Additional resident maximum-height arrays, shared by all queries.
    std::size_t index_bytes{};
};
// Immutable layer catalogue with a shared, byte-bounded LRU tile cache.
// Finest angular cell area wins; ties prefer smaller coverage, then input
// order. NoData/outside coverage falls through; corrupt files and I/O errors
// still throw. All layers must use the explicitly requested vertical reference;
// no conversion. Concurrent const queries are safe, but sampling/I/O is
// serialized by the cache lock. resident() instead loads immutable arrays for
// queries without cache synchronization.
class HeightCatalog {
  public:
    explicit HeightCatalog(std::vector<std::filesystem::path> files, HeightReference reference,
                           std::size_t cache_bytes = 8 * 1024 * 1024);
    /// Load every layer before publishing the catalogue. Queries then use only
    /// immutable memory; no cache lock, eviction, or file reads. Storage lives
    /// until this catalogue is destroyed. A shared maximum-height pyramid is
    /// built directly from these arrays, without copying the base raster.
    static HeightCatalog resident(std::vector<std::filesystem::path> files,
                                  HeightReference reference);
    ~HeightCatalog();
    HeightCatalog(HeightCatalog &&) noexcept;
    HeightCatalog &operator=(HeightCatalog &&) noexcept;
    const std::vector<HeightLayer> &layers() const;
    std::optional<CatalogSample> sample(Position) const;
    std::vector<std::optional<CatalogSample>> sample(std::span<const Position>) const;
    // Resident catalogues report their fixed allocation in both bytes and
    // budget_bytes.
    HeightCacheStats cache_stats() const;
    // Conservative maximum over overlapping source samples/layers, including
    // bilinear neighbours. Missing coverage/NoData returns nullopt. Never a
    // sampled estimate.
    std::optional<double> upper_bound(Bounds) const;
    /// True certifies the whole rectangle is below ceiling; false requires
    /// finer checks. Resident queries accept coarse nodes whose maximum is
    /// already low enough, avoiding unnecessary descent.
    bool below(Bounds, double ceiling) const;

  private:
    std::optional<double> bound(Bounds, double stop_below) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace elysia::geo
