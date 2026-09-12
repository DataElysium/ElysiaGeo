# Natural Earth land package

Made with Natural Earth: https://www.naturalearthdata.com/

Natural Earth raster and vector data are public domain:
https://www.naturalearthdata.com/about/terms-of-use/

Input: `geojson/ne_50m_land.geojson` in `nvkelso/natural-earth-vector`, revision
`ca96624a56bd078437bca8184e78163e5039ad19`.

The manifest records the exact URL and hashes. Conversion removes feature
properties and writes longitude/latitude polygon rings into a little-endian
binary. Coordinates are preserved as float64; geometry is not simplified again.
The source is a 1:50,000,000 overview dataset, not 50-metre resolution.

This package includes only land polygons, without political borders, base locations,
or faction ownership. Those are separate scenario layers. It does not provide
complete inland-water classification, elevation, or bathymetry.
