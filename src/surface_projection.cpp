#include <algorithm>
#include <cmath>
#include <elysia_geo/surface_projection.hpp>
#include <numbers>
#include <stdexcept>

namespace elysia::geo {
namespace {
constexpr double major = 6378137., minor = 6356752.314245;
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 multiply(Vec3 v, double k) { return {v.x * k, v.y * k, v.z * k}; }
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double ellipsoid_dot(Vec3 a, Vec3 b) {
    return (a.x * b.x + a.y * b.y) / (major * major) + a.z * b.z / (minor * minor);
}
double radial_scale(Vec3 p) {
    const double squared = ellipsoid_dot(p, p);
    if (!std::isfinite(squared) || squared <= 0)
        throw std::invalid_argument("Surface map coordinate exceeds numeric range");
    return 1 / std::sqrt(squared);
}
void validate_map(Point p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        throw std::invalid_argument("Non-finite surface map coordinate");
}
} // namespace
double SurfaceMetric::length(Point d) const {
    validate_map(d);
    return std::sqrt(std::max(0., xx * d.x * d.x + 2 * xy * d.x * d.y + yy * d.y * d.y));
}
SurfaceProjection::SurfaceProjection(Position p) {
    validate(p);
    p.altitude_m = 0;
    origin_ = to_ecef(p);
    constexpr double rad = std::numbers::pi / 180;
    const double lon = wrap_longitude(p.longitude_deg) * rad, lat = p.latitude_deg * rad;
    east_ = {-std::sin(lon), std::cos(lon), 0};
    north_ = {-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat)};
}
Vec3 SurfaceProjection::tangent_ecef(Point p) const {
    validate_map(p);
    return {origin_.x + east_.x * p.x + north_.x * p.y, origin_.y + east_.y * p.x + north_.y * p.y,
            origin_.z + east_.z * p.x + north_.z * p.y};
}
Vec3 SurfaceProjection::surface_ecef(Point p) const {
    const auto v = tangent_ecef(p);
    return multiply(v, radial_scale(v));
}
Position SurfaceProjection::geographic(Point p) const {
    auto result = from_ecef(surface_ecef(p));
    result.altitude_m = 0;
    return result;
}
std::optional<Point> SurfaceProjection::project(Position p) const {
    validate(p);
    p.altitude_m = 0;
    const auto v = to_ecef(p), up = cross(east_, north_);
    const double normal = dot(up, v);
    if (normal <= 1e-12 * major)
        return std::nullopt;
    const auto tangent = subtract(multiply(v, dot(up, origin_) / normal), origin_);
    return Point{dot(tangent, east_), dot(tangent, north_)};
}
SurfaceMetric SurfaceProjection::metric(Point map) const {
    const auto p = tangent_ecef(map);
    const double scale = radial_scale(p);
    // Differentiate p / sqrt(p^T A p), A=diag(1/a²,1/a²,1/b²).
    const auto derivative = [&](Vec3 direction) {
        return subtract(multiply(direction, scale),
                        multiply(p, scale * scale * scale * ellipsoid_dot(p, direction)));
    };
    const auto dx = derivative(east_), dy = derivative(north_);
    return {dot(dx, dx), dot(dx, dy), dot(dy, dy)};
}
} // namespace elysia::geo
