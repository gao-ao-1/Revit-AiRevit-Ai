"""Helper contract tests using fake geometry; real Revit integration runs remotely."""
import importlib
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))


class Line:
    def __init__(self, a, b):
        self.points = [a, b]
    def GetEndPoint(self, index):
        return self.points[index]
    CreateBound = staticmethod(lambda a, b: Line(a, b))


class LocationCurve:
    def __init__(self, curve):
        self.Curve = curve


class Array(list):
    Append = list.append
    Add = list.append
    @classmethod
    def __class_getitem__(cls, item):
        return cls


class Box:
    def __init__(self, value):
        self.Value = value
    @classmethod
    def __class_getitem__(cls, item):
        return cls


class ModelTest(unittest.TestCase):
    def setUp(self):
        self.db = types.SimpleNamespace(
            WallType=type('WallType', (), {}), FloorType=type('FloorType', (), {}),
            RoofType=type('RoofType', (), {}), Line=Line, LocationCurve=LocationCurve,
            XYZ=lambda x, y, z: types.SimpleNamespace(X=x, Y=y, Z=z),
            CurveArray=Array, ModelCurveArray=Array)
        modules = {'Autodesk.Revit': types.SimpleNamespace(DB=self.db),
                   'System.Collections.Generic': types.SimpleNamespace(List=Array),
                   'System.Runtime.CompilerServices': types.SimpleNamespace(StrongBox=Box)}
        self.patcher = patch.dict(sys.modules, modules)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        sys.modules.pop('recvt_model', None)
        self.model = importlib.import_module('recvt_model')
        self.level = types.SimpleNamespace(Id=1, Elevation=10)

    def wall(self, a, b, level=1):
        xyz = lambda p: self.db.XYZ(p[0] / .3048, p[1] / .3048, 10)
        return types.SimpleNamespace(LevelId=level, Location=LocationCurve(Line(xyz(a), xyz(b))))

    def test_host_lookup_uses_segment_and_metre_units(self):
        south = self.wall((0, 0), (10, 0))
        north = self.wall((0, 8), (10, 8))
        upstairs = self.wall((0, 0), (10, 0), 2)
        self.assertIs(self.model.wall_at([south, north, upstairs], self.level, 9, 0), south)
        self.assertIs(self.model.wall_at([south, north], self.level, 5, 8), north)
        with self.assertRaises(ValueError):
            self.model.wall_at([south], self.level, 11, 0)
        with self.assertRaises(ValueError):
            self.model.wall_at([south, south], self.level, 5, 0)

    def test_floor_rejects_roof_type_before_geometry(self):
        doc = types.SimpleNamespace(GetElement=lambda identity: self.db.RoofType())
        with self.assertRaisesRegex(ValueError, 'building.roof'):
            self.model.floor(doc, self.level, [(0, 0), (10, 0), (0, 8)], 5)

    def test_floor_hole_uses_two_loops_and_ids(self):
        self.db.CurveLoop = Array
        def create(doc, loops, type_id, level_id):
            self.assertEqual((type_id, level_id), (5, 1))
            self.assertEqual([len(loop) for loop in loops], [4, 4])
            self.assertAlmostEqual(loops[1][0].points[0].X, 2 / .3048)
            return 'floor-with-hole'
        self.db.Floor = types.SimpleNamespace(Create=create)
        doc = types.SimpleNamespace(GetElement=lambda identity: self.db.FloorType())
        result = self.model.floor(doc, self.level, [(0,0),(10,0),(10,8),(0,8)], 5,
                                  holes_xy_m=[[(2,2),(4,2),(4,4),(2,4)]])
        self.assertEqual(result, 'floor-with-hole')

    def test_roof_uses_output_mapping_and_disables_all_slopes(self):
        roof_type = self.db.RoofType()
        slopes = []
        roof = types.SimpleNamespace(set_DefinesSlope=lambda curve, value: slopes.append((curve, value)))
        def create(curves, level, kind, mapping):
            self.assertIs(kind, roof_type)
            self.assertEqual(len(curves), 4)
            self.assertAlmostEqual(curves[0].points[1].X, 10 / .3048)
            mapping.Value = ['a', 'b', 'c', 'd']
            return roof
        doc = types.SimpleNamespace(GetElement=lambda identity: roof_type,
                                    Create=types.SimpleNamespace(NewFootPrintRoof=create))
        self.assertIs(self.model.roof(doc, self.level, [(0, 0), (10, 0), (10, 8), (0, 8)], 5), roof)
        self.assertEqual(slopes, [('a', False), ('b', False), ('c', False), ('d', False)])


if __name__ == '__main__':
    unittest.main()
