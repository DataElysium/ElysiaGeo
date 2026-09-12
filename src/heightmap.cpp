#include <elysia_geo/heightmap.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace elysia::geo {
namespace {
constexpr std::uint64_t header_size=56;
std::uint64_t integer(std::istream& in, int size) {
    std::uint64_t result=0;
    for(int i=0;i<size;++i) {
        int c=in.get();
        if(c==EOF) throw std::runtime_error("Truncated heightmap package");
        result|=std::uint64_t(c)<<(8*i);
    }
    return result;
}
double number(std::istream& in) {return std::bit_cast<double>(integer(in,8));}
std::uint64_t tiles(std::uint32_t n,std::uint32_t size) {return (std::uint64_t(n)+size-1)/size;}
std::ifstream input(const std::filesystem::path& file) {
    std::ifstream in(file,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open heightmap: "+file.string());
    return in;
}
float cell(std::ifstream& in,const HeightGrid& g,std::uint32_t x,std::uint32_t y) {
    auto t=g.tile_size;
    auto tile=std::uint64_t(y/t)*tiles(g.columns,t)+x/t;
    auto index=tile*t*t+std::uint64_t(y%t)*t+x%t;
    in.seekg(header_size+index*4);
    auto value=std::bit_cast<float>(static_cast<std::uint32_t>(integer(in,4)));
    if(std::isinf(value)) throw std::runtime_error("Infinite heightmap sample");
    return value;
}
}
Heightmap Heightmap::open(const std::filesystem::path& file) {
    auto in=input(file);
    char magic[8];in.read(magic,8);
    if(in.gcount()!=8||std::string_view(magic,8)!="ELYHGT01")
        throw std::runtime_error("Unsupported heightmap package");
    Heightmap result;result.file_=std::filesystem::absolute(file);
    auto& g=result.grid_;
    g.columns=integer(in,4);g.rows=integer(in,4);g.tile_size=integer(in,4);
    g.reference=static_cast<HeightReference>(integer(in,4));
    g.west=number(in);g.north=number(in);g.longitude_step=number(in);g.latitude_step=number(in);
    if(!g.columns||!g.rows||!g.tile_size||g.tile_size>4096||
       static_cast<std::uint32_t>(g.reference)>4||!std::isfinite(g.west)||
       !std::isfinite(g.north)||!std::isfinite(g.longitude_step)||!std::isfinite(g.latitude_step)||
       g.west < -180||g.west>180||g.north < -90||g.north>90||
       g.longitude_step<=0||g.latitude_step<=0||
       g.longitude_step*(g.columns-1)>360||g.north-g.latitude_step*(g.rows-1)<-90)
        throw std::runtime_error("Invalid heightmap grid");
    auto nx=tiles(g.columns,g.tile_size),ny=tiles(g.rows,g.tile_size);
    auto bytes_per_tile=std::uint64_t(g.tile_size)*g.tile_size*4;
    auto limit=(std::uint64_t(std::numeric_limits<std::int64_t>::max())-header_size)/bytes_per_tile;
    if(nx>limit/ny||std::filesystem::file_size(file)!=header_size+nx*ny*bytes_per_tile)
        throw std::runtime_error("Invalid heightmap package size");
    return result;
}
HeightTile Heightmap::read_tile(std::uint32_t x,std::uint32_t y) const {
    const auto& g=grid_;
    if(!g.tile_size||x>=tiles(g.columns,g.tile_size)||y>=tiles(g.rows,g.tile_size))
        throw std::out_of_range("Heightmap tile outside grid");
    HeightTile result{x,y,std::min(g.tile_size,g.columns-x*g.tile_size),
                           std::min(g.tile_size,g.rows-y*g.tile_size),{}};
    result.metres.reserve(std::size_t(result.columns)*result.rows);
    auto in=input(file_);
    for(std::uint32_t row=0;row<result.rows;++row) {
        // Read each contiguous row without per-sample seeks.
        auto tile=std::uint64_t(y)*tiles(g.columns,g.tile_size)+x;
        in.seekg(header_size+(tile*g.tile_size*g.tile_size+std::uint64_t(row)*g.tile_size)*4);
        for(std::uint32_t col=0;col<result.columns;++col) {
            auto v=std::bit_cast<float>(static_cast<std::uint32_t>(integer(in,4)));
            if(std::isinf(v)) throw std::runtime_error("Infinite heightmap sample");
            result.metres.push_back(v);
        }
    }
    return result;
}
std::optional<HeightSample> Heightmap::sample(Position p) const {
    validate(p);
    const auto& g=grid_;
    if(!g.tile_size) throw std::logic_error("Unopened heightmap");
    double lon=wrap_longitude(p.longitude_deg);
    // Place longitude in this raster's unwrapped interval (supports dateline regions).
    lon+=360*std::ceil((g.west-lon)/360);
    double x=(lon-g.west)/g.longitude_step,y=(g.north-p.latitude_deg)/g.latitude_step;
    if(x<0||y<0||x>g.columns-1||y>g.rows-1) return std::nullopt;
    auto x0=static_cast<std::uint32_t>(std::floor(x)),y0=static_cast<std::uint32_t>(std::floor(y));
    auto x1=std::min(x0+1,g.columns-1),y1=std::min(y0+1,g.rows-1);
    double fx=x-x0,fy=y-y0,value=0;
    auto in=input(file_);
    for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
        double weight=(i?fx:1-fx)*(j?fy:1-fy);
        if(weight==0) continue;
        auto v=cell(in,g,i?x1:x0,j?y1:y0);
        if(std::isnan(v)) return std::nullopt;
        value+=weight*v;
    }
    return HeightSample{value,g.longitude_step,g.latitude_step,g.reference};
}
}
