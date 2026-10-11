"""Host-side tests with a fake Revit transaction API; no claim of Revit integration."""
import importlib
import json
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import recvt_bridge as bridge


class Group:
    committed = 0
    rolled_back = 0
    def __init__(self, *args):
        self.status = 'new'
    def Start(self):
        self.status = 'started'
        return self.status
    def GetStatus(self):
        return self.status
    def Assimilate(self):
        Group.committed += 1
        self.status = 'committed'
        return self.status
    def RollBack(self):
        Group.rolled_back += 1
        self.status = 'rolledback'
        return self.status
    def Dispose(self):
        pass


def atomic(path, value):
    temporary = path + '.tmp'
    Path(temporary).write_text(json.dumps(value), encoding='utf-8')
    os.replace(temporary, path)


class BridgeTest(unittest.TestCase):
    def test_curve_loop_cannot_be_added_to_curve_array(self):
        with self.assertRaisesRegex(ValueError, 'ONE Curve'):
            bridge.check_model_code('a=DB.CurveArray()\nb=DB.CurveLoop()\na.Append(b)', 'ai_gen.py')
        bridge.check_model_code('a=DB.CurveArray()\nb=DB.CurveLoop()\nfor curve in b:\n    a.Append(curve)', 'ai_gen.py')

    def test_nonexistent_stairs_api_rejected(self):
        with self.assertRaisesRegex(ValueError, 'has no StartNewStairs'):
            bridge.check_model_code('scope.StartNewStairs(a.Id, b.Id)', 'ai_gen.py')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = self.temp.name
        status = types.SimpleNamespace(Started='started', Committed='committed', RolledBack='rolledback')
        db = types.SimpleNamespace(TransactionGroup=Group, TransactionStatus=status)
        revit_module = types.ModuleType('Autodesk.Revit')
        revit_module.DB = db
        self.patch = patch.dict(sys.modules, {'Autodesk': types.ModuleType('Autodesk'), 'Autodesk.Revit': revit_module})
        self.patch.start(); self.addCleanup(self.patch.stop)
        doc = types.SimpleNamespace(IsFamilyDocument=False, IsReadOnly=False, IsModifiable=False)
        self.revit = types.SimpleNamespace(ActiveUIDocument=types.SimpleNamespace(Document=doc))
        Group.committed = Group.rolled_back = 0

    def execute(self, code):
        return bridge.execute(code, 'ai_gen.py', 'test-id', self.revit, self.directory)

    def test_wall_location_point_rejected_before_execution(self):
        code = """
def get_walls():
    col = DB.FilteredElementCollector(doc).OfClass(DB.Wall)
    result = []
    for w in col:
        result.append(w)
    return result
def place(walls):
    for w in walls:
        loc = w.Location
        point = loc.Point
walls0 = get_walls()
place(walls0)
"""
        with self.assertRaisesRegex(ValueError, 'LocationCurve'):
            bridge.check_model_code(code, 'remote.py')
        result = self.execute(code)
        self.assertFalse(result['success'])
        self.assertTrue(result['rolled_back'])
        self.assertIn('building.opening_at', result['error'])

    def test_furniture_location_point_is_allowed(self):
        bridge.check_model_code("""
def walls():
    col = DB.FilteredElementCollector(doc).OfClass(DB.Wall)
    for w in col:
        curve = w.Location.Curve
def furniture(items):
    for w in items:
        loc = w.Location
        point = loc.Point
""", 'mixed.py')

    def test_failed_model_rolls_back_and_does_not_export(self):
        with patch.object(bridge, 'export_ifc') as exporter:
            result = self.execute("raise Exception('bad wall')")
        self.assertFalse(result['success'])
        self.assertTrue(result['rolled_back'])
        self.assertEqual(result['stage'], 'model')
        self.assertEqual(Group.rolled_back, 1)
        exporter.assert_not_called()

    def test_ifc_failure_keeps_model_and_still_exports_mesh(self):
        with patch.object(bridge, 'export_ifc', side_effect=RuntimeError('no exporter')), \
             patch.object(bridge, 'export_mesh', return_value=(12, 0)), \
             patch.object(bridge, 'write_context'):
            result = self.execute('value = 123')
        self.assertTrue(result['success'])
        self.assertFalse(result['ifc_success'])
        self.assertTrue(result['mesh_success'])
        self.assertEqual(Group.committed, 1)
        self.assertEqual(Group.rolled_back, 0)
        self.assertTrue(result['warnings'])
        self.assertFalse(os.path.isabs(result['mesh_path']))

    def test_export_only_never_runs_model_code(self):
        with patch.object(bridge, 'export_ifc'), \
             patch.object(bridge, 'export_mesh', return_value=(1, 0)), \
             patch.object(bridge, 'write_context'):
            result = self.execute('# recvt_export_only\nraise Exception("must not run")')
        self.assertTrue(result['success'])
        self.assertEqual(Group.committed, 0)

    def test_duplicate_task_and_handler_restart_do_not_reexecute(self):
        Path(self.directory, 'ai_gen.py').write_text('# recvt_request_id: task-1\npass')
        clock = iter([i * 10.0 for i in range(100)])
        with patch.object(bridge, 'atomic_json', side_effect=atomic), \
             patch.object(bridge, 'write_context'), \
             patch.object(bridge.time, 'time', side_effect=lambda: next(clock)), \
             patch.object(bridge, 'execute', return_value={'request_id': 'task-1', 'success': True}) as execute:
            handler = bridge.make_handler(self.revit, self.directory)
            handler(None, None)
            handler(None, None)
            os.remove(os.path.join(self.directory, 'result.json'))
            bridge.make_handler(self.revit, self.directory)(None, None)
            self.assertEqual(execute.call_count, 1)
            self.assertEqual(bridge.read_json(os.path.join(self.directory, 'result.json'))['request_id'], 'task-1')

    def test_interrupted_task_does_not_replay(self):
        Path(self.directory, 'ai_gen.py').write_text('# recvt_request_id: task-2\npass')
        Path(self.directory, 'tasks').mkdir()
        atomic(os.path.join(self.directory, 'tasks', 'task-2.json'), {'state': 'running'})
        with patch.object(bridge, 'atomic_json', side_effect=atomic), \
             patch.object(bridge, 'write_context'), patch.object(bridge, 'execute') as execute:
            bridge.make_handler(self.revit, self.directory)(None, None)
        execute.assert_not_called()
        self.assertFalse(bridge.read_json(os.path.join(self.directory, 'result.json'))['success'])

    def test_legacy_script_not_executed(self):
        Path(self.directory, 'ai_gen.py').write_text('pass')
        with patch.object(bridge, 'write_context'), patch.object(bridge, 'execute') as execute:
            bridge.make_handler(self.revit, self.directory)(None, None)
        execute.assert_not_called()

    def test_context_converts_clr_ids_without_losing_precision(self):
        class ClrInt64:
            def __init__(self, value):
                self.value = value
            def __str__(self):
                return str(self.value)
        item = types.SimpleNamespace(Id=types.SimpleNamespace(Value=ClrInt64(9007199254740993)),
            Elevation=3.5, FamilyName='门族', Category=types.SimpleNamespace(Name='门'))
        db = sys.modules['Autodesk.Revit'].DB
        db.Level, db.WallType, db.FloorType, db.RoofType, db.FamilySymbol = range(5)
        class Collector:
            def __init__(self, doc):
                pass
            def OfClass(self, cls):
                return [item]
        db.FilteredElementCollector = Collector
        db.Element = types.SimpleNamespace(Name=types.SimpleNamespace(GetValue=lambda element: '标高一'))
        doc = types.SimpleNamespace(Title='测试项目', Application=types.SimpleNamespace(VersionNumber='2025'))
        with patch.object(bridge, 'atomic_json', side_effect=atomic):
            bridge.write_context(doc, self.directory)
        data = bridge.read_json(os.path.join(self.directory, 'revit_context.json'))
        self.assertEqual(data['levels'][0]['id'], '9007199254740993')
        self.assertEqual(data['types'][0]['id'], '9007199254740993')
        self.assertEqual(data['types'][0]['family'], '门族')
        self.assertEqual(data['levels'][0]['elevation'], 3.5)

    def test_context_failure_is_throttled_and_document_change_refreshes(self):
        with patch.object(bridge.time, 'time', return_value=100) as clock, \
             patch.object(bridge, 'write_context', side_effect=ValueError('bad context')) as refresh, \
             patch('builtins.print'):
            handler = bridge.make_handler(self.revit, self.directory)
            handler(None, None)
            clock.return_value = 102
            handler(None, None)
            self.assertEqual(refresh.call_count, 1)
            clock.return_value = 131
            handler(None, None)
            self.assertEqual(refresh.call_count, 2)
            self.revit.ActiveUIDocument.Document = object()
            clock.return_value = 133
            handler(None, None)
            self.assertEqual(refresh.call_count, 3)

    def test_warning_handler_dismisses_warnings_but_rolls_back_errors(self):
        db = sys.modules['Autodesk.Revit'].DB
        db.FailureSeverity = types.SimpleNamespace(Warning='warning')
        db.FailureProcessingResult = types.SimpleNamespace(Continue='continue', ProceedWithRollBack='rollback')
        document = object()
        warning = types.SimpleNamespace(GetSeverity=lambda: 'warning', GetDescriptionText=lambda: 'overlapping walls')
        error = types.SimpleNamespace(GetSeverity=lambda: 'error', GetDescriptionText=lambda: 'invalid floor')
        options = types.SimpleNamespace(SetClearAfterRollback=unittest.mock.Mock())
        accessor = types.SimpleNamespace(GetDocument=lambda: document, GetFailureMessages=lambda: [warning, error],
            DeleteWarning=unittest.mock.Mock(), GetFailureHandlingOptions=lambda: options,
            SetFailureHandlingOptions=unittest.mock.Mock())
        args = types.SimpleNamespace(GetFailuresAccessor=lambda: accessor, SetProcessingResult=unittest.mock.Mock())
        operation = {'document': document, 'warnings': [], 'errors': []}
        bridge.make_failure_handler(operation)(None, args)
        accessor.DeleteWarning.assert_called_once_with(warning)
        options.SetClearAfterRollback.assert_called_once_with(True)
        args.SetProcessingResult.assert_called_once_with('rollback')
        self.assertEqual(operation['errors'], ['invalid floor'])

    def test_confirmation_rules_are_scoped_to_exact_dialog_id(self):
        operation = {'warnings': [], 'errors': []}
        known = types.SimpleNamespace(DialogId='TaskDialog_Really_Print_Or_Export_Temp_View_Modes',
            Message='export', OverrideResult=unittest.mock.Mock(return_value=True))
        unknown = types.SimpleNamespace(DialogId='TaskDialog_DeleteEverything', Message='delete all',
            OverrideResult=unittest.mock.Mock())
        handler = bridge.make_dialog_handler(self.directory, operation)
        with patch('builtins.print'):
            handler(None, known)
            handler(None, unknown)
        known.OverrideResult.assert_called_once_with(1002)
        unknown.OverrideResult.assert_not_called()
        self.assertIn('TaskDialog_DeleteEverything', Path(self.directory, 'dialogs.log').read_text())

    def test_task_unsubscribes_handlers_when_execution_raises(self):
        class Event:
            def __init__(self): self.handlers = []
            def __iadd__(self, handler): self.handlers.append(handler); return self
            def __isub__(self, handler): self.handlers.remove(handler); return self
        failure = Event()
        dialog = Event()
        self.revit.Application = types.SimpleNamespace(FailuresProcessing=failure)
        self.revit.DialogBoxShowing = dialog
        with patch.object(bridge, '_execute', side_effect=ValueError('failed')):
            with self.assertRaises(ValueError):
                self.execute('pass')
        self.assertEqual(failure.handlers, [])
        self.assertEqual(dialog.handlers, [])


if __name__ == '__main__':
    unittest.main()
