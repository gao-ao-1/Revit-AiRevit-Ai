# -*- coding: utf-8 -*-
"""Small Revit 2025 building helpers. Length arguments are METRES.
Call inside a DB.Transaction. Returned objects are real Revit elements.
"""
from Autodesk.Revit import DB
from System.Collections.Generic import List


def feet(metres):
    if metres is None:
        raise ValueError('Length/elevation must be a number in metres, not None. '
                         'level(doc, name, elevation_m) creates ONE level; '
                         'query levels with FilteredElementCollector(doc).OfClass(DB.Level).')
    return float(metres) / 0.3048


def level(doc, name, elevation_m):
    elevation = feet(elevation_m)
    for item in DB.FilteredElementCollector(doc).OfClass(DB.Level):
        if abs(item.Elevation - elevation) < 0.001:
            return item
    item = DB.Level.Create(doc, elevation)
    item.Name = name
    return item


def wall(doc, base_level, start_xy_m, end_xy_m, height_m, type_id=None):
    if height_m <= 0:
        raise ValueError('Wall height must be positive')
    if type_id is None:
        type_id = doc.GetDefaultElementTypeId(DB.ElementTypeGroup.WallType)
    if not isinstance(doc.GetElement(type_id), DB.WallType):
        raise ValueError('wall type_id must identify a WallType')
    start = DB.XYZ(feet(start_xy_m[0]), feet(start_xy_m[1]), base_level.Elevation)
    end = DB.XYZ(feet(end_xy_m[0]), feet(end_xy_m[1]), base_level.Elevation)
    return DB.Wall.Create(doc, DB.Line.CreateBound(start, end), type_id,
                          base_level.Id, feet(height_m), 0.0, False, False)


def floor(doc, base_level, outline_xy_m, type_id=None, holes_xy_m=None):
    if len(outline_xy_m) < 3:
        raise ValueError('Floor outline requires at least three points, without repeating the first')
    if type_id is None:
        item = DB.FilteredElementCollector(doc).OfClass(DB.FloorType).FirstElement()
        if item is None:
            raise ValueError('Load a floor type into the project first')
        type_id = item.Id
    if not isinstance(doc.GetElement(type_id), DB.FloorType):
        raise ValueError('floor requires FloorType, not RoofType; use building.roof for a roof')
    loop = DB.CurveLoop()
    points = [DB.XYZ(feet(x), feet(y), base_level.Elevation) for x, y in outline_xy_m]
    for i in range(len(points)):
        loop.Append(DB.Line.CreateBound(points[i], points[(i + 1) % len(points)]))
    loops = List[DB.CurveLoop]()
    loops.Add(loop)
    for hole in holes_xy_m or []:
        if len(hole) < 3 or hole[0] == hole[-1]:
            raise ValueError('Floor hole requires at least three points without repeating the first')
        hole_loop = DB.CurveLoop()
        hole_points = [DB.XYZ(feet(x), feet(y), base_level.Elevation) for x, y in hole]
        for i in range(len(hole_points)):
            hole_loop.Append(DB.Line.CreateBound(hole_points[i], hole_points[(i + 1) % len(hole_points)]))
        loops.Add(hole_loop)
    return DB.Floor.Create(doc, loops, type_id, base_level.Id)


def opening_family(doc, host_wall, base_level, symbol, x_m, y_m, sill_m=0.0):
    """Place a preloaded wall-hosted door/window family at world XY and level+sill."""
    if symbol is None:
        raise ValueError('A loaded door/window FamilySymbol is required')
    if not isinstance(host_wall, DB.Wall):
        raise ValueError('Door/window host must be a Wall')
    if not symbol.IsActive:
        symbol.Activate()
        doc.Regenerate()
    point = DB.XYZ(feet(x_m), feet(y_m), base_level.Elevation + feet(sill_m))
    return doc.Create.NewFamilyInstance(point, symbol, host_wall, base_level,
                                        DB.Structure.StructuralType.NonStructural)


def element_name(element):
    """Avoid IronPython inherited ElementType.Name binding errors."""
    return DB.Element.Name.GetValue(element)


def roof(doc, base_level, outline_xy_m, type_id=None):
    """Create a flat footprint roof; all lengths are metres."""
    from System.Runtime.CompilerServices import StrongBox
    roof_type = (DB.FilteredElementCollector(doc).OfClass(DB.RoofType).FirstElement()
                 if type_id is None else doc.GetElement(type_id))
    if not isinstance(roof_type, DB.RoofType):
        raise ValueError('roof requires a loaded RoofType')
    if len(outline_xy_m) < 3 or outline_xy_m[0] == outline_xy_m[-1]:
        raise ValueError('Roof needs at least three points without repeating the first')
    curves = DB.CurveArray()
    points = [DB.XYZ(feet(x), feet(y), base_level.Elevation) for x, y in outline_xy_m]
    for i in range(len(points)):
        curves.Append(DB.Line.CreateBound(points[i], points[(i + 1) % len(points)]))
    mapping = StrongBox[DB.ModelCurveArray](DB.ModelCurveArray())
    item = doc.Create.NewFootPrintRoof(curves, base_level, roof_type, mapping)
    for model_curve in mapping.Value:
        item.set_DefinesSlope(model_curve, False)
    return item


def wall_at(walls, base_level, x_m, y_m, tolerance_m=0.02):
    """Find an unambiguous straight host wall from explicitly supplied walls.
    Use the returned walls from building.wall, avoiding unrelated project walls.
    """
    matches = []
    for item in walls:
        if item.LevelId != base_level.Id:
            continue
        location = item.Location
        if not isinstance(location, DB.LocationCurve) or not isinstance(location.Curve, DB.Line):
            continue
        a = location.Curve.GetEndPoint(0)
        b = location.Curve.GetEndPoint(1)
        ax, ay, bx, by = a.X * 0.3048, a.Y * 0.3048, b.X * 0.3048, b.Y * 0.3048
        dx, dy = bx - ax, by - ay
        length2 = dx * dx + dy * dy
        if length2 <= 0:
            continue
        t = ((x_m - ax) * dx + (y_m - ay) * dy) / length2
        if t < 0 or t > 1:
            continue
        distance2 = (x_m - ax - t * dx) ** 2 + (y_m - ay - t * dy) ** 2
        if distance2 <= tolerance_m ** 2:
            matches.append(item)
    if len(matches) != 1:
        raise ValueError('Expected ONE host wall at ({}, {}) metres; found {}'.format(x_m, y_m, len(matches)))
    return matches[0]


def opening_at(doc, walls, base_level, symbol, x_m, y_m, sill_m=0.0):
    return opening_family(doc, wall_at(walls, base_level, x_m, y_m),
                          base_level, symbol, x_m, y_m, sill_m)


def stairs_u(doc, base_level, top_level, x_m, y_m, width_m=1.1,
             tread_m=0.28, risers=20, gap_m=0.15):
    """Create real U-shaped stairs. Call AFTER closing the building transaction.
    First run travels +Y, second returns -Y. Inputs are metres.
    Reserve width 2*width+gap and depth (ceil(risers/2)-1)*tread+width.
    The upper floor must have an opening covering both runs and landing.
    """
    import math
    from Autodesk.Revit.DB import Architecture
    if doc.IsModifiable:
        raise ValueError('stairs_u must be called after committing the building transaction')
    if top_level.Elevation <= base_level.Elevation or risers < 4 or width_m <= 0 or tread_m <= 0:
        raise ValueError('Invalid stairs levels, riser count, width or tread depth')

    class Failures(DB.IFailuresPreprocessor):
        def PreprocessFailures(self, accessor):
            for failure in accessor.GetFailureMessages():
                if failure.GetSeverity() == DB.FailureSeverity.Warning:
                    accessor.DeleteWarning(failure)
                else:
                    return DB.FailureProcessingResult.ProceedWithRollBack
            return DB.FailureProcessingResult.Continue

    scope = DB.StairsEditScope(doc, 'Recvt U stairs')
    transaction = None
    try:
        stair_id = scope.Start(base_level.Id, top_level.Id)
        transaction = DB.Transaction(doc, 'Recvt stairs runs and landing')
        transaction.Start()
        stairs = doc.GetElement(stair_id)
        stairs.DesiredRisersNumber = int(risers)
        stairs.ActualTreadDepth = feet(tread_m)
        doc.Regenerate()
        first_count = int(math.ceil(risers / 2.0))
        length = feet((first_count - 1) * tread_m)
        x, y, z = feet(x_m), feet(y_m), base_level.Elevation
        first = Architecture.StairsRun.CreateStraightRun(doc, stair_id,
            DB.Line.CreateBound(DB.XYZ(x, y, z), DB.XYZ(x, y + length, z)),
            Architecture.StairsRunJustification.Center)
        first.ActualRunWidth = feet(width_m)
        doc.Regenerate()
        remaining = int(risers) - first.ActualRisersNumber
        if remaining < 2:
            raise ValueError('First stairs run consumed too many risers')
        second_x = x + feet(width_m + gap_m)
        second_z = z + first.TopElevation
        second = Architecture.StairsRun.CreateStraightRun(doc, stair_id,
            DB.Line.CreateBound(DB.XYZ(second_x, y + length, second_z),
                               DB.XYZ(second_x, y + length - feet((remaining - 1) * tread_m), second_z)),
            Architecture.StairsRunJustification.Center)
        second.ActualRunWidth = feet(width_m)
        Architecture.StairsLanding.CreateAutomaticLanding(doc, first.Id, second.Id)
        doc.Regenerate()
        if stairs.ActualRisersNumber != int(risers):
            raise ValueError('Stairs riser count does not reach the requested top level')
        if transaction.Commit() != DB.TransactionStatus.Committed:
            raise ValueError('Stairs transaction was rolled back')
        scope.Commit(Failures())
        if doc.GetElement(stair_id) is None:
            raise ValueError('Stairs edit scope was rolled back')
        return doc.GetElement(stair_id)
    except Exception:
        if transaction is not None and transaction.GetStatus() == DB.TransactionStatus.Started:
            transaction.RollBack()
        scope.Cancel()
        raise
    finally:
        if transaction is not None:
            transaction.Dispose()
        scope.Dispose()
