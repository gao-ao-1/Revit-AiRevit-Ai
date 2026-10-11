# -*- coding: utf-8 -*-
# -*- coding: utf-8 -*-
# ============================================================
# 需求清单（逐条）
# 1. 25层框架结构，层高3.6m，总高90m
# 2. 平面80m(东西) x 30m(南北)；东西向7跨x10m，南北向3跨x10m
# 3. 东西外墙：带砖与金属立筋龙骨复合墙(约350mm)
# 4. 南北外墙：玻璃幕墙(约20mm)
# 5. 柱：610x610mm 现浇混凝土柱
# 6. 楼板：混凝土362mm，每层含首层地面
# 7. 屋顶居中游泳池：50.2m(东西) x 18.2m(南北) x 2.2m深
#    做法：屋面板开洞 + 池壁(墙) + 池底楼板
# 8. 南侧7.2m标高居中挑檐：20.61m(东西) x 14m(南北) x 225mm
#    由两根475x475mm现浇柱支撑
# 9. 场地：120m(东西) x 66m(南北)草地，建筑居中
# 10. 挑檐下方同心圆弧沥青道路，宽10m
#
# 布局方案与假设：
# - 建筑中心置于原点(0,0)，X为东西向，Y为南北向
# - 建筑范围：X: -40..40, Y: -15..15
# - 柱网：X = -40,-30,-20,-10,0,10,20,30,40 (9条轴线)
#         Y = -15,-5,5,15 (4条轴线)
# - 挑檐从南立面(y=-15)向外悬挑，挑檐外缘 y = -29
#   挑檐东西居中：X: -10.305..10.305 (宽20.61m)，标高7.2m，厚225mm
# - 两根475x475支撑柱位于挑檐外缘下方，X=±5.1525, Y=-29
# - 游泳池居中于屋顶：X: -25.1..25.1, Y: -9.1..9.1，池深2.2m
# - 草地：120x66m，中心在原点
# - 圆弧道路：以建筑中心为圆心，中心线半径34m，宽10m
# - 未指定次要尺寸采用合理默认值
#
# 修复要点：
# - 所有 building.level 调用在 DB.Transaction 内
# ============================================================

from pyrevit import revit, DB
from System.Collections.Generic import List
import recvt_model as building
import math

FT = 0.3048

doc = revit.doc

# ---------- 辅助：按名称查找类型 ----------
def find_type_by_name(category_bic, name_key):
    col = DB.FilteredElementCollector(doc).OfCategory(category_bic).WhereElementIsElementType()
    for t in col:
        try:
            n = building.element_name(t)
        except Exception:
            n = None
        if n and name_key in n:
            return t
    return None

# ---------- 查找所需类型 ----------
wall_type_east_west = find_type_by_name(DB.BuiltInCategory.OST_Walls, u"带砖与金属立筋龙骨复合墙")
if wall_type_east_west is None:
    wall_type_east_west = find_type_by_name(DB.BuiltInCategory.OST_Walls, u"带砌块与金属立筋龙骨复合墙")
if wall_type_east_west is None:
    raise Exception(u"缺少东西外墙类型：带砖与金属立筋龙骨复合墙")

wall_type_glass = find_type_by_name(DB.BuiltInCategory.OST_Walls, u"外部玻璃")
if wall_type_glass is None:
    raise Exception(u"缺少南北外墙玻璃幕墙类型：外部玻璃")

floor_type_362 = find_type_by_name(DB.BuiltInCategory.OST_Floors, u"混凝土 - 商用 362mm")
if floor_type_362 is None:
    raise Exception(u"缺少楼板类型：混凝土 - 商用 362mm")

floor_type_225 = find_type_by_name(DB.BuiltInCategory.OST_Floors, u"现场浇注混凝土 225mm")
if floor_type_225 is None:
    floor_type_225 = find_type_by_name(DB.BuiltInCategory.OST_Floors, u"常规 - 300mm")
if floor_type_225 is None:
    raise Exception(u"缺少挑檐楼板类型：现场浇注混凝土 225mm")

col_symbol_610 = None
col_sym_col = DB.FilteredElementCollector(doc).OfCategory(DB.BuiltInCategory.OST_Columns).WhereElementIsElementType()
for s in col_sym_col:
    try:
        n = building.element_name(s)
    except Exception:
        n = None
    if n and u"610" in n:
        col_symbol_610 = s
        break
if col_symbol_610 is None:
    raise Exception(u"缺少柱族类型：610 x 610mm 矩形柱")

col_symbol_475 = None
for s in col_sym_col:
    try:
        n = building.element_name(s)
    except Exception:
        n = None
    if n and u"475" in n:
        col_symbol_475 = s
        break
if col_symbol_475 is None:
    raise Exception(u"缺少柱族类型：475 x 475mm 矩形柱")

roof_type = find_type_by_name(DB.BuiltInCategory.OST_Roofs, u"保温屋顶 - 混凝土")
if roof_type is None:
    roof_type = find_type_by_name(DB.BuiltInCategory.OST_Roofs, u"常规 - 400mm")
if roof_type is None:
    raise Exception(u"缺少屋顶类型")

floor_type_site = find_type_by_name(DB.BuiltInCategory.OST_Floors, u"常规 - 150mm")
if floor_type_site is None:
    floor_type_site = floor_type_225

# ---------- 几何参数 ----------
FLOOR_H = 3.6
N_FLOORS = 25
TOTAL_H = FLOOR_H * N_FLOORS

BX = 80.0
BY = 30.0
HX = BX / 2.0
HY = BY / 2.0

X_AXES = [-40.0, -30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0, 40.0]
Y_AXES = [-15.0, -5.0, 5.0, 15.0]

CANOPY_X = 20.61
CANOPY_Y = 14.0
CANOPY_Z = 7.2
CANOPY_XMIN = -CANOPY_X / 2.0
CANOPY_XMAX = CANOPY_X / 2.0
CANOPY_YMIN = -HY - CANOPY_Y
CANOPY_YMAX = -HY

POOL_X = 50.2
POOL_Y = 18.2
POOL_DEPTH = 2.2
POOL_XMIN = -POOL_X / 2.0
POOL_XMAX = POOL_X / 2.0
POOL_YMIN = -POOL_Y / 2.0
POOL_YMAX = POOL_Y / 2.0

SITE_X = 120.0
SITE_Y = 66.0

ROAD_WIDTH = 10.0
ROAD_R_CENTER = 34.0
ROAD_R_IN = ROAD_R_CENTER - ROAD_WIDTH / 2.0
ROAD_R_OUT = ROAD_R_CENTER + ROAD_WIDTH / 2.0

# ============================================================
# 主体建模事务
# ============================================================
t = DB.Transaction(doc, u"创建25层框架建筑")
t.Start()
try:
    # ---------- 0. 创建标高（事务内） ----------
    levels = []
    for i in range(N_FLOORS + 1):
        lv = building.level(doc, u"标高 {0}".format(i), i * FLOOR_H)
        levels.append(lv)

    roof_level = levels[N_FLOORS]
    canopy_level = building.level(doc, u"挑檐标高 7.2m", CANOPY_Z)
    pool_bottom_level = building.level(doc, u"泳池底标高", TOTAL_H - POOL_DEPTH)
    site_level = building.level(doc, u"场地标高", -0.15)

    # ---------- 1. 柱 ----------
    if not col_symbol_610.IsActive:
        col_symbol_610.Activate()
        doc.Regenerate()

    for i in range(N_FLOORS):
        base_lv = levels[i]
        for x in X_AXES:
            for y in Y_AXES:
                p = DB.XYZ(x / FT, y / FT, 0.0)
                inst = doc.Create.NewFamilyInstance(
                    p, col_symbol_610, base_lv,
                    DB.Structure.StructuralType.Column)
                h_param = inst.get_Parameter(DB.BuiltInParameter.INSTANCE_LENGTH_PARAM)
                if h_param is not None and not h_param.IsReadOnly:
                    h_param.Set(FLOOR_H / FT)

    # ---------- 2. 楼板 ----------
    slab_pts = [(-HX, -HY), (HX, -HY), (HX, HY), (-HX, HY)]
    for i in range(N_FLOORS + 1):
        building.floor(doc, levels[i], slab_pts, floor_type_362.Id)

    # ---------- 3. 东西外墙 ----------
    for i in range(N_FLOORS):
        lv = levels[i]
        building.wall(doc, lv, (-HX, -HY), (-HX, HY), FLOOR_H, wall_type_east_west.Id)
        building.wall(doc, lv, (HX, -HY), (HX, HY), FLOOR_H, wall_type_east_west.Id)

    # ---------- 4. 南北外墙（玻璃幕墙） ----------
    for i in range(N_FLOORS):
        lv = levels[i]
        building.wall(doc, lv, (-HX, -HY), (HX, -HY), FLOOR_H, wall_type_glass.Id)
        building.wall(doc, lv, (-HX, HY), (HX, HY), FLOOR_H, wall_type_glass.Id)

    # ---------- 5. 屋顶（带泳池洞口） ----------
    roof = building.roof(doc, roof_level,
        [(-HX, -HY), (HX, -HY), (HX, HY), (-HX, HY)], roof_type.Id)
    pool_profile = DB.CurveArray()
    pool_points = [(POOL_XMIN, POOL_YMIN), (POOL_XMAX, POOL_YMIN),
                   (POOL_XMAX, POOL_YMAX), (POOL_XMIN, POOL_YMAX)]
    for k in range(4):
        a, b = pool_points[k], pool_points[(k+1) % 4]
        pool_profile.Append(DB.Line.CreateBound(
            DB.XYZ(a[0]/FT, a[1]/FT, roof_level.Elevation),
            DB.XYZ(b[0]/FT, b[1]/FT, roof_level.Elevation)))
    doc.Create.NewOpening(roof, pool_profile, False)

    pool_bottom_pts = [
        (POOL_XMIN, POOL_YMIN), (POOL_XMAX, POOL_YMIN),
        (POOL_XMAX, POOL_YMAX), (POOL_XMIN, POOL_YMAX)
    ]
    building.floor(doc, pool_bottom_level, pool_bottom_pts, floor_type_362.Id)

    building.wall(doc, pool_bottom_level, (POOL_XMIN, POOL_YMIN), (POOL_XMAX, POOL_YMIN), POOL_DEPTH, wall_type_east_west.Id)
    building.wall(doc, pool_bottom_level, (POOL_XMIN, POOL_YMAX), (POOL_XMAX, POOL_YMAX), POOL_DEPTH, wall_type_east_west.Id)
    building.wall(doc, pool_bottom_level, (POOL_XMIN, POOL_YMIN), (POOL_XMIN, POOL_YMAX), POOL_DEPTH, wall_type_east_west.Id)
    building.wall(doc, pool_bottom_level, (POOL_XMAX, POOL_YMIN), (POOL_XMAX, POOL_YMAX), POOL_DEPTH, wall_type_east_west.Id)

    # ---------- 7. 挑檐 ----------
    canopy_pts = [
        (CANOPY_XMIN, CANOPY_YMIN), (CANOPY_XMAX, CANOPY_YMIN),
        (CANOPY_XMAX, CANOPY_YMAX), (CANOPY_XMIN, CANOPY_YMAX)
    ]
    building.floor(doc, canopy_level, canopy_pts, floor_type_225.Id)

    # ---------- 8. 挑檐支撑柱 ----------
    if not col_symbol_475.IsActive:
        col_symbol_475.Activate()
        doc.Regenerate()

    support_x = CANOPY_X / 4.0
    support_y = CANOPY_YMIN
    for sx in (-support_x, support_x):
        p = DB.XYZ(sx / FT, support_y / FT, 0.0)
        inst = doc.Create.NewFamilyInstance(
            p, col_symbol_475, site_level,
            DB.Structure.StructuralType.Column)
        h_param = inst.get_Parameter(DB.BuiltInParameter.INSTANCE_LENGTH_PARAM)
        if h_param is not None and not h_param.IsReadOnly:
            h_param.Set(CANOPY_Z / FT)

    # ---------- 9. 草地场地 ----------
    site_pts = [
        (-SITE_X / 2.0, -SITE_Y / 2.0), (SITE_X / 2.0, -SITE_Y / 2.0),
        (SITE_X / 2.0, SITE_Y / 2.0), (-SITE_X / 2.0, SITE_Y / 2.0)
    ]
    building.floor(doc, site_level, site_pts, floor_type_site.Id)

    # ---------- 10. 同心圆弧沥青道路 ----------
    road_loops = List[DB.CurveLoop]()
    n_seg = 48

    outer_circle = DB.CurveLoop()
    for k in range(n_seg):
        a1 = 2.0 * math.pi * k / n_seg
        a2 = 2.0 * math.pi * (k + 1) / n_seg
        p1 = DB.XYZ(ROAD_R_OUT * math.cos(a1) / FT, ROAD_R_OUT * math.sin(a1) / FT, 0.0)
        p2 = DB.XYZ(ROAD_R_OUT * math.cos(a2) / FT, ROAD_R_OUT * math.sin(a2) / FT, 0.0)
        outer_circle.Append(DB.Line.CreateBound(p1, p2))
    road_loops.Add(outer_circle)

    inner_circle = DB.CurveLoop()
    for k in range(n_seg):
        a1 = 2.0 * math.pi * k / n_seg
        a2 = 2.0 * math.pi * (k + 1) / n_seg
        p1 = DB.XYZ(ROAD_R_IN * math.cos(a1) / FT, ROAD_R_IN * math.sin(a1) / FT, 0.0)
        p2 = DB.XYZ(ROAD_R_IN * math.cos(a2) / FT, ROAD_R_IN * math.sin(a2) / FT, 0.0)
        inner_circle.Append(DB.Line.CreateBound(p1, p2))
    road_loops.Add(inner_circle)

    DB.Floor.Create(doc, road_loops, floor_type_site.Id, site_level.Id)

    t.Commit()
except Exception:
    t.RollBack()
    raise
finally:
    t.Dispose()

# ============================================================
# 楼梯（主体事务提交后调用）
# ============================================================
building.stairs_u(doc, levels[0], roof_level, 0.0, 0.0,
                  width_m=1.1, tread_m=0.28, risers=20, gap_m=0.15)
