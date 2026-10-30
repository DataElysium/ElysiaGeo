#pragma once
#include <elysia_geo/coordinates.hpp>

namespace elysia::geo {
/// First fundamental form of the projected WGS84 surface at one map position.
/// For an infinitesimal map displacement (x,y), squared ground distance is
/// xx*x*x + 2*xy*x*y + yy*y*y. Use short displacements; long routes need integration.
struct SurfaceMetric {
    double xx, xy, yy;
    double length(Point displacement) const;
};

/// Radial projection between a tangent map and the WGS84 ellipsoid.
/// Map x/y are east/north metres at the origin. Scale varies away from the origin.
/// Altitudes in Position are validated, then ignored; height remains a separate coordinate.
/// Immutable after construction; queries allocate no memory.
class SurfaceProjection {
  public:
    explicit SurfaceProjection(Position origin);
    Position geographic(Point map) const;
    /// Returns empty at/beyond the tangent plane's horizon (normalised dot <= 1e-12).
    std::optional<Point> project(Position geographic) const;
    /// ECEF point on the tangent plane, for conservative geographic bound construction.
    Vec3 tangent_ecef(Point map) const;
    Vec3 surface_ecef(Point map) const;
    SurfaceMetric metric(Point map) const;

  private:
    Vec3 origin_, east_, north_;
};
} // namespace elysia::geo
