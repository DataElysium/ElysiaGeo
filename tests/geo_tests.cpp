#include <elysia_geo/map.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace elysia::geo;
namespace {
int checks=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F f,const char* message) {
    bool rejected=false;try {f();}catch(const std::exception&) {rejected=true;}
    check(rejected,message);
}
void near(double a,double b,double tolerance,const char* message) {check(std::abs(a-b)<=tolerance,message);}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("elysia-geo-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Temporary(){std::error_code ec;std::filesystem::remove(path,ec);}
};
void integer(std::ostream& out,std::uint64_t value,int n=4) {
    for(int i=0;i<n;++i) out.put(char((value>>(i*8))&255));
}
void ring(std::ostream& out,std::initializer_list<Point> points) {
    integer(out,points.size());for(auto p:points) {
        integer(out,std::bit_cast<std::uint64_t>(p.x),8);integer(out,std::bit_cast<std::uint64_t>(p.y),8);
    }
}
void coordinates() {
    near(wrap_longitude(540),-180,0,"longitude wrap");
    near(spherical_distance_m({179,0,0},{-179,0,0}),222390.160467,0.01,"dateline distance");
    near(spherical_distance_m({0,0,0},{180,0,0}),20015114.442,0.1,"antipodal distance");
    check(!initial_bearing_deg({0,0,0},{180,0,0}),"antipodal bearing undefined");
    check(!initial_bearing_deg({0,0,0},{0,0,0}),"coincident bearing undefined");
    near(*initial_bearing_deg({179,0,0},{-179,0,0}),90,1e-8,"eastward dateline bearing");
    near(to_ecef({0,0,0}).x,6378137,1e-6,"WGS84 semimajor axis");
    near(to_ecef({0,90,0}).z,6356752.314245,1e-6,"WGS84 semiminor axis");
    LocalFrame local({179.9,65,300});
    for(double lon:{-180.0,-179.99,-30.0,0.0,179.99}) for(double lat:{-90.0,-89.9,-30.0,0.0,65.0,89.9,90.0})
        for(double altitude:{-400.0,0.0,10000.0,1000000.0}) {
            Position p{lon,lat,altitude};auto q=local.to_geographic(local.to_local(p));
            near(q.latitude_deg,lat,1e-8,"local latitude roundtrip");
            near(q.altitude_m,altitude,1e-5,"local altitude roundtrip");
            if(std::abs(lat)<90) near(wrap_longitude(q.longitude_deg-lon),0,1e-8,"local longitude roundtrip");
        }
    std::mt19937_64 random(0x454c59534941);
    std::uniform_real_distribution<double> longitude(-180, 180),
        latitude(-90, 90);
    for (double altitude :
         {-3000000., -11000., 0., 8000., 100000., 40000000.}) {
      for (int i = 0; i < 2000; ++i) {
        const Position p{longitude(random), latitude(random), altitude};
        const auto q = from_ecef(to_ecef(p));
        near(wrap_longitude(q.longitude_deg - p.longitude_deg), 0, 1e-10,
             "ECEF inverse longitude over the globe");
        near(q.latitude_deg, p.latitude_deg, 1e-10,
             "ECEF inverse latitude below surface and at orbital altitude");
        near(q.altitude_m, p.altitude_m, 1e-6,
             "ECEF inverse altitude remains accurate within one micrometre");
      }
    }
    const auto distant = from_ecef({1, 0, std::numeric_limits<double>::max()});
    check(std::isfinite(distant.latitude_deg) && std::isfinite(distant.altitude_m),
          "Normal initialization must not overflow for a finite polar ECEF point");
    auto origin=local.to_local({179.9,65,300});near(std::hypot(origin.x,origin.y,origin.z),0,1e-9,"local origin");
    Equirectangular projection{170};auto point=projection.project({-175,42,0});
    near(point.x,15,1e-9,"projection centered across dateline");
    auto position=projection.unproject(point);near(position.longitude_deg,-175,1e-9,"projection inverse");
    near(position.latitude_deg,42,1e-9,"projection latitude inverse");
    rejects([]{validate({0,91,0});},"invalid latitude");
    rejects([]{wrap_longitude(std::numeric_limits<double>::infinity());},"infinite longitude");
    rejects([]{from_ecef({0,0,0});},"Earth centre undefined");
}
Surface surface_oracle(const MapDataset& map, Position p) {
    auto ring_test = [&](const std::vector<Point>& ring) {
        bool inside = false;
        for (std::size_t i = 1; i < ring.size(); ++i) {
            const auto a = ring[i - 1], b = ring[i];
            const double dx = b.x - a.x, dy = b.y - a.y;
            if (std::abs(dx * (p.latitude_deg - a.y) - dy * (p.longitude_deg - a.x)) <=
                    1e-10 * std::max(1., std::hypot(dx, dy)) &&
                p.longitude_deg >= std::min(a.x, b.x) - 1e-10 &&
                p.longitude_deg <= std::max(a.x, b.x) + 1e-10 &&
                p.latitude_deg >= std::min(a.y, b.y) - 1e-10 &&
                p.latitude_deg <= std::max(a.y, b.y) + 1e-10) return 2;
            if ((a.y > p.latitude_deg) != (b.y > p.latitude_deg) &&
                p.longitude_deg < dx * (p.latitude_deg - a.y) / dy + a.x) inside = !inside;
        }
        return inside ? 1 : 0;
    };
    bool boundary = false;
    for (const auto& polygon : map.polygons()) {
        const auto b = polygon.bounds;
        if (p.longitude_deg < b.west || p.longitude_deg > b.east ||
            p.latitude_deg < b.south || p.latitude_deg > b.north) continue;
        const int outer = ring_test(polygon.rings.front());
        if (outer == 2) { boundary = true; continue; }
        if (!outer) continue;
        bool hole = false;
        for (std::size_t i = 1; i < polygon.rings.size(); ++i) {
            const int hit = ring_test(polygon.rings[i]);
            if (hit == 2) boundary = true;
            if (hit) { hole = true; break; }
        }
        if (!hole) return Surface::land;
    }
    return boundary ? Surface::boundary : Surface::water;
}
void indexed_surface(const MapDataset& map) {
    std::mt19937 rng(1979);
    std::uniform_real_distribution<double> longitude(-179.99, 179.99), latitude(-89.99, 89.99);
    for (int i = 0; i < 2000; ++i) {
        Position p{longitude(rng), latitude(rng), 0};
        check(map.surface(p) == surface_oracle(map, p), "indexed surface differs from full coastline scan");
    }
    for (const auto& polygon : map.polygons()) for (const auto& ring : polygon.rings)
        for (std::size_t i = 1; i < ring.size(); i += std::max<std::size_t>(1, ring.size() / 8)) {
            const auto a = ring[i - 1], b = ring[i];
            for (const double offset : {0., -5e-11, 5e-11}) {
                Position p{(a.x + b.x) * .5, (a.y + b.y) * .5 + offset, 0};
                if (p.longitude_deg <= -180 || p.longitude_deg >= 180 || std::abs(p.latitude_deg) > 90) continue;
                check(map.surface(p) == surface_oracle(map, p), "indexed coastline boundary differs from full scan");
            }
        }
}
void indexed_degenerate_rings() {
    for (bool flat : {false, true}) {
        Temporary file;
        {
            std::ofstream out(file.path, std::ios::binary);
            out.write("ELYGEO01", 8);
            integer(out, 1);
            integer(out, 1);
            integer(out, 1025);
            // Long alternating edges exercise the bounded fallback; flat rings have zero height.
            for (int i = 0; i <= 1024; ++i) {
                const double x = i == 1024 ? 0 : 20. * i / 1024;
                const double y = flat ? 10 : (i % 2 ? 20 : 0);
                integer(out, std::bit_cast<std::uint64_t>(x), 8);
                integer(out, std::bit_cast<std::uint64_t>(y), 8);
            }
        }
        auto map = MapDataset::open(file.path);
        for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x) {
            Position p{x * .5, y * .5, 0};
            check(map.surface(p) == surface_oracle(map, p), "long or flat edges preserve surface classification");
        }
    }
}
void synthetic() {
    Temporary file;
    {
        std::ofstream out(file.path,std::ios::binary);out.write("ELYGEO01",8);integer(out,1);integer(out,2);
        ring(out,{{0,0},{20,0},{20,20},{0,20},{0,0}});
        ring(out,{{5,5},{15,5},{15,15},{5,15},{5,5}});
    }
    auto map=MapDataset::open(file.path);
    check(map.surface({2,2,0})==Surface::land,"outer polygon land");
    check(map.surface({10,10,0})==Surface::water,"hole remains water");
    check(map.surface({5,10,0})==Surface::boundary,"hole boundary");
    check(map.surface({20,10,0})==Surface::boundary,"grid boundary coastline");
    check(map.surface({21,10,0})==Surface::water,"outside polygon");
    check(map.query({19,19,21,21}).size()==1,"region across index cell boundary");
    rejects([&]{map.query({0,10,20,-10});},"reversed latitude bounds");
    std::ifstream input(file.path,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
    for(std::size_t size=0;size<bytes.size();++size) {
        {std::ofstream out(file.path,std::ios::binary);out.write(bytes.data(),size);}
        rejects([&]{MapDataset::open(file.path);},"truncated file accepted");
    }
    auto corrupt=[&](std::string data) {
        {std::ofstream out(file.path,std::ios::binary);out.write(data.data(),data.size());}
        rejects([&]{MapDataset::open(file.path);},"corrupt package accepted");
    };
    corrupt(bytes+"x");auto bad=bytes;bad[0]='X';corrupt(bad);
    bad=bytes;for(int i=8;i<12;++i) bad[i]=char(255);corrupt(bad);
    bad=bytes; // First longitude becomes infinity.
    auto inf=std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity());
    for(int i=0;i<8;++i) bad[20+i]=char((inf>>(i*8))&255);corrupt(bad);
}
void earth() {
    auto map=MapDataset::open(ELYSIA_MAP_FILE);
    indexed_surface(map);
    check(map.polygons().size()==1421,"pinned Earth polygon count");
    for(auto p:{Position{-100,40,0},Position{15,23,0},Position{135,-25,0},Position{-19,65,0},Position{0,-89,0}})
        check(map.surface(p)==Surface::land,"known mainland or island");
    for(auto p:{Position{-30,30,0},Position{-140,0,0},Position{0,89,0},Position{179.9,0,0},Position{-179.9,0,0}})
        check(map.surface(p)==Surface::water,"known ocean");
    check(map.surface({180,0,0})==map.surface({-180,0,0}),"dateline aliases agree");
    check(map.query({-180,-90,180,90}).size()==map.polygons().size(),"global query covers all polygons");
    auto wrapped=map.query({170,-30,-170,0});auto east=map.query({170,-30,180,0});auto west=map.query({-180,-30,-170,0});
    east.insert(east.end(),west.begin(),west.end());std::sort(east.begin(),east.end());east.erase(std::unique(east.begin(),east.end()),east.end());
    check(wrapped==east&&!wrapped.empty(),"dateline region union");
    // Compare indexed bounds selection against independent brute-force selection.
    for(int lon=-180;lon<180;lon+=13) for(int lat=-90;lat<80;lat+=17) {
        Bounds b{double(lon),double(lat),double(std::min(lon+12,180)),double(lat+10)};
        std::vector<std::size_t> expected;
        for(std::size_t i=0;i<map.polygons().size();++i) {
            auto p=map.polygons()[i].bounds;
            if(p.east>=b.west&&p.west<=b.east&&p.north>=b.south&&p.south<=b.north)expected.push_back(i);
        }
        check(map.query(b)==expected,"spatial index missed a polygon");
    }
}
}
void heightmap_tests();
void height_catalog_tests();
void surface_projection_tests();
int main() {
    try {coordinates();surface_projection_tests();synthetic();indexed_degenerate_rings();earth();heightmap_tests();height_catalog_tests();std::cout<<"Passed "<<checks<<" geography checks\n";}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
