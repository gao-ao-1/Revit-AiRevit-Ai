// 不依赖窗口的模型数据层：加载 OBJ 与 elements.json，保存构件、材质和包围盒。
// 导出数据使用英尺、Z 轴向上；加载时映射为 Qt 的 (x,z,-y)，长度数值保持不变。
// 提供射线拾取，便于界面交互以及独立测试。

#include "model_scene.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>
#include <QStringConverter>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

// 把点或有效包围盒纳入当前轴对齐范围；首次加入时初始化上下界。
void ModelBounds::include(const QVector3D &point) {
    if(!valid) { minimum = maximum = point; valid = true; return; }
    for(int axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], point[axis]);
        maximum[axis] = std::max(maximum[axis], point[axis]);
    }
}
// 把点或有效包围盒纳入当前轴对齐范围；首次加入时初始化上下界。
void ModelBounds::include(const ModelBounds &other) {
    if(other.valid) { include(other.minimum); include(other.maximum); }
}
// 合并构件包围盒；visibleOnly=true 时跳过隐藏构件，空集合返回 valid=false 的包围盒。
ModelBounds ModelScene::bounds(bool visibleOnly) const {
    ModelBounds result;
    for(const auto &element : elements)
        if(!visibleOnly || element.visible) result.include(element.bounds);
    return result;
}

// 重置数据后读取可选属性清单和 OBJ，校验版本、单位、构件 ID 与三角形数量。
// 支持负顶点索引、多边形扇形三角化和材质分块；失败时返回 false 并填充 error。
bool ModelScene::load(const QString &meshPath, const QString &metadataPath, QString &error) {
    *this = ModelScene();
    error.clear();
    // 显式 metadataPath 必须可读；未显式指定时，允许缺少 elements.json 的旧版 OBJ 降级加载。
    const QString companion = metadataPath.isEmpty()
        ? QFileInfo(meshPath).dir().filePath("elements.json") : metadataPath;
    QHash<QString, QJsonObject> records;
    if(!metadataPath.isEmpty() || QFileInfo::exists(companion)) {
        QFile metadata(companion);
        if(!metadata.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("无法读取构件属性：") + metadata.errorString(); return false;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(metadata.readAll(), &parseError);
        manifest = document.object();
        if(parseError.error != QJsonParseError::NoError || !document.isObject()
           || manifest.value("schema_version").toInt() != 1
           || !manifest.value("elements").isArray()
           || manifest.value("length_unit").toString() != "feet"
           || manifest.value("up_axis").toString() != "Z") {
            error = QStringLiteral("构件属性格式或版本不支持，请更新监听器并重新导出。"); return false;
        }
        for(const auto &entry : manifest.value("elements").toArray()) {
            const auto record = entry.toObject();
            const QString id = record.value("id").toString();
            if(id.isEmpty() || records.contains(id)) {
                error = QStringLiteral("构件属性包含无效或重复的构件 ID。"); return false;
            }
            records.insert(id, record);
        }
        hasMetadata = true;
    } else {
        warnings << QStringLiteral("旧版网格缺少构件属性；更新 Revit 监听器并重新导出后可查看详情。");
    }
    QFile file(meshPath);
    if(!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取模型网格：") + file.errorString(); return false;
    }
    QVector<QVector3D> vertices;
    QHash<QString, int> indices;
    // Per-object material lookup avoids searching all elements for each triangle.
    QVector<QHash<QString, int>> partIndices;
    QString object = "legacy", material = "default";
    const auto materials = manifest.value("materials").toObject();
    const auto types = manifest.value("types").toObject();
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    qsizetype lineNumber = 0;
    const QRegularExpression whitespace(QStringLiteral("\\s+"));
    while(!stream.atEnd()) {
        QString line = stream.readLine().section('#', 0, 0).trimmed();
        ++lineNumber;
        const auto tokens = line.split(whitespace, Qt::SkipEmptyParts);
        if(tokens.isEmpty()) continue;
        const auto fail = [&]() {
            error = QStringLiteral("模型网格第 %1 行无效。").arg(lineNumber); return false;
        };
        if(tokens[0] == "v") {
            if(tokens.size() < 4) return fail();
            bool ok[3];
            float x = tokens[1].toFloat(&ok[0]), y = tokens[2].toFloat(&ok[1]), z = tokens[3].toFloat(&ok[2]);
            if(!ok[0] || !ok[1] || !ok[2] || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return fail();
            vertices.append(QVector3D(x, z, -y)); // Revit Z-up -> Qt Y-up.
        } else if(tokens[0] == "o" || tokens[0] == "g") {
            object = tokens.mid(1).join(' ');
            if(object.startsWith("element_")) object.remove(0, 8);
            if(object.isEmpty()) object = "legacy";
            material = "default";
        } else if(tokens[0] == "usemtl") {
            material = tokens.value(1, "default");
        } else if(tokens[0] == "f") {
            if(tokens.size() < 4) return fail();
            QVector<int> face;
            for(qsizetype i = 1; i < tokens.size(); ++i) {
                bool ok;
                int index = tokens[i].section('/', 0, 0).toInt(&ok);
                if(!ok || index == 0) return fail();
                // OBJ 正索引从 1 开始，负索引相对于当前已读顶点尾部；0 不合法。
                index = index < 0 ? int(vertices.size()) + index : index - 1;
                if(index < 0 || index >= vertices.size()) return fail();
                face.append(index);
            }
            if(!indices.contains(object)) {
                if(hasMetadata && !records.contains(object)) {
                    error = QStringLiteral("网格与属性中的构件 ID 不一致，请重新导出。"); return false;
                }
                ModelElement element;
                element.id = object;
                element.properties = records.value(object);
                if(!hasMetadata) element.properties.insert("name", object == "legacy" ? QStringLiteral("整体模型（旧版）") : object);
                element.properties.insert("type", types.value(element.properties.value("type_id").toString()));
                indices.insert(object, int(elements.size()));
                elements.append(element);
                partIndices.append(QHash<QString, int>());
            }
            const int index = indices.value(object);
            auto &element = elements[index];
            if(!partIndices[index].contains(material)) {
                ModelPart part;
                part.material = material;
                const auto description = materials.value(material).toObject();
                const auto color = description.value("color").toArray();
                if(color.size() == 3)
                    part.color = QColor(qBound(0, color[0].toInt(), 255), qBound(0, color[1].toInt(), 255), qBound(0, color[2].toInt(), 255));
                part.opacity = float(qBound(0.0, description.value("opacity").toDouble(1.0), 1.0));
                partIndices[index].insert(material, int(element.parts.size()));
                element.parts.append(part);
            }
            auto &part = element.parts[partIndices[index].value(material)];
            // 以首顶点为扇心拆分多边形，并为每个三角形计算平面法线。
            // 导出器主要输出三角形；这里的扇形算法并不是任意凹多边形的通用剖分。
            for(qsizetype i = 1; i + 1 < face.size(); ++i) {
                const auto a = vertices[face[0]], b = vertices[face[i]], c = vertices[face[i + 1]];
                const auto normal = QVector3D::crossProduct(b - a, c - a).normalized();
                part.positions << a << b << c;
                part.normals << normal << normal << normal;
                element.bounds.include(a); element.bounds.include(b); element.bounds.include(c);
                ++triangleCount;
            }
        }
    }
    if(file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
    if(hasMetadata) {
        // 属性与网格必须来自同一快照；数量或逐构件三角形统计不匹配时拒绝展示。
        if(records.size() != elements.size()) {
            error = QStringLiteral("构件网格不完整，请重新导出。"); return false;
        }
        for(const auto &element : elements) {
            qsizetype count = 0;
            for(const auto &part : element.parts) count += part.positions.size() / 3;
            if(element.properties.contains("triangles") && element.properties.value("triangles").toDouble() != double(count)) {
                error = QStringLiteral("构件网格与属性版本不一致，请重新导出。"); return false;
            }
        }
    } else if(elements.isEmpty()) {
        error = QStringLiteral("网格没有三角形，且缺少有效的空项目属性文件。"); return false;
    }
    buildPickIndex();
    return true;
}

void ModelScene::buildPickIndex() {
    m_triangles.clear(); m_pickNodes.clear();
    m_triangles.reserve(triangleCount);
    const auto &items = std::as_const(elements);
    for(int e=0;e<items.size();++e)
        for(int p=0;p<items[e].parts.size();++p)
            for(int v=0;v+2<items[e].parts[p].positions.size();v+=3) m_triangles.append({e,p,v});
    if(!m_triangles.isEmpty()) buildPickNode(0, int(m_triangles.size()));
}

int ModelScene::buildPickNode(int begin, int end) {
    PickNode node; node.begin=begin; node.count=end-begin;
    const auto &items=std::as_const(elements);
    for(int i=begin;i<end;++i) {
        const auto ref=m_triangles[i]; const auto &points=items[ref.element].parts[ref.part].positions;
        for(int k=0;k<3;++k) node.bounds.include(points[ref.vertex+k]);
    }
    const int index=int(m_pickNodes.size());m_pickNodes.append(node);
    if(end-begin<=12) return index;
    const auto span=node.bounds.maximum-node.bounds.minimum;
    int axis=span.y()>span.x()?1:0;if(span.z()>span[axis]) axis=2;
    const int middle=begin+(end-begin)/2;
    auto center=[&](const TriangleRef &ref) {
        const auto &p=items[ref.element].parts[ref.part].positions;
        return p[ref.vertex][axis]+p[ref.vertex+1][axis]+p[ref.vertex+2][axis];
    };
    std::nth_element(m_triangles.begin()+begin,m_triangles.begin()+middle,m_triangles.begin()+end,
        [&](const TriangleRef &a,const TriangleRef &b){return center(a)<center(b);});
    const int left=buildPickNode(begin,middle),right=buildPickNode(middle,end);
    m_pickNodes[index].left=left;m_pickNodes[index].right=right;
    return index;
}

namespace {
// 使用逐轴 slab 区间判断射线与包围盒是否相交；limit 为当前最近命中距离，用于提前排除远物体。
bool intersectsBox(const ModelBounds &bounds, const QVector3D &origin, const QVector3D &direction, float limit) {
    float near = 0, far = limit;
    for(int axis = 0; axis < 3; ++axis) {
        if(std::abs(direction[axis]) < 1e-8f) {
            if(origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis]) return false;
        } else {
            float a = (bounds.minimum[axis] - origin[axis]) / direction[axis];
            float b = (bounds.maximum[axis] - origin[axis]) / direction[axis];
            if(a > b) std::swap(a, b);
            near = std::max(near, a); far = std::min(far, b);
            if(near > far) return false;
        }
    }
    return true;
}
}
// 先用包围盒筛选，再逐三角形计算射线交点并保留最近结果；忽略隐藏构件，未命中返回 -1。
int ModelScene::pick(const QVector3D &origin, const QVector3D &direction) const {
    if(direction.lengthSquared() < 1e-12f) return -1;
    float closest = std::numeric_limits<float>::max();
    int selected = -1;
    std::function<void(int)> visit = [&](int nodeIndex) {
        const auto &node=m_pickNodes[nodeIndex];
        if(!intersectsBox(node.bounds,origin,direction,closest)) return;
        if(node.left>=0) { visit(node.left); visit(node.right); return; }
        for(int t=node.begin;t<node.begin+node.count;++t) {
                const auto ref=m_triangles[t];
                const auto &element=elements[ref.element];
                if(!element.visible) continue;
                const auto &part=element.parts[ref.part];const int i=ref.vertex;
                const auto a = part.positions[i], edge1 = part.positions[i + 1] - a, edge2 = part.positions[i + 2] - a;
                // Möller–Trumbore 求交：先检查射线是否平行，再检查重心坐标 u/v，最后比较正向距离。
                const auto p = QVector3D::crossProduct(direction, edge2);
                const float determinant = QVector3D::dotProduct(edge1, p);
                if(std::abs(determinant) < 1e-8f) continue;
                const auto offset = origin - a;
                const float u = QVector3D::dotProduct(offset, p) / determinant;
                if(u < 0 || u > 1) continue;
                const auto q = QVector3D::crossProduct(offset, edge1);
                const float v = QVector3D::dotProduct(direction, q) / determinant;
                if(v < 0 || u + v > 1) continue;
                const float distance = QVector3D::dotProduct(edge2, q) / determinant;
                if(distance >= 0 && (distance < closest || (distance == closest && ref.element < selected))) { closest = distance; selected = ref.element; }
        }
    };
    if(!m_pickNodes.isEmpty()) visit(0);
    return selected;
}
