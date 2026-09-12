# Geography boundary

```text
Source GIS files -> offline converter -> versioned map package
                                            |
                                C++ geography library
                                  /               \
                       simulation queries     presentation adapters
                       (server resource)       /              \
                                         tactical 2D      local 3D (future)
```

The game owns units, ports, bases, faction knowledge, orders, and objectives.
ElysiaGeo owns immutable geographic geometry and coordinate math. The game can
hold `shared_ptr<const MapDataset>` in a resource without changing this library.
Coastline vertices are not entities. LocalFrame provides the coordinate boundary
for a future 3D adapter; it is not a terrain renderer.

The server and client should select the same package hash in session setup.
Do not use geometry indices as persistent IDs. The manifest hashes are provenance
and future handshake inputs: the current geometry reader validates binary structure
but does not parse the manifest or verify its hashes at runtime. No transport or
session handshake is implemented here.

## Package v1

`manifest.json` records format, source revision/URL, hashes, coverage and scale.
`land.elygeo` is an explicitly little-endian stream, never a dumped C++ struct:

| Field | Encoding |
|---|---|
| Magic/version | 8 ASCII bytes `ELYGEO01` |
| Polygon count | uint32 |
| Per polygon: ring count | uint32 |
| Per ring: point count | uint32 |
| Per point: longitude, latitude | two IEEE 754 binary64 numbers |

Rings are explicitly closed and contain at least four points. Ring zero is the
exterior; later rings are holes. Coordinates are within [-180,180] / [-90,90].
Polygons must be dateline-split before import, with polar closure edges permitted.
Neither the importer nor reader is a full topology validator: simple valid polygons
and properly nested holes are an input contract. Boundary tolerance is 1e-10
degrees for the segment test. Heights are absent from this format.

The loader rejects unsupported magic, truncated records, nonfinite/out-of-range
coordinates, impossible element counts, unclosed rings, and trailing bytes.
It computes bounds and indexes polygon IDs in 36x18 geographic cells. Point queries
use candidate bounds followed by exterior/hole tests. Region queries collect and
deduplicate overlapping bounds. All post-load methods are read-only.

## Next extensions

1. A dataset catalog that validates manifest/hash and selects detailed theater
   layers. Keep overview and navigation resolution explicit.
2. Build on the independent heightmap reader below with real elevation packages
   and application tile caches; retain explicit missing coverage and vertical datum.
3. A local 3D adapter consuming that reader and LocalFrame, owning its own mesh/GPU
   cache. Server code never needs to construct it.
4. A game geography plugin and server-owned movement test. Route planning must be
   a separate algorithm with vessel-specific constraints, not a method of the renderer.

Avoid adding speculative plugin ABIs or a generic GIS engine before these actual
consumers need them. The current binary is an early format, not a frozen public ABI.

## Height package v1 (`ELYHGT01`)

Independent raster packages supplement the polygon format. Each describes one
regular geographic grid at one resolution. The 56-byte little-endian header is:

| Field | Encoding |
|---|---|
| Magic | 8 ASCII bytes `ELYHGT01` |
| Columns, rows, tile side | Three uint32 values |
| Vertical reference | uint32: 0 unspecified, 1 WGS84 ellipsoid, 2 EGM96, 3 EGM2008, 4 mean sea level |
| West, north sample centres | Two float64 degrees |
| Longitude, latitude spacing | Two positive float64 degrees |

Tiles follow in row-major order, each containing `tile_side²` float32 samples in
row-major order, north to south. Edge tiles are padded with NaN. Tile coordinates
are dataset-local; layer identity and resolution belong to the containing package
or application catalog. `read_tile` removes padding in its returned array.
Heights are signed metres, positive upward, and NaN represents missing data.
Infinity is rejected on read. File length must exactly match the declared tiles.

Sampling interpolates between sample centres, including across internal tile
boundaries. It returns no value outside centre coverage or when a contributing
sample is missing. There is no half-cell extrapolation. Longitude may extend east
of 180 internally, permitting dateline-crossing regional grids. A global raster
whose endpoint sample centres do not meet has a seam gap: v1 does not implicitly
interpolate periodically across that gap. Preprocessing may add a duplicate seam
column (a 360-degree centre span is supported). Heights retain their declared
vertical reference; neither ECEF conversion nor sea-level conversion is implicit.

This version intentionally leaves resolution selection, periodic padding, tile
caching, asynchronous I/O, ECS residency and GPU meshes to separate consumers.
