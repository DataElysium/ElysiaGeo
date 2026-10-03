# Global relief base

Source: NOAA NGDC **ETOPO1 Ice Surface**, via NOAA CoastWatch ERDDAP.
The source combines land topography and ocean bathymetry; this offline package
samples every third source node, giving 0.05-degree (3 arc-minute) spacing.
It contains 7201 × 3601 samples including the longitude endpoints and poles.
No source samples were missing in the downloaded grid.

- Metadata: https://coastwatch.pfeg.noaa.gov/erddap/info/etopo180/index.html
- Exact query, source/package SHA-256 hashes and preprocessing: `manifest.json`
- Rebuild: install `numpy scipy`, then `python3 tools/fetch_global_relief.py`.
- An existing download can be reused with `--source-file /path/to/global.nc`.

The source declares WGS84 horizontal coordinates and mean sea level vertically.
The package preserves those labels; it does not perform a geoid conversion.
At the duplicated ±180-degree meridian, 103 source rows differed by at most
2 metres. The +180 column is made identical to -180 to match longitude wrapping
and keep bilinear sampling continuous. This is recorded in the manifest.

The package is about 108 MB; runtime queries load source tiles as needed.
The grid is sampled, not averaged or conservatively reduced. It supports broad
game terrain, not guaranteed seabed clearance in harbours or narrow shoals.
Ice-covered regions describe the ice surface, not under-ice bathymetry.
Land/water classification remains a separate coastline layer.

Attribution: Amante, C. and B. W. Eakins (2009), ETOPO1 1 Arc-Minute Global Relief
Model: Procedures, Data Sources and Analysis, NOAA Technical Memorandum NESDIS
NGDC-24. DOI: https://doi.org/10.7289/V5C8276M

NOAA's ERDDAP metadata permits free use and redistribution with no warranty of
accuracy or fitness. This is a game/visualization base, not a navigation chart.

Repository packaging: `terrain.elyhgt.gz` is a lossless copy of the raster, not a different resolution.
Restore offline with `python3 tools/fetch_global_relief.py --restore-packaged`; the tool verifies
the expanded SHA-256 against `manifest.json` before replacing the runtime file.
