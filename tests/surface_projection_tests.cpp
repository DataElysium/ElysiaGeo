#include <cmath>
#include <elysia_geo/surface_projection.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace elysia::geo;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
double distance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z); }
// Independent historical Fleet projection, including its LocalFrame round trip.
Position reference(const LocalFrame &frame, Point p) {
    auto v = to_ecef(frame.to_geographic({p.x, p.y, 0}));
    constexpr double a = 6378137., b = 6356752.314245;
    const double k = 1 / std::sqrt((v.x * v.x + v.y * v.y) / (a * a) + v.z * v.z / (b * b));
    return from_ecef({v.x * k, v.y * k, v.z * k});
}
} // namespace

void surface_projection_tests() {
    size_t checked = 0;
    for (Position origin : {Position{121, 24, 0},
                            {86.925, 27.988, 0},
                            {179.9, 0, 0},
                            {-179.9, -40, 0},
                            {0, 85, 0},
                            {120, -85, 0},
                            {0, 90, 0}}) {
        const SurfaceProjection projection(origin);
        const LocalFrame reference_frame(origin);
        for (double extent : {0., 600000., 1500000., 4630000.})
            for (double x : {-extent, extent})
                for (double y : {-extent, extent}) {
                    const Point p{x, y};
                    const auto geographic = projection.geographic(p);
                    require(distance(to_ecef(geographic), to_ecef(reference(reference_frame, p))) <
                                1e-6,
                            "Shared projection moved the historical geographic surface");
                    const auto restored = projection.project(geographic);
                    require(restored && std::hypot(restored->x - x, restored->y - y) < 1e-6,
                            "Large theatre projection failed sub-micrometre round trip");
                    require(geographic.altitude_m == 0,
                            "Surface projection must return zero height");
                    const auto metric = projection.metric(p);
                    require(metric.xx > 0 && metric.yy > 0 &&
                                metric.xx * metric.yy > metric.xy * metric.xy,
                            "Local surface metric must be positive definite");
                    for (Point d : {Point{1, 0}, {0, 1}, {.6, .8}, {-.8, .6}}) {
                        // Symmetric one-metre displacement is an independent finite difference.
                        const auto a = projection.surface_ecef({x - d.x * .5, y - d.y * .5});
                        const auto b = projection.surface_ecef({x + d.x * .5, y + d.y * .5});
                        require(std::abs(metric.length(d) - distance(a, b)) < 1e-8,
                                "Analytical metric disagrees with WGS84 surface displacement");
                    }
                    ++checked;
                }
    }
    const SurfaceProjection equator({0, 0, 0});
    constexpr double axis = 6378137., x = 4630000.;
    require(std::abs(equator.metric({0, 0}).length({1, 0}) - 1) < 1e-12,
            "Map metres must equal physical metres at origin");
    require(std::abs(equator.metric({x, 0}).length({1, 0}) - 1 / (1 + x * x / (axis * axis))) <
                1e-12,
            "Equatorial radial scale differs from analytical circle derivative");
    require(!equator.project({90, 0, 0}) && !equator.project({180, 0, 0}),
            "Horizon and far hemisphere must have no finite tangent map position");
    const SurfaceProjection taiwan({121, 24, 0});
    const double scale = taiwan.metric({4537400, 4537400}).length({1, 0});
    require(scale > .60 && scale < .62,
            "9260 km theatre regression must expose the existing ground-distance distortion");
    for (Point invalid : {Point{NAN, 0}, {0, INFINITY}, {1e308, 1e308}}) {
        for (bool metric : {false, true}) {
            bool rejected = false;
            try {
                if (metric)
                    taiwan.metric(invalid);
                else
                    taiwan.geographic(invalid);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            require(rejected, "Invalid map coordinates must fail before returning geometry");
        }
    }
    std::cout << "PASS surface projection: " << checked
              << " centre/edge cases, analytical and finite-difference metrics\n";
}
