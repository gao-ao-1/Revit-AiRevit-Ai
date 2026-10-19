# 模型导出单元测试：模拟构件、参数、材质和嵌套几何实例，检查 OBJ 与属性快照一致性。
# 覆盖镜像绕序、空项目、部分导出失败和资源释放等容易导致预览错误的情况。

"""Exercise the real OBJ/metadata exporter with a small, simulated Revit API."""
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

import recvt_bridge as bridge


# 构造只包含 Value 的 ID 替身，用于验证 ID 字符串化与参数引用。
def identity(value):
    return types.SimpleNamespace(Value=value)


class Transform:
    def __init__(self, offset=0, reflected=False):
        self.offset, self.HasReflection = offset, reflected
    def OfPoint(self, point):
        return types.SimpleNamespace(X=(-point.X if self.HasReflection else point.X),
                                     Y=point.Y + self.offset, Z=point.Z)
    def Multiply(self, other):
        return Transform(self.offset + other.offset, self.HasReflection != other.HasReflection)


class Mesh:
    NumTriangles = 1
    MaterialElementId = identity(50)
    def get_Triangle(self, index):
        points = [(0, 0, 0), (2, 0, 0), (0, 0, 3)]
        return types.SimpleNamespace(get_Vertex=lambda i: types.SimpleNamespace(
            X=points[i][0], Y=points[i][1], Z=points[i][2]))


class Instance:
    Transform = Transform(2, True)
    def GetSymbolGeometry(self):
        return [Mesh()]


class Material:
    Id = identity(50)
    Name = '玻璃'
    Color = types.SimpleNamespace(Red=120, Green=190, Blue=220)
    Transparency = 60


class Level:
    Id = identity(7)
    Name = '首层'


class Parameter:
    HasValue = True
    def __init__(self, name, storage, value, formatted=None):
        self.Id = identity(88)
        self.Definition = types.SimpleNamespace(Name=name)
        self.StorageType, self.value, self.formatted = storage, value, formatted
    def AsString(self): return self.value
    def AsElementId(self): return identity(self.value)
    def AsValueString(self): return self.formatted
    def AsInteger(self): return self.value


class Element:
    def __init__(self, number, geometry, doc):
        self.Id, self.UniqueId = identity(number), 'uid-' + str(number)
        self.Name, self.Document = '测试墙', doc
        self.LevelId = identity(7)
        self.Category = types.SimpleNamespace(Name='墙', Id=identity(-2000011), Material=None)
        self.Parameters = [Parameter('高度', 'double', 9.8425, '3000 mm'),
                           Parameter('备注', 'string', '外墙'), Parameter('标高', 'element', 7)]
        self.geometry = geometry
    def GetTypeId(self): return identity(10)
    def get_Geometry(self, options): return self.geometry


class ModelExportTest(unittest.TestCase):
    # 为每个测试重新创建依赖替身和隔离数据，保证用例顺序不影响结果。
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = str(Path(self.temp.name, 'model.obj'))
        self.disposed = False
        owner = self
        class Options:
            def Dispose(self): owner.disposed = True
        class Collector:
            def __init__(self, doc): self.doc = doc
            def WhereElementIsNotElementType(self): return self.doc.elements
        db = types.SimpleNamespace(Options=Options, DetailLevel=None,
            ViewDetailLevel=types.SimpleNamespace(Fine=1), Mesh=Mesh, Solid=type('Solid', (), {}),
            GeometryInstance=Instance, Transform=types.SimpleNamespace(Identity=Transform()),
            Material=Material, Level=Level, FilteredElementCollector=Collector,
            StorageType=types.SimpleNamespace(String='string', ElementId='element', Double='double', Integer='integer'),
            Element=types.SimpleNamespace(Name=types.SimpleNamespace(GetValue=lambda element: element.Name)))
        revit = types.ModuleType('Autodesk.Revit')
        revit.DB = db
        modules = patch.dict(sys.modules, {'Autodesk': types.ModuleType('Autodesk'), 'Autodesk.Revit': revit})
        modules.start(); self.addCleanup(modules.stop)
        self.doc = types.SimpleNamespace(Title='构件测试项目')
        element_type = types.SimpleNamespace(Name='200mm 外墙', FamilyName='基本墙',
            Parameters=[Parameter('宽度', 'double', 0.65, '200 mm')])
        self.doc.GetElement = lambda key: {7: Level(), 10: element_type, 50: Material()}.get(key.Value)
        self.doc.elements = [Element(9007199254740993, [Mesh()], self.doc), Element(202, [Instance()], self.doc)]
        atomic = patch.object(bridge, 'atomic_json', side_effect=lambda path, data: Path(path).write_text(json.dumps(data), encoding='utf-8'))
        atomic.start(); self.addCleanup(atomic.stop)

    # 读取当前测试输出的 elements.json，供断言构件属性与网格统计。
    def manifest(self):
        return json.loads(Path(self.temp.name, 'elements.json').read_text(encoding='utf-8'))

    # 验证构件分组、属性、材质、嵌套变换和镜像三角形绕序。
    def test_groups_properties_materials_and_nested_mirror(self):
        self.assertEqual(bridge.export_mesh(self.doc, self.path), (2, 0))
        obj = Path(self.path).read_text(encoding='utf-8')
        self.assertIn('o element_9007199254740993\n', obj)
        self.assertIn('o element_202\n', obj)
        self.assertIn('usemtl material_50\n', obj)
        self.assertIn('v -2 2 0\n', obj)
        self.assertIn('f 4 6 5\n', obj)
        data = self.manifest()
        self.assertEqual(data['scope'], 'project')
        self.assertEqual(data['elements'][0]['id'], '9007199254740993')
        self.assertEqual(data['elements'][0]['level'], '首层')
        self.assertEqual(data['types']['10']['name'], '200mm 外墙')
        self.assertEqual(data['materials']['material_50']['color'], [120, 190, 220])
        self.assertAlmostEqual(data['materials']['material_50']['opacity'], 0.4)
        values = {p['name']: p['value'] for p in data['elements'][0]['parameters']}
        self.assertEqual(values['高度'], '3000 mm')
        self.assertEqual(values['标高'], '首层 [7]')
        self.assertTrue(self.disposed)

    # 验证空项目导出有效空快照，便于客户端清除旧预览。
    def test_empty_project_is_a_valid_snapshot(self):
        self.doc.elements = []
        self.assertEqual(bridge.export_mesh(self.doc, self.path), (0, 0))
        self.assertEqual(self.manifest()['elements'], [])

    # 验证输出文件创建失败时仍释放 Revit 几何选项。
    def test_output_failure_disposes_revit_options(self):
        with self.assertRaises(OSError):
            bridge.export_mesh(self.doc, str(Path(self.temp.name, 'missing', 'model.obj')))
        self.assertTrue(self.disposed)

    # 验证属性读取失败保留可用几何，并附上属性警告。
    def test_metadata_failure_preserves_geometry_and_reports_warning(self):
        with patch.object(bridge, 'element_metadata', side_effect=RuntimeError('parameter failure')):
            self.assertEqual(bridge.export_mesh(self.doc, self.path), (2, 0))
        self.assertIn('metadata_warning', self.manifest()['elements'][0])

    # 验证部分几何失败时已输出面的索引和统计仍一致。
    def test_partial_geometry_failure_has_consistent_face_indices(self):
        class BrokenTriangleMesh(Mesh):
            def get_Triangle(self, index):
                def point(i):
                    if i == 2: raise ValueError('bad vertex')
                    return types.SimpleNamespace(X=100, Y=100, Z=100)
                return types.SimpleNamespace(get_Vertex=point)
        self.doc.elements[0].geometry = [Mesh(), BrokenTriangleMesh()]
        self.assertEqual(bridge.export_mesh(self.doc, self.path), (2, 1))
        text = Path(self.path).read_text(encoding='utf-8')
        self.assertNotIn('100 100 100', text)
        self.assertIn('f 4 6 5', text)
        self.assertIn('geometry_warning', self.manifest()['elements'][0])

    # 验证全部几何读取失败不能伪装为合法空项目。
    def test_total_geometry_failure_is_not_an_empty_success(self):
        self.doc.elements[0].get_Geometry = lambda options: (_ for _ in ()).throw(RuntimeError('bad geometry'))
        self.doc.elements = self.doc.elements[:1]
        with self.assertRaisesRegex(Exception, 'No triangles exported'):
            bridge.export_mesh(self.doc, self.path)
        self.assertFalse(Path(self.temp.name, 'elements.json').exists())


if __name__ == '__main__':
    unittest.main()
