"""Convert exact STEP geometry to IGES using Open CASCADE; never uses OBJ meshes."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile


def convert(source, destination):
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IGESControl import IGESControl_Writer, IGESControl_Reader
    from OCP.IFSelect import IFSelect_RetDone
    from OCP.BRepCheck import BRepCheck_Analyzer
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID

    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination:
        raise ValueError('Input and output paths must differ')
    if not source.is_file():
        raise ValueError('STEP source does not exist')
    if not destination.parent.is_dir():
        raise ValueError('Output directory does not exist')

    def count(shape, kind):
        explorer = TopExp_Explorer(shape, kind)
        total = 0
        while explorer.More():
            total += 1
            explorer.Next()
        return total

    # ASCII filenames work with older OCCT Windows builds even for Chinese user paths.
    with tempfile.TemporaryDirectory(prefix='recvt-cad-') as temporary:
        step = Path(temporary) / 'source.step'
        iges = Path(temporary) / 'model.igs'
        shutil.copyfile(source, step)
        reader = STEPControl_Reader()
        if reader.ReadFile(str(step)) != IFSelect_RetDone or not reader.TransferRoots():
            raise ValueError('Open CASCADE could not read STEP geometry')
        shape = reader.OneShape()
        faces = count(shape, TopAbs_FACE)
        if shape.IsNull() or not faces:
            raise ValueError('STEP contains no usable faces')
        if not BRepCheck_Analyzer(shape).IsValid():
            raise ValueError('STEP geometry is invalid; repair the source model before exporting IGES')
        # IGES BRep mode preserves available topology instead of tessellating.
        writer = IGESControl_Writer('MM', 1)
        if not writer.AddShape(shape):
            raise ValueError('IGES writer rejected the geometry')
        writer.ComputeModel()
        if not writer.Write(str(iges)) or not iges.is_file() or not iges.stat().st_size:
            raise ValueError('IGES writer produced no file')
        verification = IGESControl_Reader()
        if verification.ReadFile(str(iges)) != IFSelect_RetDone or not verification.TransferRoots():
            raise ValueError('Generated IGES could not be read back')
        verified = verification.OneShape()
        verified_faces = count(verified, TopAbs_FACE)
        if verified.IsNull() or verified_faces < faces or not BRepCheck_Analyzer(verified).IsValid():
            raise ValueError('IGES verification failed or faces were lost; use the original STEP file')
        # Publish only validated output; a failed conversion never overwrites an existing file.
        fd, pending = tempfile.mkstemp(prefix='.recvt-', suffix='.igs', dir=destination.parent)
        os.close(fd)
        try:
            shutil.copyfile(iges, pending)
            os.replace(pending, destination)
        finally:
            if os.path.exists(pending):
                os.unlink(pending)
        return {'faces': verified_faces, 'input_solids': count(shape, TopAbs_SOLID),
                'output_solids': count(verified, TopAbs_SOLID), 'unit': 'mm', 'output': str(destination)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source')
    parser.add_argument('destination')
    args = parser.parse_args()
    try:
        print(json.dumps(convert(args.source, args.destination), ensure_ascii=True))
    except Exception as error:
        print(json.dumps({'error': str(error)}, ensure_ascii=True))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
