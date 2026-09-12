# ElysiaGeo

An offline Earth geography foundation for simulation applications. The C++20
library has no ECS, graphics, networking, or third-party runtime dependencies.
The optional raylib client provides a classic tactical map. This is the first
map milestone for the fleet game, not yet its simulation or 3D client.

## Build and run

```sh
xmake f -P . -m release -y
xmake -P .
xmake run -P . geo-viewer
xmake run -P . geo-tests
python3 -m unittest discover -s tests -p 'test_*.py'
```

Right-drag pans, the wheel zooms around the cursor, Home returns to the North
Atlantic, and G shows Earth. Click A and B to measure a spherical surface distance
and initial bearing; C clears them. The connecting curve is a great-circle
measurement, **not a navigable route**. Latitude/longitude and land classification
appear under the cursor. Escape closes the viewer.

```sh
# A server build does not fetch or link raylib.
xmake f -P . --viewer=n -m release -y
xmake -P .
xmake run -P . geo-query -100 40
xmake run -P . geo-query -30 30
```

The default dataset path is baked into development executables. For distribution,
ship `maps/earth` and specify `geo-viewer --map /path/to/land.elygeo` or pass the
geometry path as the third argument to `geo-query`. `geo-viewer --capture out.png`
renders a reproducible Atlantic measurement and exits; it needs a graphics display.

## Library use

```cpp
#include <elysia_geo/map.hpp>

namespace geo = elysia::geo;
auto earth = geo::MapDataset::open("maps/earth/land.elygeo");
auto surface = earth.surface({-100.0, 40.0, 0.0});
auto islands = earth.query({170.0, -30.0, -170.0, 0.0}); // crosses dateline
for (auto index : islands) {
    const auto& polygon = earth.polygons()[index];
    // First ring is exterior; subsequent rings are holes. No renderer types.
}
geo::LocalFrame frame({-63.57, 44.65, 0.0});
auto east_north_up = frame.to_local({-63.56, 44.66, 100.0});
```

`query` returns polygons whose **bounds overlap** the region; it does not clip
geometry or promise exact polygon intersection. IDs are indices within this
dataset, not persistent application IDs. Const queries can run concurrently.
File loading and invalid inputs throw exceptions. `Surface::boundary` explicitly
represents points on polygon edges; callers choose their own movement policy.

Positions use longitude/latitude degrees. Altitude is metres above the WGS84
ellipsoid, not a sea-level height datum. LocalFrame uses WGS84 ECEF and an ENU
rotation; it does not flatten Earth curvature for movement. ECEF inverse supports
near-surface and exterior positions (points near Earth's centre are rejected).
Distances and bearings are explicitly **spherical approximations**, not precise
ellipsoidal geodesics. Altitude is ignored by surface distance. Coincident and
antipodal bearings return `nullopt`.

## Data and scope

The included approximately 960 KiB binary contains 1,421 Natural Earth 1:50 million land polygons.
The [manifest](maps/earth/manifest.json) pins the upstream Git revision and records
source and generated SHA-256 hashes. [Source attribution](maps/earth/SOURCE.md).
Runtime use is entirely offline; source GeoJSON is only needed when rebuilding.

The library currently loads the small global polygon package into memory and
builds a 10-degree uniform spatial index. The viewer makes a separate 4096x2048
raster cache, then draws vector outlines. Zoom does not change server query data.
No imagery or cached GPU assets live in the library.

This is **generalized continental land**, not a navigational chart. Inland lakes
may be represented as land, small islands may be absent, and zooming does not add
source detail. The bundled geometry is 2D. Optional height raster storage and sampling are
available below; real elevation/bathymetry packages, detailed theater data,
automatic terrain streaming/LOD, pathfinding, ECS integration, and a 3D viewer
are not yet provided. Missing height coverage is not represented as a fabricated zero.

See [architecture and package format](docs/architecture.md) for extension boundaries.

## Rebuild the packaged data

Download `source_url` from the manifest to a local GeoJSON file, then run:

```sh
python3 tools/import_geojson.py source.geojson maps/earth \
  --source-url URL_FROM_MANIFEST --scale 50000000
```

The importer accepts Polygon/MultiPolygon FeatureCollections in longitude/latitude
(RFC 7946 or an explicit OGC CRS84 declaration). Source polygons must already be
split at the dateline. It preserves rings and holes; it does not reproject, repair
topology, or simplify. Use an established GIS preprocessing tool for other inputs.

Tests cover coordinate round trips including poles, geographic dateline handling,
real island/land/ocean queries, synthetic polygon holes and boundaries, spatial
index selection against brute force, malformed/truncated packages, and importer
validation/reproducibility. Linux GCC release and Clang ASan/UBSan were checked
during initial implementation; other platforms remain to be tested.

## Optional heightmap layers

`Heightmap` reads tiled, signed float32 height rasters independently of coastlines,
ECS and graphics. Use separate packages for different layers or resolutions;
there is no automatic LOD selection or layer catalog yet. Negative seabed heights
use the same representation as positive land heights. They are not automatically
converted into submarine depth or water classification.

```cpp
#include <elysia_geo/heightmap.hpp>

auto terrain = elysia::geo::Heightmap::open("terrain.elyhgt");
auto height = terrain.sample({14.5, 35.9, 0});
if (height) {
    // height->metres: signed height relative to height->reference.
    // Do not assign orthometric heights directly to WGS84 ellipsoid altitude.
}
auto tile = terrain.read_tile(0, 0); // Owns a contiguous, north-to-south float array.
```

Opening validates metadata and file length without loading all samples. Tile reads
validate the data they consume. `sample` opens the file and reads up to four cells;
it is a convenience API, not a high-throughput cached sampler. Keep `read_tile`
buffers in an application cache for mesh generation or bulk processing. The library
does not retain buffers, start background work or require an ECS tile entity.
Const calls may run concurrently provided the package is not modified externally.

Import a WGS84 longitude/latitude ESRI ASCII grid, with heights already in metres:

```sh
python3 tools/import_heightmap.py region.asc region.elyhgt --reference egm96
xmake run -P . geo-query --heightmap 14.5 35.9 region.elyhgt
```

The importer supports corner/centre grid origins and a NoData marker. It buffers
one tile-height strip and writes the package atomically. Other raster formats
(e.g. GeoTIFF), projections, depth-positive-down values and unit conversions must
be prepared offline first. ASCII grids contain no reliable CRS metadata: supplying
WGS84 geographic coordinates is the caller's responsibility. Choose the vertical
reference from the source metadata, not from its horizontal coordinate system.
The optional Mediterranean example below supplies real elevation and seabed data
for the viewer. The core library remains independent of the relief renderer.


## Shaded-relief viewer

The optional `maps/mediterranean/terrain.elyhgt` package contains NOAA ETOPO1
land elevations and seabed heights sampled at 3 arc-minute spacing (0.05 degrees).
Regenerate it with `python3 tools/fetch_relief_example.py`; network access is only
needed for this preprocessing step. Source attribution and hashes accompany it.

```sh
xmake run -P . geo-viewer
xmake run -P . geo-viewer --terrain maps/mediterranean/terrain.elyhgt --mode relief
xmake run -P . geo-viewer --mode height --capture docs/height-colours.png
```

The viewer detects the example when present. **H** cycles coastline, shaded relief
and height colours; **Home** fits the height dataset, **G** shows the globe. Cursor
heights use cached raster values, with unavailable coverage reported explicitly.
Northwest lighting exaggerates slopes 12 times for legibility; stored heights and
cursor readings are unchanged. The colour legend is in signed metres. Coastline
polygons and elevation come from different generalized sources, so shore edges
can disagree. Height sign is not a substitute for authoritative land/water data.

The presentation adapter in `apps/viewer_relief.*` loads tiles once into a CPU
cache and generates two textures. It is limited to 16 million samples and is an
initial regional viewer, not a global streaming/LOD implementation. Outside its
coverage the coastline basemap remains visible. Input heightmaps retain their
vertical reference; the ETOPO1 example uses source-declared mean sea level.
