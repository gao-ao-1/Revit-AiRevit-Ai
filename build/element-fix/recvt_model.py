# -*- coding: utf-8 -*-
# 提供生成脚本可调用的 Revit 建模辅助函数，长度输入统一为米，内部换算为英尺。
# 标高、墙、楼板、门窗和屋顶由调用者开启事务；stairs_u 自管编辑范围，须在主体事务关闭后调用。
# 返回真实 Revit 元素，便于后续门窗宿主查找及跨构件关联。

"""Small Revit 2025 building helpers. Length arguments are METRES.
Call inside a DB.Transaction. Returned objects are real Revit elements.
"""
from Autodesk.Revit import DB
from System.Collections.Generic import List


# 将米转换为 Revit 内部英尺；None 明确报错，以便定位误把查询标高写成创建标高的调用。
def feet(metres):
    if metres is None:
        raise ValueError('Length/elevation must be a number in metres, not None. '
                         'level(doc, name, elevation_m) creates ONE level; '
                         'query levels with FilteredElementCollector(doc).OfClass(DB.Level).')
    return float(metres) / 0.3048


# 按标高数值查找已有 Level（容差 0.001 英尺），命中直接复用；否则创建并设置名称。
def level(doc, name, elevation_m):
    elevation = feet(elevation_m)
    for item in DB.FilteredElementCollector(doc).OfClass(DB.Level):
        if abs(item.Elevation - elevation) < 0.001:
            return item
    item = DB.Level.Create(doc, elevation)
    item.Name = name
    return item


# 校验正高度及 WallType，将平面端点与高度从米换算后在指定基准标高创建直墙。
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


# 把外轮廓和可选洞口分别构造为闭合 CurveLoop，再以类型 ID 与标高 ID 调用 Floor.Create。
# 输入顶点不重复首点；洞口拓扑合法性由 Revit API 进一步检查。
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


# 在已存在的宿主墙上放置门窗族，坐标为米，Z 由标高加窗台高度得到。
# 未激活的族类型先 Activate 并 Regenerate，再调用带宿主与标高的放置重载。
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


# 通过 DB.Element.Name 属性访问器读取名称，绕开 IronPython 继承属性绑定问题。
def element_name(element):
    """Avoid IronPython inherited ElementType.Name binding errors."""
    return DB.Element.Name.GetValue(element)


# 创建平面轮廓屋顶，使用 StrongBox 接收 API 返回的边界 ModelCurveArray。
# 逐条关闭边界定义坡度，确保生成平屋顶；类型必须是 RoofType。
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


# 仅在调用者提供的同楼层直墙中查找宿主，把墙端点换成米后计算点在线段上的投影距离。
# 必须恰好匹配一面墙；零匹配或多匹配均抛错，防止悄悄选择错误宿主。
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
        # 投影参数 t 必须在 [0,1] 内，排除位于墙延长线而不在实际墙段上的门窗位置。
        t = ((x_m - ax) * dx + (y_m - ay) * dy) / length2
        if t < 0 or t > 1:
            continue
        distance2 = (x_m - ax - t * dx) ** 2 + (y_m - ay - t * dy) ** 2
        if distance2 <= tolerance_m ** 2:
            matches.append(item)
    if len(matches) != 1:
        raise ValueError('Expected ONE host wall at ({}, {}) metres; found {}'.format(x_m, y_m, len(matches)))
    return matches[0]


# 先按坐标找到唯一宿主墙，再调用 opening_family 放置门窗，使定位与放置规则一致。
def opening_at(doc, walls, base_level, symbol, x_m, y_m, sill_m=0.0):
    return opening_family(doc, wall_at(walls, base_level, x_m, y_m),
                          base_level, symbol, x_m, y_m, sill_m)


# 在主体事务关闭后创建 U 形真实楼梯，内部管理 StairsEditScope 和梯段/平台事务。
# 第一跑沿 +Y、第二跑折返；检查实际踢面数量和提交状态，失败回滚并取消编辑范围。
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
        # 楼梯提交时删除警告，遇到非警告失败立即请求回滚，防止留下未完成的楼梯。
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
        # 先分配第一跑的预期踢面数；创建后再根据 Revit 的实际数量计算第二跑，避免累计偏差。
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
        # 梯段事务成功后再提交整个楼梯编辑范围，失败预处理器会阻止真正错误被当成成功。
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
