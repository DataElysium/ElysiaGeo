#include <elysia_geo/heightmap.hpp>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value) {if(!value) throw std::runtime_error("Heightmap regression");}
template<class F> void rejects(F f) {bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);}
void integer(std::ostream& out,std::uint64_t n,int bytes) {for(int i=0;i<bytes;++i)out.put(char(n>>(i*8)));}
struct Fixture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("elysia-height-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Fixture(){std::error_code ec;std::filesystem::remove(path,ec);}
    void write(double west=10,double step=1) {
        std::ofstream out(path,std::ios::binary);out.write("ELYHGT01",8);
        for(auto n:{3,3,2,2})integer(out,n,4);
        for(double n:{west,2.,step,1.})integer(out,std::bit_cast<std::uint64_t>(n),8);
        // 3x3 raster, 2x2 tiles. Plane z = 10*x + 100*y, centre sample missing.
        float missing=std::numeric_limits<float>::quiet_NaN();
        for(float n:{0.f,10.f,100.f,missing,20.f,missing,120.f,missing,
                     200.f,210.f,missing,missing,220.f,missing,missing,missing})
            integer(out,std::bit_cast<std::uint32_t>(n),4);
    }
};
}
void heightmap_tests() {
    using namespace elysia::geo;
    Fixture f;f.write();auto map=Heightmap::open(f.path);
    check(map.grid().columns==3&&map.grid().reference==HeightReference::egm96);
    check(map.sample({10.5,2,0})->metres==5); // Missing zero-weight neighbours are harmless.
    check(!map.sample({10.5,1.5,0}));
    check(!map.sample({9,1,0})&&!map.sample({11,3,0}));
    check(map.sample({11.5,0,0})->metres==215); // Crosses a tile boundary.
    check(map.sample({12,0,0})->metres==220);
    auto tile=map.read_tile(1,1);check(tile.columns==1&&tile.rows==1&&tile.metres[0]==220);
    check(std::isnan(map.read_tile(0,0).metres[3]));
    rejects([&]{map.read_tile(2,0);});
    rejects([&]{map.sample({0,91,0});});
    f.write(179);map=Heightmap::open(f.path);
    check(map.sample({-179,0,0})->metres==220);
    check(map.sample({180,2,0})->metres==10);
    f.write(10,0);rejects([&]{Heightmap::open(f.path);});
    f.write();std::filesystem::resize_file(f.path,60);rejects([&]{Heightmap::open(f.path);});
    f.write();{std::ofstream out(f.path,std::ios::app);out.put('x');}rejects([&]{Heightmap::open(f.path);});
    // Negative seabed elevation and ordinary four-corner interpolation.
    f.write();{std::fstream out(f.path,std::ios::in|std::ios::out|std::ios::binary);
        out.seekp(56);for(float n:{-100.f,-80.f,-60.f,-40.f})integer(out,std::bit_cast<std::uint32_t>(n),4);}
    map=Heightmap::open(f.path);check(map.sample({10.5,1.5,0})->metres==-70);
    {std::fstream out(f.path,std::ios::in|std::ios::out|std::ios::binary);out.seekp(56);
        integer(out,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()),4);}
    rejects([&]{map.sample({10,2,0});});rejects([&]{map.read_tile(0,0);});
}
