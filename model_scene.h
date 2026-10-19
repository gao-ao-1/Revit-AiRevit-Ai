// 不依赖窗口的模型数据层：加载 OBJ 与 elements.json，保存构件、材质和包围盒。
// 导出数据使用英尺、Z 轴向上；加载时映射为 Qt 的 (x,z,-y)，长度数值保持不变。
// 提供射线拾取，便于界面交互以及独立测试。

#pragma once
#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QVector3D>

// 轴对齐包围盒；valid=false 表示尚未纳入任何有效几何，不能直接使用上下界。
struct ModelBounds {
    QVector3D minimum, maximum;
    bool valid = false;
    // 把点或有效包围盒纳入当前轴对齐范围；首次加入时初始化上下界。
    void include(const QVector3D &point);
    // 把点或有效包围盒纳入当前轴对齐范围；首次加入时初始化上下界。
    void include(const ModelBounds &other);
};

// 一个构件中共用材质的三角形块；positions/normals 平行存储，每连续三个顶点构成一个面。
struct ModelPart {
    QString material;
    QColor color = QColor(176, 186, 202);
    // 不透明度在 [0,1]，由导出材质透明百分比换算。
    float opacity = 1.0f;
    QVector<QVector3D> positions, normals;
};

// 一个 Revit 构件的数据；id 使用字符串，避免 JSON double 丢失 64 位标识精度。
struct ModelElement {
    QString id;
    QJsonObject properties;
    QVector<ModelPart> parts;
    ModelBounds bounds;
    // 仅控制 Qt 预览和拾取，不回写 Revit 可见性。
    bool visible = true;
};

// Data and picking are independent of widgets and the graphics backend.
class ModelScene {
public:
    QVector<ModelElement> elements;
    // 完整的构件清单元数据，包括项目名、材质表与类型表。
    QJsonObject manifest;
    QStringList warnings;
    // 区分新版可检查属性的快照和只含几何的旧版 OBJ。
    bool hasMetadata = false;
    qsizetype triangleCount = 0;
    // 重置数据后读取可选属性清单和 OBJ，校验版本、单位、构件 ID 与三角形数量。
    // 支持负顶点索引、多边形扇形三角化和材质分块；失败时返回 false 并填充 error。
    bool load(const QString &meshPath, const QString &metadataPath, QString &error);
    // 合并构件包围盒；visibleOnly=true 时跳过隐藏构件，空集合返回 valid=false 的包围盒。
    ModelBounds bounds(bool visibleOnly = false) const;
    // 先用包围盒筛选，再逐三角形计算射线交点并保留最近结果；忽略隐藏构件，未命中返回 -1。
    int pick(const QVector3D &origin, const QVector3D &direction) const;
private:
    struct TriangleRef { int element, part, vertex; };
    struct PickNode { ModelBounds bounds; int begin = 0, count = 0, left = -1, right = -1; };
    QVector<TriangleRef> m_triangles;
    QVector<PickNode> m_pickNodes;
    void buildPickIndex();
    int buildPickNode(int begin, int end);
};
