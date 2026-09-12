#include <elysia_geo/map.hpp>
#include <elysia_geo/heightmap.hpp>
#include <iostream>
#include <string>

int main(int argc,char** argv) {
    try {
        bool heightmap = argc == 5 && std::string_view(argv[1]) == "--heightmap";
        if(!heightmap && argc!=3 && argc!=4) {
            std::cerr<<"Usage: geo-query LONGITUDE LATITUDE [land.elygeo]\n       geo-query --heightmap LONGITUDE LATITUDE terrain.elyhgt\n"; return 2;
        }
        auto parse=[](const char* input) {
            std::string text(input); std::size_t used;
            double v=std::stod(text,&used);
            if(used!=text.size()) throw std::invalid_argument("Invalid coordinate argument");
            return v;
        };
        if(heightmap) {
            auto terrain=elysia::geo::Heightmap::open(argv[4]);
            auto sample=terrain.sample({parse(argv[2]),parse(argv[3]),0});
            if(sample) std::cout<<sample->metres<<" m; reference="<<static_cast<unsigned>(sample->reference)
                                <<"; spacing="<<sample->longitude_step<<","<<sample->latitude_step<<" degrees\n";
            else std::cout<<"unavailable\n";
            return 0;
        }
        auto map=elysia::geo::MapDataset::open(argc==4?argv[3]:ELYSIA_MAP_FILE);
        auto surface=map.surface({parse(argv[1]),parse(argv[2]),0});
        std::cout<<(surface==elysia::geo::Surface::land?"land":surface==elysia::geo::Surface::water?"water":"boundary")<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
