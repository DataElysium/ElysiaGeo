#include <elysia_geo/coordinates.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace elysia::geo {
namespace {
constexpr double rad = std::numbers::pi / 180.0;
constexpr double axis = 6378137.0;
constexpr double e2 = 6.6943799901413165e-3;
double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
}
void validate(Position p) {
    if (!std::isfinite(p.longitude_deg) || !std::isfinite(p.latitude_deg) ||
        !std::isfinite(p.altitude_m) || p.latitude_deg < -90 || p.latitude_deg > 90)
        throw std::invalid_argument("Invalid geographic position");
}
double wrap_longitude(double d) {
    if (!std::isfinite(d)) throw std::invalid_argument("Non-finite longitude");
    double result = std::fmod(d, 360.0);
    if (result < -180) result += 360;
    if (result >= 180) result -= 360;
    return result;
}
double spherical_distance_m(Position a, Position b) {
    validate(a); validate(b);
    double dl = wrap_longitude(wrap_longitude(b.longitude_deg)-wrap_longitude(a.longitude_deg))*rad;
    double dp = (b.latitude_deg-a.latitude_deg)*rad;
    double h = std::pow(std::sin(dp/2),2) + std::cos(a.latitude_deg*rad)*
        std::cos(b.latitude_deg*rad)*std::pow(std::sin(dl/2),2);
    return 2*6371008.8*std::asin(std::sqrt(std::clamp(h,0.0,1.0)));
}
std::optional<double> initial_bearing_deg(Position a, Position b) {
    validate(a); validate(b);
    double dl = wrap_longitude(wrap_longitude(b.longitude_deg)-wrap_longitude(a.longitude_deg))*rad;
    double x = std::sin(dl)*std::cos(b.latitude_deg*rad);
    double y = std::cos(a.latitude_deg*rad)*std::sin(b.latitude_deg*rad)-
        std::sin(a.latitude_deg*rad)*std::cos(b.latitude_deg*rad)*std::cos(dl);
    if (std::hypot(x,y)<1e-14) return std::nullopt;
    return std::fmod(std::atan2(x,y)/rad+360,360);
}
Vec3 to_ecef(Position p) {
    validate(p);
    double lon=wrap_longitude(p.longitude_deg)*rad, lat=p.latitude_deg*rad;
    double n=axis/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));
    return {(n+p.altitude_m)*std::cos(lat)*std::cos(lon),
            (n+p.altitude_m)*std::cos(lat)*std::sin(lon),
            (n*(1-e2)+p.altitude_m)*std::sin(lat)};
}
Position from_ecef(Vec3 p) {
    if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
        throw std::invalid_argument("Non-finite ECEF coordinate");
    double r=std::hypot(p.x,p.y);
    if (std::hypot(r,p.z)<axis/2) throw std::invalid_argument("ECEF point too near Earth centre");
    if (r<1e-8) return {0,std::copysign(90.0,p.z),std::abs(p.z)-axis*std::sqrt(1-e2)};
    double lat=std::atan2(p.z,r*(1-e2));
    for (int i=0;i<16;++i) {
        double n=axis/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));
        double next=std::atan2(p.z+e2*n*std::sin(lat),r);
        if (std::abs(next-lat)<1e-14) { lat=next; break; }
        lat=next;
    }
    double n=axis/std::sqrt(1-e2*std::sin(lat)*std::sin(lat));
    double h=r*std::cos(lat)+p.z*std::sin(lat)-n*(1-e2*std::sin(lat)*std::sin(lat));
    return {wrap_longitude(std::atan2(p.y,p.x)/rad),lat/rad,h};
}
LocalFrame::LocalFrame(Position p):origin_(to_ecef(p)) {
    double l=wrap_longitude(p.longitude_deg)*rad, a=p.latitude_deg*rad;
    east_={-std::sin(l),std::cos(l),0};
    north_={-std::sin(a)*std::cos(l),-std::sin(a)*std::sin(l),std::cos(a)};
    up_={std::cos(a)*std::cos(l),std::cos(a)*std::sin(l),std::sin(a)};
}
Vec3 LocalFrame::to_local(Position p) const {
    auto v=to_ecef(p); v={v.x-origin_.x,v.y-origin_.y,v.z-origin_.z};
    return {dot(v,east_),dot(v,north_),dot(v,up_)};
}
Position LocalFrame::to_geographic(Vec3 p) const {
    return from_ecef({origin_.x+east_.x*p.x+north_.x*p.y+up_.x*p.z,
        origin_.y+east_.y*p.x+north_.y*p.y+up_.y*p.z,
        origin_.z+east_.z*p.x+north_.z*p.y+up_.z*p.z});
}
Point Equirectangular::project(Position p) const {
    validate(p);
    return {wrap_longitude(wrap_longitude(p.longitude_deg)-wrap_longitude(central_meridian_deg)), -p.latitude_deg};
}
Position Equirectangular::unproject(Point p) const {
    Position result{wrap_longitude(wrap_longitude(p.x)+wrap_longitude(central_meridian_deg)),-p.y,0};
    validate(result); return result;
}
}
