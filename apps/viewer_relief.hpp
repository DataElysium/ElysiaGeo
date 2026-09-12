#pragma once
#include <elysia_geo/heightmap.hpp>
#include <raylib.h>

// Presentation-only CPU raster and GPU cache; no renderer dependency in ElysiaGeo.
class ReliefView {
public:
    explicit ReliefView(const std::filesystem::path& file);
    ~ReliefView();
    ReliefView(const ReliefView&)=delete;
    ReliefView& operator=(const ReliefView&)=delete;
    const elysia::geo::HeightGrid& grid() const { return grid_; }
    std::optional<double> sample(double longitude,double latitude) const;
    void draw(Rectangle destination,bool shaded) const;
    static Color colour(double height);
private:
    elysia::geo::HeightGrid grid_;
    std::vector<float> heights_;
    Texture2D shaded_{},coloured_{};
};
