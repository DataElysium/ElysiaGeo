#pragma once
#include "coordinates.hpp"
#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace elysia::geo {
// west > east denotes a region crossing the dateline; [-180,180] is the globe.
struct Bounds { double west, south, east, north; };
struct Polygon { std::vector<std::vector<Point>> rings; Bounds bounds; };
enum class Surface { land, water, boundary };

// Immutable after loading; concurrent const queries require no graphics or ECS.
// This dataset describes generalized continental land, not navigable waterways.
class MapDataset {
public:
    static MapDataset open(const std::filesystem::path& geometry_file);
    Surface surface(Position position) const;
    std::vector<std::size_t> query(Bounds region) const;
    std::span<const Polygon> polygons() const { return polygons_; }
private:
    std::vector<Polygon> polygons_;
    std::array<std::vector<std::size_t>, 36*18> cells_;
};
}
