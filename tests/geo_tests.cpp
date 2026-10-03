#include <elysia_geo/map.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
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
    auto origin=local.to_local({179.9,65,300});near(std::hypot(origin.x,origin.y,origin.z),0,1e-9,"local origin");
    Equirectangular projection{170};auto point=projection.project({-175,42,0});
    near(point.x,15,1e-9,"projection centered across dateline");
    auto position=projection.unproject(point);near(position.longitude_deg,-175,1e-9,"projection inverse");
    near(position.latitude_deg,42,1e-9,"projection latitude inverse");
    rejects([]{validate({0,91,0});},"invalid latitude");
    rejects([]{wrap_longitude(std::numeric_limits<double>::infinity());},"infinite longitude");
    rejects([]{from_ecef({0,0,0});},"Earth centre undefined");
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
int main() {
    try {coordinates();synthetic();earth();heightmap_tests();height_catalog_tests();std::cout<<"Passed "<<checks<<" geography checks\n";}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
