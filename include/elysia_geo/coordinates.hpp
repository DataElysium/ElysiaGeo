#pragma once
#include <optional>

namespace elysia::geo {
// Degrees; altitude is metres above the WGS84 ellipsoid (not sea level).
struct Position { double longitude_deg{}, latitude_deg{}, altitude_m{}; };
struct Vec3 { double x{}, y{}, z{}; };
struct Point { double x{}, y{}; };

void validate(Position p);
double wrap_longitude(double degrees);
// Spherical approximation using mean Earth radius, not an ellipsoidal geodesic.
double spherical_distance_m(Position a, Position b);
// No unique bearing for coincident or antipodal positions.
std::optional<double> initial_bearing_deg(Position a, Position b);
Vec3 to_ecef(Position p);
Position from_ecef(Vec3 p);

class LocalFrame {
public:
    explicit LocalFrame(Position origin);
    Vec3 to_local(Position p) const; // east, north, up, in metres
    Position to_geographic(Vec3 p) const;
private:
    Vec3 origin_, east_, north_, up_;
};

// Plate carree in degrees. Longitude is unwrapped around the selected meridian.
struct Equirectangular {
    double central_meridian_deg{};
    Point project(Position p) const;
    Position unproject(Point p) const;
};
}
