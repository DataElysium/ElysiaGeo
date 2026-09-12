import importlib.util
from pathlib import Path
import math
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("height_import", Path(__file__).parents[1] / "tools/import_heightmap.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class HeightImporter(unittest.TestCase):
    def test_package(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "grid.asc", Path(directory) / "grid.elyhgt"
            source.write_text("ncols 3\nnrows 2\nxllcorner 10\nyllcorner 20\ncellsize 1\nNODATA_value -9999\n-10 20 30\n40 -9999 60\n")
            module.convert(source, output, reference="egm2008", tile_size=2)
            data = output.read_bytes()
            self.assertEqual(struct.unpack("<8sIIII4d", data[:56]),
                             (b"ELYHGT01", 3, 2, 2, 3, 10.5, 21.5, 1., 1.))
            values = struct.unpack("<8f", data[56:])
            self.assertEqual(values[:3], (-10., 20., 40.))
            self.assertTrue(math.isnan(values[3]))
            self.assertEqual((values[4], values[6]), (30., 60.))
            self.assertTrue(math.isnan(values[5]) and math.isnan(values[7]))
            module.convert(source, output, reference="egm2008", tile_size=2)
            self.assertEqual(data, output.read_bytes())
            source.write_text(source.read_text() + "99")
            with self.assertRaises(ValueError):
                module.convert(source, output, reference="egm2008", tile_size=2)
            self.assertEqual(data, output.read_bytes())

    def test_invalid(self):
        header = "ncols 2\nnrows 2\nxllcenter 179\nyllcenter 0\ncellsize 1\nNODATA_value -9999\n"
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "grid.asc", Path(directory) / "out"
            for body in ("1 2 3", "1 2 3 inf", "1 2 3 nan"):
                source.write_text(header + body)
                with self.assertRaises(ValueError):
                    module.convert(source, output, reference="egm96")
                self.assertFalse(output.exists())
            source.write_text(header.replace("cellsize 1", "cellsize 0") + "1 2 3 4")
            with self.assertRaises(ValueError):
                module.convert(source, output, reference="egm96")
