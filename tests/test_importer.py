import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("importer", Path(__file__).parents[1] / "tools/import_geojson.py")
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.document = {"type": "FeatureCollection", "features": [{
            "type": "Feature", "geometry": {"type": "Polygon", "coordinates": [
                [[0, 0], [20, 0], [20, 20], [0, 20], [0, 0]],
                [[5, 5], [15, 5], [15, 15], [5, 15], [5, 5]],
            ]}}]}

    def convert(self, document):
        source = self.root / "source.json"
        source.write_text(json.dumps(document))
        return importer.convert(source, self.root / "package", "fixture", 50000000)

    def test_reproducible_and_preserves_holes(self):
        first = self.convert(self.document)
        self.assertEqual(first, self.convert(self.document))
        data = (self.root / "package/land.elygeo").read_bytes()
        self.assertEqual(data[:8], b"ELYGEO01")
        self.assertEqual(int.from_bytes(data[12:16], "little"), 2)

    def test_rejects_wrong_crs(self):
        self.document["crs"] = {"type": "name", "properties": {"name": "EPSG:3857"}}
        with self.assertRaises(ValueError):
            self.convert(self.document)

    def test_rejects_invalid_geometry(self):
        for point in [[0, 91], [float("nan"), 0], [0, 0, 1]]:
            document = copy.deepcopy(self.document)
            document["features"][0]["geometry"]["coordinates"][0][1] = point
            with self.subTest(point=point), self.assertRaises(ValueError):
                self.convert(document)

    def test_rejects_unclosed_ring(self):
        self.document["features"][0]["geometry"]["coordinates"][0].pop()
        with self.assertRaises(ValueError):
            self.convert(self.document)

    def test_rejects_unsplit_dateline(self):
        self.document["features"][0]["geometry"]["coordinates"] = [
            [[179, 0], [-179, 0], [-179, 5], [179, 5], [179, 0]]]
        with self.assertRaises(ValueError):
            self.convert(self.document)

    def test_multipolygon(self):
        geometry = self.document["features"][0]["geometry"]
        geometry["coordinates"] = [geometry["coordinates"], geometry["coordinates"]]
        geometry["type"] = "MultiPolygon"
        self.assertEqual(self.convert(self.document)["polygons"], 2)


if __name__ == "__main__":
    unittest.main()
