#!/usr/bin/env python3
"""Convert a geographic ESRI ASCII grid (metres, positive up) to ELYHGT01.

Reproject other raster formats offline first. The input must use WGS84
longitude/latitude degrees; this format cannot identify or convert its CRS.
"""
import argparse
import math
from pathlib import Path
import struct

REFERENCES = {"unspecified": 0, "wgs84_ellipsoid": 1, "egm96": 2, "egm2008": 3, "mean_sea_level": 4}


def convert(source, destination, *, reference, tile_size=256):
    if reference not in REFERENCES or not 1 <= tile_size <= 4096:
        raise ValueError("Invalid reference or tile size")
    with Path(source).open() as stream:
        header = {}
        for _ in range(6):
            key, value = stream.readline().split()
            key = key.lower()
            if key in header:
                raise ValueError("Duplicate grid header")
            header[key] = float(value)
        cols, rows = header["ncols"], header["nrows"]
        if not (math.isfinite(cols) and math.isfinite(rows) and
                cols == int(cols) and rows == int(rows) and 0 < cols <= 0xffffffff and 0 < rows <= 0xffffffff):
            raise ValueError("Invalid grid dimensions")
        cols, rows = int(cols), int(rows)
        step = header["cellsize"]
        if not math.isfinite(step) or step <= 0:
            raise ValueError("Invalid cell size")
        if "xllcenter" in header and "yllcenter" in header:
            west, south = header["xllcenter"], header["yllcenter"]
        elif "xllcorner" in header and "yllcorner" in header:
            west, south = header["xllcorner"] + step / 2, header["yllcorner"] + step / 2
        else:
            raise ValueError("Expected matching centre or corner origins")
        north = south + (rows - 1) * step
        if not (math.isfinite(west) and math.isfinite(south) and math.isfinite(north) and
                -180 <= west <= 180 and -90 <= south <= north <= 90 and (cols - 1) * step <= 360):
            raise ValueError("Grid is not geographic longitude/latitude")
        nodata = header["nodata_value"]
        nx, ny = (cols + tile_size - 1) // tile_size, (rows + tile_size - 1) // tile_size
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Publish only a completed package; a failed conversion preserves existing output.
        import tempfile
        import os
        name = None
        try:
            with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as out:
                name = out.name
                out.write(struct.pack("<8sIIII4d", b"ELYHGT01", cols, rows, tile_size,
                                      REFERENCES[reference], west, north, step, step))
                tokens = (token for line in stream for token in line.split())
                missing = struct.pack("<f", float("nan"))
                # Buffer one tile-height strip, not the complete source raster.
                for ty in range(ny):
                    strip = []
                    for _ in range(min(tile_size, rows - ty * tile_size)):
                        row = bytearray()
                        for _ in range(cols):
                            token = next(tokens, None)
                            if token is None:
                                raise ValueError("Truncated elevation grid")
                            value = float(token)
                            if value == nodata or (math.isnan(value) and math.isnan(nodata)):
                                row.extend(missing)
                            elif not math.isfinite(value):
                                raise ValueError("Nonfinite elevation")
                            else:
                                row.extend(struct.pack("<f", value))
                        strip.append(row)
                    for tx in range(nx):
                        width = min(tile_size, cols - tx * tile_size)
                        for row in strip:
                            out.write(row[tx * tile_size * 4:(tx * tile_size + width) * 4])
                            out.write(missing * (tile_size - width))
                        out.write(missing * ((tile_size - len(strip)) * tile_size))
                if next(tokens, None) is not None:
                    raise ValueError("Trailing elevation samples")
            os.replace(name, destination)
            name = None
        finally:
            if name is not None:
                Path(name).unlink(missing_ok=True)
    return {"columns": cols, "rows": rows, "tiles": nx * ny, "reference": reference}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--reference", required=True, choices=REFERENCES)
    parser.add_argument("--tile-size", type=int, default=256)
    args = parser.parse_args()
    print(convert(args.source, args.destination, reference=args.reference, tile_size=args.tile_size))
