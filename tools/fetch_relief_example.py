#!/usr/bin/env python3
"""Build the offline Mediterranean example from NOAA ETOPO1 (3 arc-minute samples)."""
import csv
import hashlib
import io
import json
from pathlib import Path
import tempfile
import urllib.request
from import_heightmap import convert

URL = ('https://coastwatch.pfeg.noaa.gov/erddap/griddap/etopo180.csv?'
       'altitude%5B(25):3:(50)%5D%5B(-12):3:(38)%5D')
DESTINATION = Path(__file__).resolve().parents[1] / 'maps' / 'mediterranean'


def main():
    print('Downloading NOAA ETOPO1 Mediterranean elevation and bathymetry...', flush=True)
    with urllib.request.urlopen(URL, timeout=120) as response:
        raw = response.read()
    records = csv.reader(io.StringIO(raw.decode()))
    if next(records) != ['latitude', 'longitude', 'altitude']:
        raise ValueError('Unexpected NOAA response columns')
    if next(records) != ['degrees_north', 'degrees_east', 'm']:
        raise ValueError('Unexpected NOAA response units')
    values = []
    for index, row in enumerate(records):
        lat, lon, height = map(float, row)
        y, x = divmod(index, 1001)
        if abs(lat - (25+y*.05)) > 1e-7 or abs(lon - (-12+x*.05)) > 1e-7:
            raise ValueError('Unexpected grid order or spacing')
        values.append('-99999' if height != height or height == 32767 else str(height))
    if len(values) != 501*1001:
        raise ValueError('Incomplete NOAA raster')
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'region.asc'
        with source.open('w') as stream:
            stream.write('ncols 1001\nnrows 501\nxllcenter -12\nyllcenter 25\ncellsize 0.05\nNODATA_value -99999\n')
            for y in reversed(range(501)):
                stream.write(' '.join(values[y*1001:(y+1)*1001]) + '\n')
        convert(source, DESTINATION / 'terrain.elyhgt', reference='mean_sea_level', tile_size=128)
    manifest = {
        'source': 'NOAA ETOPO1 Ice Surface, sampled every third grid node',
        'source_url': URL, 'source_sha256': hashlib.sha256(raw).hexdigest(),
        'geometry_sha256': hashlib.sha256((DESTINATION/'terrain.elyhgt').read_bytes()).hexdigest(),
        'coverage_centres': [-12, 25, 38, 50], 'spacing_degrees': .05,
        'vertical_reference': 'Mean sea level, as declared by ETOPO1; not an EGM conversion',
        'format': 'ELYHGT01', 'columns': 1001, 'rows': 501,
    }
    (DESTINATION/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Saved', DESTINATION/'terrain.elyhgt', flush=True)


if __name__ == '__main__':
    main()
