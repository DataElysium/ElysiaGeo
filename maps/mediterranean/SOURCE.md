# Mediterranean relief example

Source: NOAA NGDC **ETOPO1, Ice Surface**, accessed through NOAA CoastWatch ERDDAP.
ETOPO1 integrates land topography and ocean bathymetry. This package samples every
third node of the original 1 arc-minute grid, yielding 0.05-degree spacing. It is
not averaged/downsampled bathymetry suitable for clearance guarantees.

- Dataset information: https://coastwatch.pfeg.noaa.gov/erddap/info/etopo180/index.html
- Source description: https://www.ncei.noaa.gov/products/etopo-global-relief-model
- Exact extraction URL and source/package SHA-256 hashes: `manifest.json`
- Rebuild: `python3 tools/fetch_relief_example.py`

Attribution: Amante, C. and B. W. Eakins (2009), ETOPO1 1 Arc-Minute Global Relief
Model: Procedures, Data Sources and Analysis, NOAA Technical Memorandum NESDIS
NGDC-24. DOI: https://doi.org/10.7289/V5C8276M

NOAA's ERDDAP metadata permits free use and redistribution, with no warranty of
accuracy or fitness. This is a game/visualization overview, not a navigation chart.
The source declares WGS84 horizontal coordinates and mean sea level vertically;
we preserve that label without asserting a more specific geoid realization.
Negative values are below the vertical reference, not necessarily open water
(e.g. depressions on land). Coastline classification remains a separate layer.
