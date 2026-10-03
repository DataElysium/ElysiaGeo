#!/usr/bin/env python3
"""Build the offline 0.05-degree ETOPO1 global base. Requires numpy and scipy.

Downloads ~52 MB NetCDF and writes ~108 MB ELYHGT01. No network access at game runtime.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import urllib.request


URL = ('https://coastwatch.pfeg.noaa.gov/erddap/griddap/etopo180.nc?'
       'altitude%5B(-90):3:(90)%5D%5B(-180):3:(180)%5D')
DESTINATION = Path(__file__).resolve().parents[1] / 'maps' / 'global-relief'


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def convert(source, destination):
    import numpy as np
    from scipy.io import netcdf_file

    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    with netcdf_file(source, mmap=False) as dataset:
        latitude = dataset.variables['latitude']
        longitude = dataset.variables['longitude']
        altitude = dataset.variables['altitude']
        if (latitude.units != b'degrees_north' or longitude.units != b'degrees_east' or
                altitude.units != b'm' or altitude.positive != b'up' or
                b'Mean Sea Level' not in dataset.summary):
            raise ValueError('Unexpected coordinate/height reference metadata')
        if (altitude.dimensions != ('latitude', 'longitude') or
                altitude.shape != (3601, 7201) or
                not np.allclose(latitude[:], np.linspace(-90, 90, 3601), rtol=0, atol=1e-7) or
                not np.allclose(longitude[:], np.linspace(-180, 180, 7201), rtol=0, atol=1e-7)):
            raise ValueError('Unexpected global grid order, extent or spacing')
        values = altitude[:]
        seam_delta = values[:, 0].astype(np.int32) - values[:, -1].astype(np.int32)
        seam_count = int(np.count_nonzero(seam_delta))
        seam_max = int(np.max(np.abs(seam_delta)))
        missing = int(np.count_nonzero(values == altitude._FillValue))
        # A global base is expected to eliminate gaps, not silently reproduce them.
        if missing:
            raise ValueError(f'Global source has {missing} missing samples')
        # Both endpoints represent the same meridian. Match runtime wrapping to -180.
        # Record this explicit preprocessing instead of leaving a tiny interpolation seam.
        values[:, -1] = values[:, 0]
        name = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination, delete=False) as output:
                name = output.name
                output.write(struct.pack('<8sIIII4d', b'ELYHGT01', 7201, 3601, 128, 4,
                                         -180., 90., .05, .05))
                north_first = values[::-1]
                for y in range(0, 3601, 128):
                    for x in range(0, 7201, 128):
                        tile = np.full((128, 128), np.nan, dtype='<f4')
                        part = north_first[y:y+128, x:x+128]
                        tile[:part.shape[0], :part.shape[1]] = part
                        output.write(tile.tobytes())
            os.replace(name, destination / 'terrain.elyhgt')
            name = None
        finally:
            if name is not None:
                Path(name).unlink(missing_ok=True)
    manifest = {
        'source': 'NOAA ETOPO1 Ice Surface, sampled every third grid node',
        'source_url': URL, 'source_sha256': sha256(source),
        'geometry_sha256': sha256(destination / 'terrain.elyhgt'),
        'coverage_centres': [-180, -90, 180, 90], 'spacing_degrees': .05,
        'vertical_reference': 'Mean sea level, as declared by ETOPO1; not an EGM conversion',
        'format': 'ELYHGT01', 'columns': 7201, 'rows': 3601, 'missing_samples': missing,
        'longitude_seam': {'policy': '+180 duplicates -180', 'changed_samples': seam_count,
                           'maximum_source_difference_metres': seam_max},
    }
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Saved', destination / 'terrain.elyhgt', flush=True)


def restore_packaged(destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    expected = json.loads((DESTINATION / 'manifest.json').read_text())['geometry_sha256']
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as output:
            temporary = Path(output.name)
            with gzip.open(DESTINATION / 'terrain.elyhgt.gz', 'rb') as source:
                shutil.copyfileobj(source, output)
        if sha256(temporary) != expected:
            raise ValueError('Packaged terrain checksum mismatch')
        os.replace(temporary, destination / 'terrain.elyhgt')
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print('Restored', destination / 'terrain.elyhgt', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--restore-packaged', action='store_true', help='Restore committed gzip terrain offline; standard Python only')
    modes.add_argument('--source-file', type=Path, help='Reuse an already downloaded source NetCDF')
    parser.add_argument('--destination', type=Path, default=DESTINATION)
    args = parser.parse_args()
    if args.restore_packaged:
        restore_packaged(args.destination)
    elif args.source_file:
        convert(args.source_file, args.destination)
    else:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'global.nc'
            print('Downloading NOAA ETOPO1 global relief...', flush=True)
            with urllib.request.urlopen(URL, timeout=240) as response, source.open('wb') as output:
                shutil.copyfileobj(response, output)
            convert(source, args.destination)


if __name__ == '__main__':
    main()
