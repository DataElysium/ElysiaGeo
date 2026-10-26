#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <elysia_geo/height_catalog.hpp>
#include <elysia_geo/map.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid catalogue operation accepted");
}
void integer(std::ostream &out, std::uint64_t n, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(char(n >> (8 * i)));
}
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("elysia-catalog-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { std::filesystem::create_directories(root); }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    auto write(const char *name, double west, double step, bool hole, unsigned reference = 4,
               bool peak = false) {
        auto path = root / name;
        std::ofstream out(path, std::ios::binary);
        out.write("ELYHGT01", 8);
        for (auto n : {3u, 3u, 2u, reference})
            integer(out, n, 4);
        for (double n : {west, 2., step, step})
            integer(out, std::bit_cast<std::uint64_t>(n), 8);
        for (unsigned ty = 0; ty < 2; ++ty)
            for (unsigned tx = 0; tx < 2; ++tx)
                for (unsigned y = 0; y < 2; ++y)
                    for (unsigned x = 0; x < 2; ++x) {
                        auto xx = tx * 2 + x, yy = ty * 2 + y;
                        float z = peak ? (xx == 1 && yy == 1 ? 1000.f : -100.f)
                                       : -100.f + 10 * xx + 20 * yy;
                        if (xx >= 3 || yy >= 3 || (hole && xx == 1 && yy == 1))
                            z = NAN;
                        integer(out, std::bit_cast<std::uint32_t>(z), 4);
                    }
        return path;
    }
};
} // namespace
void height_catalog_tests() {
    using namespace elysia::geo;
    Fixture f;
    auto base = f.write("base", 10, 1, false);
    auto detail = f.write("detail", 10, .5, true);
    HeightCatalog catalog({base, detail}, HeightReference::mean_sea_level, 32);
    check(catalog.layers()[0].source == detail, "Finest layer must win independent of input order");
    check(catalog.cache_stats().tiles == 0, "Catalogue open must not read raster payload");
    HeightCatalog complete({base}, HeightReference::mean_sea_level, 32);
    check(complete.upper_bound({10, 0, 12, 2}) == -40,
          "Height certificate must include actual tile peaks, not just query "
          "samples");
    check(!complete.upper_bound({9, 0, 12, 2}), "Partial coverage cannot certify clearance");
    check(!catalog.upper_bound({10, 1, 11, 2}), "NoData must disable the fast certificate");
    check(complete.upper_bound({10, 2, 10, 2}) == -100,
          "A partial tile query excludes unrelated higher samples in the same "
          "tile");
    check(catalog.upper_bound({10, 2, 10, 2}) == -100,
          "NoData outside the queried interpolation footprint must not disable "
          "its bound");
    check(std::abs(*complete.upper_bound({10.1, 1.9, 10.2, 1.95}) + 96) < 1e-9,
          "A tiny footprint bounds the interpolated surface within its clipped "
          "source cell");
    HeightCatalog peaked({f.write("peak", 10, 1, false, 4, true)}, HeightReference::mean_sea_level);
    check(peaked.upper_bound({10.2, .2, 11.8, 1.8}).value_or(-INFINITY) >= 1000,
          "Clipped rectangle must include interior source-grid extrema");
    const auto clipped = peaked.upper_bound({10.1, 1.8, 10.2, 1.9});
    check(clipped && std::abs(*clipped + 56) < 1e-9,
          "Clipped bilinear corner bound excludes the remote peak while "
          "retaining interpolation");
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) {
            const Position point{10.1 + .1 * x / 20, 1.8 + .1 * y / 20, 0};
            check(*clipped >= peaked.sample(point)->height.metres,
                  "Every sample in the clipped cell lies below its conservative "
                  "upper bound");
        }
    check(complete.cache_stats().bytes <= 32, "Bound scan must retain the cache byte limit");
    rejects([&] { complete.upper_bound({10, 2, 12, 1}); });
    auto z = catalog.sample({10.25, 2, 0});
    check(z && z->layer == 0 && z->height.metres == -95,
          "Fine sample or zero-weight NoData failed");
    z = catalog.sample({10.5, 1.5, 0});
    check(z && z->layer == 1 && z->height.metres == -85, "NoData must fall back to coarse source");
    z = catalog.sample({11.5, .5, 0});
    check(z && z->layer == 1 && z->height.metres == -55, "Outside detailed bounds must use base");
    check(!catalog.sample({0, 0, 0}), "Outside every layer must remain missing");
    rejects([&] { catalog.sample({0, NAN, 0}); });
    rejects([&] { catalog.sample({0, 91, 0}); });
    rejects([&] { HeightCatalog bad({base}, HeightReference::egm96); });
    std::vector<Position> points{{10.25, 2, 0}, {10.5, 1.5, 0}, {11.5, .5, 0}, {0, 0, 0}};
    auto batch = catalog.sample(points);
    check(batch[0]->layer == 0 && batch[1]->layer == 1 && batch[2]->height.metres == -55 &&
              !batch[3],
          "Batch sampling differs from individual queries");
    std::atomic<bool> good = true;
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([&] {
                for (int i = 0; i < 200; ++i) {
                    auto value = catalog.sample(points[i % 3]);
                    if (!value || value->height.metres != batch[i % 3]->height.metres)
                        good = false;
                }
            });
    }
    check(good && catalog.cache_stats().bytes <= 32, "Concurrent cache reads/eviction failed");
    HeightCatalog uncached({base}, HeightReference::mean_sea_level, 0);
    check(uncached.upper_bound({10, 2, 10, 2}) == -100 &&
              std::abs(*uncached.upper_bound({10.1, 1.9, 10.2, 1.95}) + 96) < 1e-9,
          "Partial tile bounds also work with zero cache budget");
    check(uncached.sample({10.5, 1.5, 0})->height.metres == -85 &&
              uncached.cache_stats().bytes == 0,
          "Zero budget must allow transient reads without retaining tiles");
    auto date = f.write("date", 179, 1, false);
    HeightCatalog seam_bound({date}, HeightReference::mean_sea_level);
    check(std::abs(*seam_bound.upper_bound({179.5, 0, -179.5, 2}) + 45) < 1e-9,
          "Conservative tile bounds must cover both sides of the date line");
    HeightCatalog dateline({date}, HeightReference::mean_sea_level);
    check(dateline.sample({-179, 0, 0})->height.metres == -40, "Dateline coverage failed");
    // Compare the cached implementation against the independent uncached reader.
    auto source = Heightmap::open(base);
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) {
            Position p{10 + x * .1, y * .1, 0};
            check(std::abs(source.sample(p)->metres - uncached.sample(p)->height.metres) < 1e-9,
                  "Cached/uncached bilinear mismatch");
            const auto bound = complete.upper_bound(
                {p.longitude_deg, p.latitude_deg, p.longitude_deg, p.latitude_deg});
            check(bound && *bound + 1e-9 >= source.sample(p)->metres,
                  "Point bounds must conservatively cover interpolated height");
        }
    // Resident queries keep exact layer fallback/bounds after the files
    // disappear.
    auto resident = HeightCatalog::resident({base, detail}, HeightReference::mean_sea_level);
    const auto resident_stats = resident.cache_stats();
    check(resident_stats.tiles == 8 && resident_stats.bytes == resident_stats.budget_bytes,
          "Resident storage contains every tile and reports its fixed allocation");
    std::vector<Position> resident_points;
    std::vector<Bounds> resident_bounds;
    std::vector<std::optional<CatalogSample>> expected_points;
    std::vector<std::optional<double>> expected_bounds;
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x) {
            Position p{10 + .1 * x, .1 * y, 0};
            resident_points.push_back(p);
            resident_bounds.push_back({p.longitude_deg, p.latitude_deg, 12, 2});
            expected_points.push_back(catalog.sample(p));
            expected_bounds.push_back(catalog.upper_bound(resident_bounds.back()));
        }
    std::filesystem::remove(base);
    std::filesystem::remove(detail);
    good = true;
    {
        std::vector<std::jthread> workers;
        for (int thread = 0; thread < 4; ++thread)
            workers.emplace_back([&] {
                const auto values = resident.sample(resident_points);
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const auto &a = values[i];
                    const auto &b = expected_points[i];
                    if (bool(a) != bool(b) ||
                        (a && (a->layer != b->layer || a->height.metres != b->height.metres)) ||
                        resident.upper_bound(resident_bounds[i]) != expected_bounds[i])
                        good = false;
                }
            });
    }
    check(good && resident.cache_stats().bytes == resident_stats.bytes,
          "Concurrent resident reads preserve values and bounds without files or "
          "cache mutation");
    auto resident_seam = HeightCatalog::resident({date}, HeightReference::mean_sea_level);
    check(resident_seam.upper_bound({179.5, 0, -179.5, 2}) ==
              seam_bound.upper_bound({179.5, 0, -179.5, 2}),
          "Resident bounds preserve date-line wrapping");
    auto empty = HeightCatalog::resident({}, HeightReference::mean_sea_level);
    check(!empty.sample({0, 0, 0}) && !empty.upper_bound({0, 0, 1, 1}) &&
              empty.cache_stats().tiles == 0,
          "Empty resident layers preserve missing coverage");
    auto corrupt = f.write("corrupt", 10, 1, false);
    {
        std::fstream out(corrupt, std::ios::in | std::ios::out | std::ios::binary);
        out.seekp(56);
        integer(out, std::bit_cast<std::uint32_t>(INFINITY), 4);
    }
    rejects([&] { HeightCatalog::resident({corrupt}, HeightReference::mean_sea_level); });
    // Packaged global coverage: axes/poles and the longitude seam all have
    // samples.
    auto root = std::filesystem::path(ELYSIA_MAP_FILE).parent_path().parent_path();
    HeightCatalog global(
        {root / "global-relief/terrain.elyhgt", root / "mediterranean/terrain.elyhgt"},
        HeightReference::mean_sea_level, 64 * 1024);
    check(global.layers()[0].grid.columns == 1001, "Equal-resolution regional layer should win");
    for (double lat : {-90., -60., 0., 30., 90.})
        for (double lon : {-180., -120., 0., 120., 180.}) {
            auto sample = global.sample({lon, lat, 0});
            check(sample && std::isfinite(sample->height.metres),
                  "Global base has missing coverage");
        }
    auto west = global.sample({-179.999999, 0, 0}), east = global.sample({179.999999, 0, 0});
    check(std::abs(west->height.metres - east->height.metres) < .1, "Global interpolation seam");
    check(global.cache_stats().bytes <= 64 * 1024, "Global cache grew over budget");
    auto resident_global = HeightCatalog::resident(
        {root / "global-relief/terrain.elyhgt", root / "mediterranean/terrain.elyhgt"},
        HeightReference::mean_sea_level);
    for (const Bounds bounds :
         {Bounds{85, 26, 89, 30}, Bounds{-180, -90, 180, 90}, Bounds{179, -1, -179, 1}})
        check(resident_global.upper_bound(bounds) == global.upper_bound(bounds),
              "Resident full-tile maxima match bounded-cache scans");
    for (double lat : {-90., -60., 0., 30., 90.})
        for (double lon : {-180., -120., 0., 120., 180.})
            check(resident_global.sample({lon, lat, 0})->height.metres ==
                      global.sample({lon, lat, 0})->height.metres,
                  "Resident samples preserve poles and longitude seam");
    for (auto p : std::vector<Position>{{-35, 30, 0}, {150, 30, 0}, {80, -20, 0}, {179.95, 0, 0}})
        check(global.sample(p)->height.metres < -100, "Known ocean site has no bathymetry");
    std::cout << "PASS height catalogue: fallback, metadata, batch, cache "
                 "budget, threads, global seam\n";
}
