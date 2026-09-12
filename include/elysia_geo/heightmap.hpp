#pragma once
#include "coordinates.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace elysia::geo {
// Heights are signed metres, positive upward. Seabed heights are usually negative.
// The reference is metadata; no vertical datum transformation is performed.
enum class HeightReference : std::uint32_t { unspecified, wgs84_ellipsoid, egm96, egm2008, mean_sea_level };
struct HeightGrid {
    std::uint32_t columns{}, rows{}, tile_size{};
    double west{}, north{}, longitude_step{}, latitude_step{}; // sample centres
    HeightReference reference{};
};
struct HeightTile {
    std::uint32_t x{}, y{}, columns{}, rows{};
    std::vector<float> metres; // row-major, north to south; NaN means unavailable
};
struct HeightSample {
    double metres{}, longitude_step{}, latitude_step{};
    HeightReference reference{};
};
// One immutable raster layer/resolution per package. Files must remain unchanged
// while in use. Independent const calls are thread-safe; buffers belong to callers.
class Heightmap {
public:
    static Heightmap open(const std::filesystem::path& file);
    const HeightGrid& grid() const { return grid_; }
    HeightTile read_tile(std::uint32_t x, std::uint32_t y) const;
    // Bilinear interpolation. No extrapolation or implicit resolution fallback.
    // Any missing sample with nonzero weight makes the result unavailable.
    std::optional<HeightSample> sample(Position position) const;
private:
    std::filesystem::path file_;
    HeightGrid grid_;
};
}
