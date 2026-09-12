#include <elysia_geo/map.hpp>
#include "viewer_relief.hpp"
#include <memory>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

using namespace elysia::geo;
namespace {
constexpr Color ocean{10,23,34,255}, land{37,63,69,255}, ink{204,223,225,255};
constexpr Color muted{121,157,166,255}, accent{91,225,196,255};
struct MapCamera {
    double lon=-28, lat=39, scale=7;
    Rectangle viewport{};
    Point geographic(Vector2 p) const {
        return {lon+(p.x-viewport.x-viewport.width/2)/scale,
                lat-(p.y-viewport.y-viewport.height/2)/scale};
    }
    Vector2 screen(double longitude,double latitude) const {
        return {float(viewport.x+viewport.width/2+(longitude-lon)*scale),
                float(viewport.y+viewport.height/2-(latitude-lat)*scale)};
    }
};
// Rasterization is a presentation cache. Server queries use original polygons.
Texture2D make_basemap(const MapDataset& map) {
    constexpr int w=4096,h=2048;
    Image image=GenImageColor(w,h,ocean);
    auto* pixels=static_cast<Color*>(image.data);
    for(const auto& polygon:map.polygons()) {
        int begin=std::clamp(int((90-polygon.bounds.north)/180*h),0,h-1);
        int end=std::clamp(int((90-polygon.bounds.south)/180*h)+1,0,h-1);
        std::vector<double> crossings;
        for(int y=begin;y<=end;++y) {
            crossings.clear(); double latitude=90-(y+.5)*180/h;
            for(const auto& ring:polygon.rings) for(std::size_t i=1;i<ring.size();++i) {
                auto a=ring[i-1],b=ring[i];
                if((a.y>latitude)!=(b.y>latitude))
                    crossings.push_back((a.x+(b.x-a.x)*(latitude-a.y)/(b.y-a.y)+180)/360*w);
            }
            std::sort(crossings.begin(),crossings.end());
            for(std::size_t i=1;i<crossings.size();i+=2) {
                int left=std::clamp(int(std::ceil(crossings[i-1]-.5)),0,w);
                int right=std::clamp(int(std::ceil(crossings[i]-.5)),0,w);
                for(int x=left;x<right;++x) pixels[y*w+x]=land;
            }
        }
    }
    auto texture=LoadTextureFromImage(image); UnloadImage(image);
    SetTextureFilter(texture,TEXTURE_FILTER_BILINEAR); return texture;
}
void label(const char* text,int x,int y,int size,Color color=ink) {DrawText(text,x,y,size,color);}
void marker(const MapCamera& c,Position p,const char* text,Color color) {
    double lon=c.lon+wrap_longitude(p.longitude_deg-c.lon);
    auto s=c.screen(lon,p.latitude_deg);
    DrawCircleLines(int(s.x),int(s.y),8,color);
    DrawLine(int(s.x)-12,int(s.y),int(s.x)+12,int(s.y),color);
    DrawLine(int(s.x),int(s.y)-12,int(s.x),int(s.y)+12,color);
    label(text,int(s.x)+15,int(s.y)-8,16,color);
}
}
int main(int argc,char** argv) {
    try {
        std::string file=ELYSIA_MAP_FILE,capture,terrain;
        auto example=std::filesystem::path(ELYSIA_MAP_FILE).parent_path().parent_path()/"mediterranean/terrain.elyhgt";
        if(std::filesystem::exists(example))terrain=example.string();
        int mode=1;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if((arg=="--map"||arg=="--capture"||arg=="--terrain"||arg=="--mode")&&i+1<argc) {
                if(arg=="--map") file=argv[++i];
                else if(arg=="--terrain") terrain=argv[++i];
                else if(arg=="--mode") {
                    std::string name=argv[++i];
                    if(name=="coast")mode=0;else if(name=="relief")mode=1;else if(name=="height")mode=2;
                    else throw std::invalid_argument("Mode must be coast, relief or height");
                } else capture=argv[++i];
            } else if(arg=="--help") {
                std::cout<<"geo-viewer [--map land.elygeo] [--terrain terrain.elyhgt] [--mode coast|relief|height] [--capture image.png]\n";return 0;
            } else throw std::invalid_argument("Unknown or incomplete viewer argument");
        }
        auto map=MapDataset::open(file);
        SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
        InitWindow(1440,900,"ElysiaGeo | Earth operations map");
        SetWindowMinSize(1100,800); SetTargetFPS(60);
        auto texture=make_basemap(map);
        std::unique_ptr<ReliefView> relief;
        if(!terrain.empty())relief=std::make_unique<ReliefView>(terrain);
        else mode=0;
        MapCamera camera;
        auto home=[&] {
            if(relief) {
                auto g=relief->grid();
                camera.lon=g.west+(g.columns-1)*g.longitude_step/2;
                camera.lat=g.north-(g.rows-1)*g.latitude_step/2;
                camera.scale=std::min(double(GetScreenWidth()-300)/std::max(1.,(g.columns-1)*g.longitude_step),
                    double(GetScreenHeight()-152)/std::max(1.,(g.rows-1)*g.latitude_step));
            } else {camera.lon=-28;camera.lat=39;camera.scale=7;}
        };
        home();
        std::optional<Position> start,end;
        if(!capture.empty()&&!relief) {start=Position{-63.57,44.65,0};end=Position{-21.94,64.15,0};}
        int frame=0;
        while(!WindowShouldClose()) {
            int width=GetScreenWidth(),height=GetScreenHeight();
            camera.viewport={280,92,float(width-300),float(height-152)};
            auto mouse=GetMousePosition(); bool over=CheckCollisionPointRec(mouse,camera.viewport);
            if(IsKeyPressed(KEY_HOME)) home();
            if(relief&&IsKeyPressed(KEY_H))mode=(mode+1)%3;
            if(IsKeyPressed(KEY_G)) {camera.lon=0;camera.lat=0;camera.scale=camera.viewport.width/360;}
            if(IsKeyPressed(KEY_C)) {start.reset();end.reset();}
            if(over) {
                auto before=camera.geographic(mouse);
                camera.scale=std::clamp(camera.scale*std::pow(1.18,GetMouseWheelMove()),
                    double(camera.viewport.width)/360,120.0);
                auto after=camera.geographic(mouse);
                camera.lon+=before.x-after.x;camera.lat+=before.y-after.y;
                if(IsMouseButtonDown(MOUSE_BUTTON_RIGHT)&&!IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    auto delta=GetMouseDelta();camera.lon-=delta.x/camera.scale;camera.lat+=delta.y/camera.scale;
                }
            }
            camera.lon=wrap_longitude(camera.lon);
            double latitude_limit=std::max(0.0,90-camera.viewport.height/(2*camera.scale));
            camera.lat=std::clamp(camera.lat,-latitude_limit,latitude_limit);
            auto cursor=camera.geographic(mouse);
            bool valid=over&&cursor.y>=-90&&cursor.y<=90;
            if(valid&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Position p{wrap_longitude(cursor.x),cursor.y,0};
                if(!start||end) {start=p;end.reset();} else end=p;
            }
            BeginDrawing(); ClearBackground({7,15,23,255});
            label("ELYSIA / GEO",24,22,26,accent);
            label("EARTH OPERATIONS",280,22,30);
            label(mode==1?"SHADED RELIEF   /   OFFLINE HEIGHTMAP":mode==2?"HEIGHT COLOURS   /   OFFLINE HEIGHTMAP":"COASTLINES   /   OFFLINE VECTOR DATA",282,58,15,muted);
            label("01  TACTICAL MAP",24,112,19,accent);
            label(mode?"HEIGHT + SEA DEPTH":"NATURAL EARTH",24,162,18);
            label(mode?"Signed heights in metres":"Land polygons / 1:50M",24,189,16,muted);
            const char* reference="Reference unspecified";
            if(relief) switch(relief->grid().reference) {
                case HeightReference::mean_sea_level:reference="Height: mean sea level";break;
                case HeightReference::egm96:reference="Height: EGM96";break;
                case HeightReference::egm2008:reference="Height: EGM2008";break;
                case HeightReference::wgs84_ellipsoid:reference="Height: WGS84 ellipsoid";break;
                default:break;
            }
            label(mode?reference:"WGS84 lon / lat",24,216,16,muted);
            label("Plate carree projection",24,243,16,muted);
            DrawLine(24,284,252,284,{41,62,72,255});
            label("NAVIGATION",24,308,17);
            label("Right drag   Pan",24,342,16,muted);
            label("Wheel        Zoom",24,370,16,muted);
            label("Home         Fit dataset",24,398,16,muted);
            label("G            Whole Earth",24,426,16,muted);
            label("Left click   Measure A/B",24,454,16,muted);
            label("C Clear    H Layer",24,482,16,muted);
            DrawLine(24,522,252,522,{41,62,72,255});
            label("MEASUREMENT",24,546,17);
            if(start&&end) {
                label(TextFormat("%.1f km",spherical_distance_m(*start,*end)/1000),24,583,28,accent);
                auto bearing=initial_bearing_deg(*start,*end);
                label(bearing?TextFormat("Initial bearing %.1f deg",*bearing):"Bearing undefined",24,621,16,muted);
                label("Spherical surface distance",24,650,15,muted);
            } else label(start?"Choose destination B":"Choose starting point A",24,584,16,muted);
            if(relief&&mode) {
                label("HEIGHT / DEPTH",24,690,15,accent);
                for(int i=0;i<210;++i) {
                    double z=-6000+i/209.*10500;
                    DrawLine(24+i,718,24+i,730,ReliefView::colour(z));
                }
                label("-6000",24,740,13,muted);
                label("0",140,740,13,muted);
                label("4500 m",192,740,13,muted);
                label("H: coast / relief / height",24,height-72,14,muted);
            } else {
                label("OVERVIEW GEOGRAPHY",24,height-98,15,accent);
                label("Coastline layer",24,height-72,15,muted);
            }
            BeginScissorMode(int(camera.viewport.x),int(camera.viewport.y),int(camera.viewport.width),int(camera.viewport.height));
            DrawRectangleRec(camera.viewport,ocean);
            for(int copy=-2;copy<=2;++copy) {
                auto origin=camera.screen(-180+copy*360,90);
                DrawTexturePro(texture,{0,0,float(texture.width),float(texture.height)},
                    {origin.x,origin.y,float(360*camera.scale),float(180*camera.scale)},{0,0},0,WHITE);
            }
            if(relief&&mode) {
                auto g=relief->grid();
                for(int copy=-1;copy<=1;++copy) {
                    auto origin=camera.screen(g.west-g.longitude_step/2+copy*360,g.north+g.latitude_step/2);
                    relief->draw({origin.x,origin.y,float(g.columns*g.longitude_step*camera.scale),
                        float(g.rows*g.latitude_step*camera.scale)},mode==1);
                }
            }
            int step=camera.scale>30?5:camera.scale>12?10:30;
            double left=camera.geographic({camera.viewport.x,0}).x;
            double right=left+camera.viewport.width/camera.scale;
            for(double lon=std::floor(left/step)*step;lon<=right;lon+=step) {
                auto top=camera.screen(lon,90),bottom=camera.screen(lon,-90);
                DrawLineV(top,bottom,{73,115,125,65});
                label(TextFormat("%.0f",wrap_longitude(lon)),int(top.x)+5,int(camera.viewport.y)+8,13,muted);
            }
            for(int lat=-90;lat<=90;lat+=step) {
                auto a=camera.screen(left,lat),b=camera.screen(right,lat);
                DrawLineV(a,b,{73,115,125,65});label(TextFormat("%d",lat),int(a.x)+8,int(a.y)+5,13,muted);
            }
            // Vector coastline preserves detail when the raster cache is enlarged.
            for(const auto& polygon:map.polygons()) for(int copy=-1;copy<=1;++copy) {
                if(polygon.bounds.east+copy*360<left||polygon.bounds.west+copy*360>right) continue;
                for(const auto& ring:polygon.rings) for(std::size_t i=1;i<ring.size();++i) {
                    auto a=camera.screen(ring[i-1].x+copy*360,ring[i-1].y);
                    auto b=camera.screen(ring[i].x+copy*360,ring[i].y);
                    DrawLineV(a,b,mode?Color{26,48,51,155}:Color{105,153,151,160});
                }
            }
            if(relief&&mode) {
                auto g=relief->grid();
                DrawRectangle(int(camera.viewport.x)+16,int(camera.viewport.y)+int(camera.viewport.height)-66,420,48,{7,15,23,220});
                label(TextFormat("RASTER  %.3f deg   /   %u x %u samples",g.longitude_step,g.columns,g.rows),
                    int(camera.viewport.x)+28,int(camera.viewport.y+camera.viewport.height)-55,15,ink);
                label(mode==1?"NW illumination / slope exaggeration 12x":"Height colours / no artificial illumination",
                    int(camera.viewport.x)+28,int(camera.viewport.y+camera.viewport.height)-34,14,muted);
            }
            if(start) marker(camera,*start,"A",accent);
            if(end) marker(camera,*end,"B",{255,197,115,255});
            // Display a sampled great-circle, not a misleading straight map chord.
            if(start&&end) {
                auto unit=[](Position p) {double r=std::acos(-1.0)/180,lat=p.latitude_deg*r,lon=p.longitude_deg*r;
                    return Vec3{std::cos(lat)*std::cos(lon),std::cos(lat)*std::sin(lon),std::sin(lat)};};
                auto a=unit(*start),b=unit(*end);
                double angle=std::acos(std::clamp(a.x*b.x+a.y*b.y+a.z*b.z,-1.0,1.0));
                if(angle>1e-9&&angle<std::acos(-1.0)-1e-6) {
                    Vector2 previous{};
                    for(int i=0;i<=128;++i) {
                        double t=i/128.0,u=std::sin((1-t)*angle)/std::sin(angle),v=std::sin(t*angle)/std::sin(angle);
                        Vec3 p{u*a.x+v*b.x,u*a.y+v*b.y,u*a.z+v*b.z};
                        double lon=std::atan2(p.y,p.x)*180/std::acos(-1.0),lat=std::atan2(p.z,std::hypot(p.x,p.y))*180/std::acos(-1.0);
                        auto point=camera.screen(camera.lon+wrap_longitude(lon-camera.lon),lat);
                        if(i&&std::abs(point.x-previous.x)<180*camera.scale) DrawLineEx(previous,point,2,accent);
                        previous=point;
                    }
                }
            }
            EndScissorMode();
            DrawRectangleLinesEx(camera.viewport,1,{56,86,95,255});
            if(valid) {
                auto surface=map.surface({cursor.x,cursor.y,0});
                label(TextFormat("%8.3f LON   %7.3f LAT   /   %s",wrap_longitude(cursor.x),cursor.y,
                    surface==Surface::land?"LAND":surface==Surface::water?"WATER":"BOUNDARY"),282,height-40,17,accent);
                if(relief) {
                    auto z=relief->sample(cursor.x,cursor.y);
                    label(z?TextFormat("HEIGHT %+.0f m",*z):"HEIGHT unavailable",width-270,height-40,17,ink);
                }
            } else label("READY  /  Geographic data independent of rendering",282,height-40,17,muted);
            EndDrawing();
            if(!capture.empty()&&++frame==3) {
                auto screenshot=LoadImageFromScreen();
                bool saved=ExportImage(screenshot,capture.c_str());UnloadImage(screenshot);
                if(!saved) throw std::runtime_error("Cannot save screenshot: "+capture);
                break;
            }
        }
        relief.reset();UnloadTexture(texture);CloseWindow();
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
