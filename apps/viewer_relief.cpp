#include "viewer_relief.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

Color ReliefView::colour(double z) {
    struct Stop {double height;Color colour;};
    static constexpr Stop stops[]={{-8000,{8,20,44,255}},{-4000,{15,48,75,255}},
        {-1500,{25,78,105,255}},{-200,{43,119,141,255}},{0,{84,155,162,255}},
        {0,{92,121,82,255}},{300,{137,153,98,255}},{900,{173,160,112,255}},
        {1800,{150,120,96,255}},{3000,{192,177,157,255}},{4500,{235,232,217,255}}};
    int begin=z<0?0:5,end=z<0?4:10;
    if(z<=stops[begin].height)return stops[begin].colour;
    for(int i=begin+1;i<=end;++i) if(z<=stops[i].height) {
        double t=(z-stops[i-1].height)/(stops[i].height-stops[i-1].height);
        auto a=stops[i-1].colour,b=stops[i].colour;
        return {static_cast<unsigned char>(a.r+(b.r-a.r)*t),static_cast<unsigned char>(a.g+(b.g-a.g)*t),
                static_cast<unsigned char>(a.b+(b.b-a.b)*t),255};
    }
    return stops[end].colour;
}
ReliefView::ReliefView(const std::filesystem::path& file) {
    auto map=elysia::geo::Heightmap::open(file);grid_=map.grid();
    auto count=std::uint64_t(grid_.columns)*grid_.rows;
    if(count>16000000)throw std::runtime_error("Viewer relief cache limited to 16 million samples; import a regional/coarser raster");
    heights_.resize(count);
    for(std::uint32_t ty=0;ty<(grid_.rows+grid_.tile_size-1)/grid_.tile_size;++ty)
        for(std::uint32_t tx=0;tx<(grid_.columns+grid_.tile_size-1)/grid_.tile_size;++tx) {
            auto tile=map.read_tile(tx,ty);
            for(std::uint32_t y=0;y<tile.rows;++y)
                std::copy_n(tile.metres.begin()+y*tile.columns,tile.columns,
                    heights_.begin()+std::size_t(ty*grid_.tile_size+y)*grid_.columns+tx*grid_.tile_size);
        }
    auto plain=GenImageColor(grid_.columns,grid_.rows,BLANK),shade=GenImageColor(grid_.columns,grid_.rows,BLANK);
    auto* a=static_cast<Color*>(plain.data);auto* b=static_cast<Color*>(shade.data);
    for(std::uint32_t y=0;y<grid_.rows;++y)for(std::uint32_t x=0;x<grid_.columns;++x) {
        auto index=std::size_t(y)*grid_.columns+x;double z=heights_[index];if(!std::isfinite(z))continue;
        a[index]=colour(z);
        auto height=[&](int xx,int yy){auto v=heights_[std::size_t(std::clamp(yy,0,int(grid_.rows)-1))*grid_.columns+
            std::clamp(xx,0,int(grid_.columns)-1)];return std::isfinite(v)?double(v):z;};
        double latitude=grid_.north-y*grid_.latitude_step;
        double dx=std::max(1.,111195*grid_.longitude_step*std::cos(latitude*std::acos(-1.)/180));
        double dy=111195*grid_.latitude_step;
        // Northwest illumination, exaggerated slopes for readable overview relief.
        double east=(height(x+1,y)-height(int(x)-1,y))/(2*dx)*12;
        double north=(height(x,int(y)-1)-height(x,y+1))/(2*dy)*12;
        double light=std::clamp((east*.5-north*.5+.7071)/std::sqrt(east*east+north*north+1),0.,1.);
        double factor=.52+.65*light;
        b[index]={static_cast<unsigned char>(std::clamp(a[index].r*factor,0.,255.)),
                  static_cast<unsigned char>(std::clamp(a[index].g*factor,0.,255.)),
                  static_cast<unsigned char>(std::clamp(a[index].b*factor,0.,255.)),255};
    }
    coloured_=LoadTextureFromImage(plain);shaded_=LoadTextureFromImage(shade);
    UnloadImage(plain);UnloadImage(shade);
    SetTextureFilter(coloured_,TEXTURE_FILTER_BILINEAR);SetTextureFilter(shaded_,TEXTURE_FILTER_BILINEAR);
}
ReliefView::~ReliefView(){UnloadTexture(coloured_);UnloadTexture(shaded_);}
void ReliefView::draw(Rectangle destination,bool shaded)const {
    auto texture=shaded?shaded_:coloured_;
    DrawTexturePro(texture,{0,0,float(texture.width),float(texture.height)},destination,{0,0},0,WHITE);
}
std::optional<double> ReliefView::sample(double lon,double lat)const {
    lon=elysia::geo::wrap_longitude(lon);lon+=360*std::ceil((grid_.west-lon)/360);
    double x=(lon-grid_.west)/grid_.longitude_step,y=(grid_.north-lat)/grid_.latitude_step;
    if(x<0||y<0||x>grid_.columns-1||y>grid_.rows-1)return std::nullopt;
    auto x0=std::uint32_t(std::floor(x)),y0=std::uint32_t(std::floor(y));
    double value=0;
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){
        double w=(i?x-x0:1-x+x0)*(j?y-y0:1-y+y0);if(w==0)continue;
        auto z=heights_[std::size_t(std::min(y0+j,grid_.rows-1))*grid_.columns+std::min(x0+i,grid_.columns-1)];
        if(!std::isfinite(z))return std::nullopt;value+=w*z;
    }
    return value;
}
