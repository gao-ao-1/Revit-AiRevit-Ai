"""CAD export routing tests. Revit operations are mocked; no live project is modified."""
import tempfile
import unittest
import types
from pathlib import Path
from unittest.mock import patch
from test_bridge import BridgeTest, Group
import recvt_bridge as bridge


class CadRoutingTest(BridgeTest):
    def test_native_export_uses_mm_ap214_and_rolls_back_temporary_view(self):
        from Autodesk.Revit import DB
        class Transaction(Group):
            active=False
            def Start(self):
                Transaction.active=True
                return super().Start()
            def Commit(self):
                Transaction.active=False;self.status='committed';return self.status
        class Options:
            disposed=False
            def Dispose(self):Options.disposed=True
        class Collector(list):
            def __init__(self, doc):super().__init__([types.SimpleNamespace(ViewFamily='3d',Id=1)])
            def OfClass(self, kind):return self
        view=types.SimpleNamespace(Id=123)
        DB.Transaction=Transaction;DB.STEPExportOptions=Options;DB.FilteredElementCollector=Collector
        DB.ViewFamilyType=object;DB.ViewFamily=types.SimpleNamespace(ThreeDimensional='3d')
        DB.View3D=types.SimpleNamespace(CreateIsometric=lambda doc, id:view)
        DB.ElementId=types.SimpleNamespace(InvalidElementId=-1)
        DB.ExportUnit=types.SimpleNamespace(Millimeter='mm')
        DB.STEPApplicationProtocol=types.SimpleNamespace(ApplicationProtocol214='AP214')
        doc=self.revit.ActiveUIDocument.Document;doc.Regenerate=lambda:None
        def export(directory,name,options):
            self.assertFalse(Transaction.active)
            self.assertEqual((options.ViewId,options.TargetUnit,options.ApplicationProtocol),(123,'mm','AP214'))
            Path(directory,'model.step').write_text('STEP fixture')
            return True
        doc.Export=export
        self.assertEqual(bridge.export_step(doc,self.directory),'model.step')
        self.assertEqual(Group.rolled_back,1)
        self.assertTrue(Options.disposed)
        self.assertFalse(view.IsSectionBoxActive)
        doc.Export=lambda *args:False
        with self.assertRaisesRegex(ValueError,'returned failure'):bridge.export_step(doc,self.directory)
        self.assertEqual(Group.rolled_back,2)

    def test_cad_export_does_not_execute_code_or_export_mesh(self):
        with patch.object(bridge, 'export_step', return_value='model.step') as exporter, \
             patch.object(bridge, 'export_ifc') as ifc, patch.object(bridge, 'export_mesh') as mesh:
            result = self.execute('# recvt_cad_export: step\nraise Exception("must not execute")')
        self.assertTrue(result['success'])
        self.assertTrue(result['cad_success'])
        self.assertEqual(result['stage'], 'export')
        self.assertEqual(result['cad_path'], 'output/test-id/model.step')
        self.assertEqual(Group.committed, 0)
        exporter.assert_called_once()
        ifc.assert_not_called(); mesh.assert_not_called()

    def test_cad_failure_cannot_trigger_model_retry(self):
        with patch.object(bridge, 'export_step', side_effect=ValueError('export failure')):
            result = self.execute('# recvt_cad_export: step\n')
        self.assertFalse(result['success'])
        self.assertFalse(result['rolled_back'])
        self.assertEqual(result['stage'], 'export')
        self.assertTrue(result['model_success'])
        self.assertEqual(Group.committed, 0)


class ConverterTest(unittest.TestCase):
    def test_exact_box_step_iges_round_trip(self):
        try:
            from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox
            from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
            from OCP.IFSelect import IFSelect_RetDone
            from OCP.IGESControl import IGESControl_Reader
            from OCP.BRepBndLib import BRepBndLib
            from OCP.Bnd import Bnd_Box
        except ImportError:
            self.skipTest('Run with .cad-venv/Scripts/python.exe to test the CAD kernel')
        from cad_converter import convert
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'model.step'; destination=Path(directory)/'中文模型.igs'
            shape=BRepPrimAPI_MakeBox(100,200,300).Shape()
            writer=STEPControl_Writer()
            self.assertEqual(writer.Transfer(shape,STEPControl_AsIs),IFSelect_RetDone)
            self.assertEqual(writer.Write(str(source)),IFSelect_RetDone)
            result=convert(source,destination)
            self.assertEqual(result['faces'],6)
            self.assertEqual(result['input_solids'],1)
            self.assertEqual(result['output_solids'],1)
            # Use an ASCII filename to support older OCCT readers on Windows.
            readback=Path(directory)/'check.igs';readback.write_bytes(destination.read_bytes())
            reader=IGESControl_Reader();self.assertEqual(reader.ReadFile(str(readback)),IFSelect_RetDone)
            reader.TransferRoots(); bounds=Bnd_Box(); BRepBndLib.Add_s(reader.OneShape(),bounds)
            xmin,ymin,zmin,xmax,ymax,zmax=bounds.Get()
            for actual,expected in zip((xmax-xmin,ymax-ymin,zmax-zmin),(100,200,300)):
                self.assertAlmostEqual(actual,expected,places=3)
            before=destination.read_bytes();source.write_text('invalid STEP')
            with self.assertRaises(ValueError):convert(source,destination)
            self.assertEqual(destination.read_bytes(),before)


if __name__=='__main__':unittest.main()
