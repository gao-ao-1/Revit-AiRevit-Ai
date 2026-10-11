# -*- coding: utf-8 -*-
# -*- coding: utf-8 -*-
# ============================================================
# 需求清单（用户授权自行确定尺寸与布局）：
# 1. 两层农村住宅，共 2 层，层高 3.0m，总高 6.0m
# 2. 占地轮廓 12.0m(X) x 9.0m(Y)，外墙厚 200mm，内墙 140mm
# 3. 一层布局：客厅(西南)、餐厅+厨房(东南)、卫生间(北中)、楼梯间(北东)
# 4. 二层布局：主卧(西南)、次卧(东南)、次卧2(北西)、卫生间(北中)、起居厅(中)、楼梯间(北东)
# 5. 楼板：二层楼板 + 屋顶平屋面
# 6. 门窗：一层入户门、厨房后门、各房间窗；二层各卧室窗、卫生间窗
# 7. 楼梯：一层到二层，双跑楼梯，位于楼梯间
# 8. 假设：标高 F1=0.0m，F2=3.0m，屋顶=6.0m
#    墙高 3.0m；门高 2.1m；窗台 0.9m，窗高 1.5m
# 修复要点：
#   - 所有建模操作（含 building.level 创建标高）在事务内执行
#   - 二层次卧门定位修正到主分隔墙 X=7.0
# ============================================================
from pyrevit import revit, DB
import recvt_model as building
from System.Collections.Generic import List

doc = revit.doc

# ---------- 单位换算 ----------
FT = 0.3048

def m2ft(v):
    return v / FT

# ---------- 类型查询 ----------
def find_type_by_name(category_bic, name_key):
    col = DB.FilteredElementCollector(doc).OfCategory(category_bic).WhereElementIsElementType()
    for t in col:
        try:
            nm = building.element_name(t)
        except Exception:
            nm = None
        if nm and name_key in nm:
            return t
    return None

# 墙类型
wall_ext_type = find_type_by_name(DB.BuiltInCategory.OST_Walls, "常规 - 200mm")
wall_int_type = find_type_by_name(DB.BuiltInCategory.OST_Walls, "常规 - 140mm 砌体")
if wall_ext_type is None:
    raise Exception("缺少外墙类型：常规 - 200mm")
if wall_int_type is None:
    raise Exception("缺少内墙类型：常规 - 140mm 砌体")

# 楼板类型
floor_type = find_type_by_name(DB.BuiltInCategory.OST_Floors, "常规 - 150mm")
if floor_type is None:
    raise Exception("缺少楼板类型：常规 - 150mm")

# 屋顶类型
roof_type = find_type_by_name(DB.BuiltInCategory.OST_Roofs, "常规 - 125mm")
if roof_type is None:
    raise Exception("缺少屋顶类型：常规 - 125mm")

# 门类型（单扇 - 与墙齐 0915 x 2134mm）
door_type = find_type_by_name(DB.BuiltInCategory.OST_Doors, "0915 x 2134mm")
if door_type is None:
    raise Exception("缺少门类型：单扇 - 与墙齐 0915 x 2134mm")

# 窗类型（固定 0915 x 1220mm）
win_type = find_type_by_name(DB.BuiltInCategory.OST_Windows, "0915 x 1220mm")
if win_type is None:
    raise Exception("缺少窗类型：固定 0915 x 1220mm")

# ---------- 几何参数（米） ----------
W = 12.0   # X 方向总宽
D = 9.0    # Y 方向总深
H = 3.0    # 层高
T_EXT = 0.20
T_INT = 0.14

# 外墙中心线轮廓（矩形）
hx = T_EXT / 2.0
hy = T_EXT / 2.0
x0 = hx
y0 = hy
x1 = W - hx
y1 = D - hy

# 内墙位置
X_MID = 7.0   # 南北向主分隔
Y_MID = 5.5   # 东西向分隔
X_WC = 4.5    # 卫生间西墙

# ---------- 事务（所有建模操作都在事务内） ----------
t = DB.Transaction(doc, "创建两层农村住宅")
t.Start()
try:
    # 创建标高（必须在事务内）
    level1 = building.level(doc, "F1", 0.0)
    level2 = building.level(doc, "F2", 3.0)
    level3 = building.level(doc, "ROOF", 6.0)

    walls = []

    # ============ 一层墙体 ============
    walls.append(building.wall(doc, level1, (x0, y0), (x1, y0), H, wall_ext_type.Id))
    walls.append(building.wall(doc, level1, (x1, y0), (x1, y1), H, wall_ext_type.Id))
    walls.append(building.wall(doc, level1, (x1, y1), (x0, y1), H, wall_ext_type.Id))
    walls.append(building.wall(doc, level1, (x0, y1), (x0, y0), H, wall_ext_type.Id))

    # 一层内墙
    walls.append(building.wall(doc, level1, (X_MID, y0), (X_MID, y1), H, wall_int_type.Id))
    walls.append(building.wall(doc, level1, (x0, Y_MID), (X_MID, Y_MID), H, wall_int_type.Id))
    walls.append(building.wall(doc, level1, (X_WC, Y_MID), (X_WC, y1), H, wall_int_type.Id))

    # ============ 二层墙体 ============
    walls2 = []
    walls2.append(building.wall(doc, level2, (x0, y0), (x1, y0), H, wall_ext_type.Id))
    walls2.append(building.wall(doc, level2, (x1, y0), (x1, y1), H, wall_ext_type.Id))
    walls2.append(building.wall(doc, level2, (x1, y1), (x0, y1), H, wall_ext_type.Id))
    walls2.append(building.wall(doc, level2, (x0, y1), (x0, y0), H, wall_ext_type.Id))

    # 二层内墙
    walls2.append(building.wall(doc, level2, (X_MID, y0), (X_MID, y1), H, wall_int_type.Id))
    walls2.append(building.wall(doc, level2, (x0, Y_MID), (X_MID, Y_MID), H, wall_int_type.Id))
    walls2.append(building.wall(doc, level2, (X_WC, Y_MID), (X_WC, y1), H, wall_int_type.Id))
    walls2.append(building.wall(doc, level2, (4.0, y0), (4.0, Y_MID), H, wall_int_type.Id))

    # ============ 二层楼板 ============
    slab_pts = [(0.0, 0.0), (W, 0.0), (W, D), (0.0, D)]
    upper_floor = building.floor(doc, level2, slab_pts, floor_type.Id)
    opening_points = [(7.4, 5.6), (9.85, 5.6), (9.85, 8.85), (7.4, 8.85)]
    opening_curves = DB.CurveArray()
    for i in range(4):
        a, b = opening_points[i], opening_points[(i+1) % 4]
        opening_curves.Append(DB.Line.CreateBound(
            DB.XYZ(m2ft(a[0]), m2ft(a[1]), level2.Elevation),
            DB.XYZ(m2ft(b[0]), m2ft(b[1]), level2.Elevation)))
    doc.Create.NewOpening(upper_floor, opening_curves, False)

    # ============ 屋顶 ============
    roof_pts = [(0.0, 0.0), (W, 0.0), (W, D), (0.0, D)]
    building.roof(doc, level3, roof_pts, roof_type.Id)

    # ============ 门窗 ============
    if not door_type.IsActive:
        door_type.Activate()
    if not win_type.IsActive:
        win_type.Activate()
    doc.Regenerate()

    # --- 一层门 ---
    building.opening_at(doc, walls, level1, door_type, 3.0, y0, 0.0)
    building.opening_at(doc, walls, level1, door_type, x1, 2.5, 0.0)
    building.opening_at(doc, walls, level1, door_type, X_WC, 7.0, 0.0)

    # --- 一层窗 ---
    building.opening_at(doc, walls, level1, win_type, 1.5, y0, 0.9)
    building.opening_at(doc, walls, level1, win_type, 9.5, y0, 0.9)
    building.opening_at(doc, walls, level1, win_type, x0, 2.5, 0.9)
    building.opening_at(doc, walls, level1, win_type, x1, 7.0, 0.9)
    building.opening_at(doc, walls, level1, win_type, 5.5, y1, 1.2)

    # --- 二层门 ---
    # 主卧门：内墙 X=4.0，Y=3.0
    building.opening_at(doc, walls2, level2, door_type, 4.0, 3.0, 0.0)
    # 次卧门：南北向主分隔墙 X=7.0，Y=3.0
    building.opening_at(doc, walls2, level2, door_type, X_MID, 3.0, 0.0)
    # 卫生间门：内墙 X=4.5，Y=7.0
    building.opening_at(doc, walls2, level2, door_type, X_WC, 7.0, 0.0)

    # --- 二层窗 ---
    building.opening_at(doc, walls2, level2, win_type, 2.0, y0, 0.9)
    building.opening_at(doc, walls2, level2, win_type, 9.5, y0, 0.9)
    building.opening_at(doc, walls2, level2, win_type, 2.0, y1, 0.9)
    building.opening_at(doc, walls2, level2, win_type, 5.5, y1, 1.2)
    building.opening_at(doc, walls2, level2, win_type, x0, 2.5, 0.9)

    t.Commit()
except Exception:
    t.RollBack()
    raise
finally:
    t.Dispose()

# ============ 楼梯（一层到二层） ============
# 楼梯间范围 X 7.0..12.0, Y 5.5..9.0
# 双跑楼梯：梯段宽 1.1m，踏步高 0.15m，踏步深 0.28m
# 层高 3.0m -> 20 级，每跑 10 级
# 因此放在主事务提交之后，用独立事务包裹。

building.stairs_u(doc, level1, level2, 8.0, 5.7, width_m=1.1, tread_m=0.28, risers=20)
