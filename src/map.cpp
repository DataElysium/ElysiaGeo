#include <elysia_geo/map.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace elysia::geo {
namespace {
int column(double lon) { return std::clamp(int(std::floor((lon+180)/10)),0,35); }
int row(double lat) { return std::clamp(int(std::floor((lat+90)/10)),0,17); }
std::uint64_t read_uint(std::istream& in, int bytes) {
    std::uint64_t v=0;
    for(int i=0;i<bytes;++i) {
        int c=in.get(); if(c==EOF) throw std::runtime_error("Truncated geography package");
        v|=std::uint64_t(c)<<(8*i);
    }
    return v;
}
std::uint32_t count(std::istream& in, std::uint32_t limit) {
    auto n=read_uint(in,4);
    if(n==0 || n>limit) throw std::runtime_error("Invalid geography element count");
    return static_cast<std::uint32_t>(n);
}
double number(std::istream& in) { return std::bit_cast<double>(read_uint(in,8)); }
// 0 outside, 1 inside, 2 exactly on a segment within numerical tolerance.
int ring_test(const std::vector<Point>& ring, std::span<const std::uint32_t> edges, Point p) {
    bool inside=false;
    for(auto i:edges) {
        auto a=ring[i-1], b=ring[i];
        double dx=b.x-a.x, dy=b.y-a.y;
        if(p.x>=std::min(a.x,b.x)-1e-10 && p.x<=std::max(a.x,b.x)+1e-10 &&
           p.y>=std::min(a.y,b.y)-1e-10 && p.y<=std::max(a.y,b.y)+1e-10 &&
           std::abs(dx*(p.y-a.y)-dy*(p.x-a.x))<=1e-10*std::max(1.0,std::hypot(dx,dy))) return 2;
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    }
    return inside?1:0;
}
}
std::size_t MapDataset::RingIndex::band(double latitude) const {
    if (north == south) return 0;
    const double position = (latitude - south) / (north - south) * bands.size();
    return static_cast<std::size_t>(std::clamp(position, 0., double(bands.size() - 1)));
}
MapDataset::RingIndex::RingIndex(const std::vector<Point>& ring) {
    south = north = ring.front().y;
    for (auto p : ring) {
        south = std::min(south, p.y);
        north = std::max(north, p.y);
    }
    bands.resize(std::clamp<std::size_t>((ring.size() - 1) / 32, 1, 256));
    std::size_t references = 0;
    for (std::uint32_t i = 1; i < ring.size(); ++i) {
        // Include the original segment-boundary tolerance on both sides of a band.
        const auto first = band(std::min(ring[i - 1].y, ring[i].y) - 1e-10);
        const auto last = band(std::max(ring[i - 1].y, ring[i].y) + 1e-10);
        references += last - first + 1;
        if (references > 4 * ring.size()) {
            // Highly overlapping long edges use a bounded linear fallback.
            bands.clear();
            bands.resize(1);
            for (std::uint32_t edge = 1; edge < ring.size(); ++edge)
                bands.front().push_back(edge);
            return;
        }
        for (auto j = first; j <= last; ++j) bands[j].push_back(i);
    }
}
MapDataset MapDataset::open(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open geography package: "+path.string());
    char magic[8]; in.read(magic,8);
    if(in.gcount()!=8 || std::string_view(magic,8)!="ELYGEO01") throw std::runtime_error("Unsupported geography package");
    MapDataset result;
    auto bytes=std::filesystem::file_size(path);
    auto n=count(in,1000000);
    // Every polygon needs at least a ring count, point count and four XY points.
    if(n>(bytes-12)/72) throw std::runtime_error("Impossible geography polygon count");
    for(std::uint32_t i=0;i<n;++i) {
        Polygon p{{},{180,90,-180,-90}};
        auto nr=count(in,100000);
        for(std::uint32_t r=0;r<nr;++r) {
            auto np=count(in,10000000);
            auto offset=in.tellg();
            if(np<4 || offset<0 || std::uint64_t(np)*16>bytes-std::uint64_t(offset))
                throw std::runtime_error("Impossible geography ring size");
            std::vector<Point> ring; ring.reserve(np);
            for(std::uint32_t k=0;k<np;++k) {
                Point v{number(in),number(in)};
                if(!std::isfinite(v.x)||!std::isfinite(v.y)||v.x < -180||v.x>180||v.y < -90||v.y>90)
                    throw std::runtime_error("Invalid geography coordinate");
                p.bounds.west=std::min(p.bounds.west,v.x); p.bounds.east=std::max(p.bounds.east,v.x);
                p.bounds.south=std::min(p.bounds.south,v.y); p.bounds.north=std::max(p.bounds.north,v.y);
                ring.push_back(v);
            }
            if(ring.front().x!=ring.back().x || ring.front().y!=ring.back().y)
                throw std::runtime_error("Unclosed geography ring");
            p.rings.push_back(std::move(ring));
        }
        for(int y=row(p.bounds.south);y<=row(p.bounds.north);++y)
            for(int x=column(p.bounds.west);x<=column(p.bounds.east);++x)
                result.cells_[y*36+x].push_back(i);
        auto& indices = result.ring_indices_.emplace_back();
        for (const auto& ring : p.rings) indices.emplace_back(ring);
        result.polygons_.push_back(std::move(p));
    }
    if(in.peek()!=EOF) throw std::runtime_error("Trailing data in geography package");
    return result;
}
Surface MapDataset::surface(Position p) const {
    validate(p); p.longitude_deg=wrap_longitude(p.longitude_deg);
    auto test=[&](double lon) {
        bool boundary=false;
        for(auto i:cells_[row(p.latitude_deg)*36+column(lon)]) {
            const auto& polygon=polygons_[i]; auto b=polygon.bounds;
            if(lon<b.west||lon>b.east||p.latitude_deg<b.south||p.latitude_deg>b.north) continue;
            auto test_ring = [&](std::size_t ring) {
                const auto& index = ring_indices_[i][ring];
                return ring_test(polygon.rings[ring], index.bands[index.band(p.latitude_deg)],
                                 {lon, p.latitude_deg});
            };
            int outer=test_ring(0);
            if(outer==2) { boundary=true; continue; }
            if(!outer) continue;
            bool hole=false;
            for(std::size_t j=1;j<polygon.rings.size();++j) {
                int value=test_ring(j);
                if(value==2) boundary=true;
                if(value) {hole=true; break;}
            }
            if(!hole) return Surface::land;
        }
        return boundary?Surface::boundary:Surface::water;
    };
    auto answer=test(p.longitude_deg);
    if(p.longitude_deg==-180 && answer==Surface::water) return test(180);
    return answer;
}
std::vector<std::size_t> MapDataset::query(Bounds b) const {
    if(!std::isfinite(b.west)||!std::isfinite(b.east)||!std::isfinite(b.south)||!std::isfinite(b.north)||
       b.west < -180||b.west>180||b.east < -180||b.east>180||b.south < -90||b.north>90||b.south>b.north)
        throw std::invalid_argument("Invalid geographic bounds");
    std::vector<std::size_t> ids;
    auto append=[&](double west,double east) {
        for(int y=row(b.south);y<=row(b.north);++y) for(int x=column(west);x<=column(east);++x)
            for(auto i:cells_[y*36+x]) {
                auto p=polygons_[i].bounds;
                if(p.east>=west&&p.west<=east&&p.north>=b.south&&p.south<=b.north) ids.push_back(i);
            }
    };
    if(b.west>b.east) {append(b.west,180);append(-180,b.east);} else append(b.west,b.east);
    std::sort(ids.begin(),ids.end()); ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    return ids;
}
}
