#!/usr/bin/env python3
"""Import already dateline-split WGS84 Polygon/MultiPolygon GeoJSON offline."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def convert(source, destination, source_url, scale):
    raw = source.read_bytes()
    document = json.loads(raw)
    crs = document.get("crs")
    if document.get("type") != "FeatureCollection" or (crs is not None and crs != {
        "type": "name", "properties": {"name": "urn:ogc:def:crs:OGC:1.3:CRS84"}
    }):
        raise ValueError("Expected longitude/latitude FeatureCollection (RFC 7946 or CRS84)")
    polygons = []
    for feature in document["features"]:
        geometry = feature["geometry"]
        if geometry["type"] == "Polygon":
            polygons.append(geometry["coordinates"])
        elif geometry["type"] == "MultiPolygon":
            polygons.extend(geometry["coordinates"])
        else:
            raise ValueError("Only land Polygon/MultiPolygon geometry is supported")
    data = bytearray(b"ELYGEO01")
    if not polygons:
        raise ValueError("Empty dataset")
    data += struct.pack("<I", len(polygons))
    for polygon in polygons:
        if not polygon:
            raise ValueError("Empty polygon")
        data += struct.pack("<I", len(polygon))
        for ring in polygon:
            if len(ring) < 4 or ring[0] != ring[-1]:
                raise ValueError("Rings must be closed with at least four vertices")
            data += struct.pack("<I", len(ring))
            for point in ring:
                if len(point) != 2:
                    raise ValueError("Expected longitude/latitude without altitude")
                lon, lat = point
                if not (math.isfinite(lon) and math.isfinite(lat) and -180 <= lon <= 180 and -90 <= lat <= 90):
                    raise ValueError("Coordinate outside WGS84 longitude/latitude range")
                data += struct.pack("<dd", lon, lat)
            # Dateline cutting belongs upstream. The south-pole closure is valid.
            for a, b in zip(ring, ring[1:]):
                if abs(a[0] - b[0]) > 180 and not (abs(a[1]) == 90 and a[1] == b[1]):
                    raise ValueError("Split polygons at the dateline before import")
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "land.elygeo").write_bytes(data)
    manifest = {
        "format": "ELYGEO01", "geometry": "land.elygeo",
        "coordinates": "WGS84 longitude/latitude degrees; dateline-split polygons",
        "coverage": [-180, -90, 180, 90], "scale_denominator": scale,
        "source_url": source_url, "source_sha256": hashlib.sha256(raw).hexdigest(),
        "geometry_sha256": hashlib.sha256(data).hexdigest(), "polygons": len(polygons),
        "semantics": "Generalized continental land polygons; inland water may be included in land",
        "elevation": None, "bathymetry": None,
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--source-url", required=True)
    parser.add_argument("--scale", type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(convert(args.source, args.destination, args.source_url, args.scale), indent=2))
