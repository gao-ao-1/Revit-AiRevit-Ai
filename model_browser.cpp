// 只读构件浏览器：同步楼层/类别树、实例与类型属性、三维高亮和可见性。
// 场景元素、渲染记录与树叶节点通过索引关联；隐藏、隔离和搜索不会修改 Revit 文档。

#include "model_browser.h"
#include <QTreeWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QSignalBlocker>
#include <QWindow>
#include <QJsonArray>
#include <QSet>
#include <QtMath>
#include <Qt3DCore/QGeometry>
#include <Qt3DCore/QBuffer>
#include <Qt3DCore/QAttribute>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QPhongAlphaMaterial>
#include <algorithm>
#include <cmath>
#include <functional>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <memory>

namespace {
// 将 QVector3D 序列复制到连续 float 缓冲，注册为 Qt3D 顶点属性，供位置或法线输入。
void addAttribute(Qt3DCore::QGeometry *geometry, const QString &name, const QVector<QVector3D> &values) {
    QByteArray bytes;
    bytes.resize(values.size() * 3 * sizeof(float));
    auto *raw = reinterpret_cast<float *>(bytes.data());
    for(qsizetype i = 0; i < values.size(); ++i)
        for(int axis = 0; axis < 3; ++axis) raw[i * 3 + axis] = values[i][axis];
    auto *buffer = new Qt3DCore::QBuffer(geometry);
    buffer->setData(bytes);
    auto *attribute = new Qt3DCore::QAttribute(geometry);
    attribute->setName(name);
    attribute->setVertexBaseType(Qt3DCore::QAttribute::Float);
    attribute->setVertexSize(3);
    attribute->setAttributeType(Qt3DCore::QAttribute::VertexAttribute);
    attribute->setBuffer(buffer);
    attribute->setByteStride(3 * sizeof(float));
    attribute->setCount(uint(values.size()));
    geometry->addAttribute(attribute);
}
// 读取属性字符串，空值使用界面占位文字，避免树节点和详情出现空标题。
QString display(const QJsonObject &object, const char *key, const QString &fallback = QStringLiteral("未指定")) {
    const QString value = object.value(key).toString();
    return value.isEmpty() ? fallback : value;
}
}

// 创建构件树、属性树、搜索框及操作按钮，连接选择、搜索和可见性事件。
// 相机和场景根节点由外部提供；浏览器仅管理自己的模型内容子树。
ModelBrowser::ModelBrowser(Qt3DCore::QEntity *root, Qt3DRender::QCamera *camera, QWindow *window, QWidget *parent)
    : QWidget(parent), m_root(root), m_camera(camera), m_window(window) {
    setObjectName("modelBrowser");
    setMinimumWidth(245);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_summary = new QLabel(QStringLiteral("等待模型导出"));
    m_summary->setObjectName("modelSummary");
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    m_search = new QLineEdit();
    m_search->setObjectName("elementSearch");
    m_search->setPlaceholderText(QStringLiteral("搜索名称、ID、类别或楼层"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    auto *actions = new QHBoxLayout();
    actions->setSpacing(4);
    auto button = [&](const QString &name, const QString &text, const QString &tip) {
        auto *result = new QToolButton();
        result->setObjectName(name); result->setText(text); result->setToolTip(tip);
        actions->addWidget(result); return result;
    };
    m_focus = button("focusElement", QStringLiteral("定位"), QStringLiteral("将视角对准选中的构件、类别或楼层"));
    m_hide = button("hideElement", QStringLiteral("隐藏"), QStringLiteral("仅在预览中隐藏所选内容"));
    m_isolate = button("isolateElement", QStringLiteral("隔离"), QStringLiteral("仅显示所选构件、类别或楼层"));
    auto *showAll = button("showAllElements", QStringLiteral("全部显示"), QStringLiteral("恢复所有构件的可见性"));
    actions->addStretch();
    layout->addLayout(actions);
    auto *panels = new QSplitter(Qt::Vertical);
    m_tree = new QTreeWidget();
    m_tree->setObjectName("elementTree");
    m_tree->setHeaderLabel(QStringLiteral("楼层 / 类别 / 构件"));
    m_tree->setUniformRowHeights(true);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);
    panels->addWidget(m_tree);
    m_properties = new QTreeWidget();
    m_properties->setObjectName("elementProperties");
    m_properties->setColumnCount(2);
    m_properties->setHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_properties->setAlternatingRowColors(true);
    m_properties->setWordWrap(true);
    m_properties->setColumnWidth(0, 125);
    m_properties->header()->setStretchLastSection(true);
    m_properties->setToolTip(QStringLiteral("只读快照；参数值采用 Revit 项目的显示单位。修改后请重新导出。"));
    panels->addWidget(m_properties);
    panels->setSizes({320, 260});
    layout->addWidget(panels, 1);
    m_searchTimer = new QTimer(this); m_searchTimer->setSingleShot(true); m_searchTimer->setInterval(120);
    connect(m_searchTimer, &QTimer::timeout, this, &ModelBrowser::filterTree);
    connect(m_search, &QLineEdit::textChanged, this, [this](){ m_searchTimer->start(); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](){ selectCurrent(); });
    m_visibilityTimer = new QTimer(this); m_visibilityTimer->setSingleShot(true);
    connect(m_visibilityTimer, &QTimer::timeout, this, &ModelBrowser::syncVisibility);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](){ m_visibilityTimer->start(0); });
    m_buildTimer = new QTimer(this); m_buildTimer->setInterval(0);
    connect(m_buildTimer, &QTimer::timeout, this, &ModelBrowser::buildBatch);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](){ m_focus->click(); });
    connect(m_hide, &QToolButton::clicked, this, [this](){ setVisibility(0); });
    connect(m_isolate, &QToolButton::clicked, this, [this](){ setVisibility(1); });
    connect(showAll, &QToolButton::clicked, this, [this](){ setVisibility(2); });
    connect(m_focus, &QToolButton::clicked, this, [this](){
        ModelBounds bounds;
        { QSignalBlocker blocker(m_tree);
          for(int index : currentIndices()) { m_rendered[index].item->setCheckState(0, Qt::Checked); bounds.include(m_scene.elements[index].bounds); } }
        syncVisibility(); fit(bounds);
    });
    selectCurrent();
}

// 删除浏览器自有渲染子树，清空索引、模型数据、属性树和操作状态。
void ModelBrowser::clear() {
    ++m_loadGeneration; m_buildTimer->stop(); m_searchTimer->stop(); m_visibilityTimer->stop();
    m_loading = false; m_loadDone = {}; m_tree->setEnabled(true); m_search->setEnabled(true);
    QSignalBlocker blocker(m_tree);
    delete m_content.data();
    m_content = nullptr;
    m_rendered.clear();
    m_scene = ModelScene();
    m_selected = -1;
    m_tree->clear(); m_properties->clear();
    m_summary->setText(QStringLiteral("等待模型导出"));
    m_hide->setEnabled(false); m_isolate->setEnabled(false); m_focus->setEnabled(false);
}

// 先在候选 ModelScene 中完整解析，成功后才替换现有场景并重建树与实体。
// 失败保留浏览器原场景；主窗口调用方可按业务需求另行清空旧预览。
bool ModelBrowser::load(const QString &mesh, const QString &metadata, QString &error) {
    ModelScene candidate;
    if(!candidate.load(mesh, metadata, error)) return false;
    clear();
    m_scene = std::move(candidate);
    buildScene(); buildTree(); filterTree(); fitAll(); updateSummary();
    return true;
}

void ModelBrowser::loadAsync(const QString &mesh, const QString &metadata, std::function<void(const QString &)> done) {
    clear();
    const auto generation=m_loadGeneration;
    m_loading=true; m_tree->setEnabled(false); m_search->setEnabled(false);
    m_summary->setText(QStringLiteral("正在后台读取模型与属性…"));
    struct Result { ModelScene scene; QString error; };
    auto *watcher=new QFutureWatcher<std::shared_ptr<Result>>(this);
    connect(watcher,&QFutureWatcher<std::shared_ptr<Result>>::finished,this,[this,watcher,generation,done](){
        const auto result=watcher->result();watcher->deleteLater();
        if(generation!=m_loadGeneration) return;
        if(!result->error.isEmpty()) { clear(); done(result->error); return; }
        m_scene=std::move(result->scene); m_rendered.reserve(m_scene.elements.size());
        m_content=new Qt3DCore::QEntity(m_root);m_content->setEnabled(false);
        m_loadDone=done;m_buildTimer->start();
    });
    watcher->setFuture(QtConcurrent::run([mesh,metadata](){
        auto result=std::make_shared<Result>();
        try { result->scene.load(mesh,metadata,result->error); }
        catch(const std::exception &e) {result->error=QString::fromUtf8(e.what());}
        catch(...) {result->error=QStringLiteral("读取模型失败");}
        return result;
    }));
}

void ModelBrowser::buildBatch() {
    QElapsedTimer timer; timer.start();
    while(m_rendered.size()<m_scene.elements.size()) {
        buildElement(int(m_rendered.size()));
        if(timer.elapsed()>=6) break;
    }
    m_summary->setText(QStringLiteral("正在创建预览：%1 / %2 个构件…").arg(m_rendered.size()).arg(m_scene.elements.size()));
    if(m_rendered.size()!=m_scene.elements.size()) return;
    m_buildTimer->stop(); buildTree(); filterTree(); fitAll();updateSummary();
    m_content->setEnabled(true);m_loading=false;m_tree->setEnabled(true);m_search->setEnabled(true);
    auto done=std::move(m_loadDone);m_loadDone={};if(done) done({});
}

// 为每个构件建立实体，再按材质分块创建三角形几何；透明部分使用带 alpha 的材质。
void ModelBrowser::buildScene() {
    m_content = new Qt3DCore::QEntity(m_root);
    m_rendered.reserve(m_scene.elements.size());
    for(int i=0;i<m_scene.elements.size();++i) buildElement(i);
}

void ModelBrowser::buildElement(int index) {
        const auto &element=std::as_const(m_scene.elements)[index];
        RenderedElement rendered;
        rendered.entity = new Qt3DCore::QEntity(m_content);
        rendered.entity->setObjectName("element_" + element.id);
        for(const auto &part : element.parts) {
            auto *entity = new Qt3DCore::QEntity(rendered.entity);
            auto *geometry = new Qt3DCore::QGeometry(entity);
            addAttribute(geometry, Qt3DCore::QAttribute::defaultPositionAttributeName(), part.positions);
            addAttribute(geometry, Qt3DCore::QAttribute::defaultNormalAttributeName(), part.normals);
            auto *renderer = new Qt3DRender::QGeometryRenderer(entity);
            renderer->setGeometry(geometry);
            renderer->setPrimitiveType(Qt3DRender::QGeometryRenderer::Triangles);
            renderer->setVertexCount(int(part.positions.size()));
            entity->addComponent(renderer);
            if(part.opacity < 0.999f) {
                auto *material = new Qt3DExtras::QPhongAlphaMaterial(entity);
                material->setDiffuse(part.color); material->setAmbient(part.color.darker(170));
                material->setAlpha(part.opacity); material->setShininess(25);
                entity->addComponent(material); rendered.materials.append(material);
            } else {
                auto *material = new Qt3DExtras::QPhongMaterial(entity);
                material->setDiffuse(part.color); material->setAmbient(part.color.darker(170));
                material->setShininess(25);
                entity->addComponent(material); rendered.materials.append(material);
            }
        }
        m_rendered.append(rendered);
}

// 按楼层和类别建立可勾选树；叶节点 UserRole 保存场景索引，构建时阻断信号避免中间态回调。
void ModelBrowser::buildTree() {
    QSignalBlocker blocker(m_tree);
    m_tree->setUpdatesEnabled(false);
    QHash<QString, QTreeWidgetItem *> levels;
    QHash<QTreeWidgetItem *, QHash<QString, QTreeWidgetItem *>> categories;
    auto group = [](QTreeWidgetItem *parent, const QString &label) {
        auto *item = new QTreeWidgetItem(parent, {label});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        item->setCheckState(0, Qt::Checked);
        return item;
    };
    for(qsizetype index = 0; index < m_scene.elements.size(); ++index) {
        const auto &element = m_scene.elements[index];
        const auto &properties = element.properties;
        const QString levelKey = display(properties, "level_id", "none");
        if(!levels.contains(levelKey)) levels.insert(levelKey, group(m_tree->invisibleRootItem(), display(properties, "level", QStringLiteral("未关联楼层"))));
        auto *level = levels.value(levelKey);
        const QString categoryKey = display(properties, "category_id", display(properties, "category"));
        if(!categories[level].contains(categoryKey)) categories[level].insert(categoryKey, group(level, display(properties, "category", QStringLiteral("未分类"))));
        auto *item = new QTreeWidgetItem(categories[level].value(categoryKey), {display(properties, "name") + " [" + element.id + "]"});
        item->setData(0, Qt::UserRole, int(index));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setToolTip(0, item->text(0));
        m_rendered[index].item = item;
        const auto type=properties.value("type").toObject();
        m_rendered[index].searchText=(item->text(0)+' '+properties.value("category").toString()+' '
            +properties.value("level").toString()+' '+type.value("name").toString()+' '+type.value("family").toString()).toCaseFolded();
    }
    m_tree->sortItems(0, Qt::AscendingOrder);
    m_tree->expandToDepth(1);
    m_tree->setUpdatesEnabled(true);
}

// 递归收集当前树节点下的全部构件索引，使楼层或类别选择也能用于隐藏、隔离和聚焦。
QVector<int> ModelBrowser::currentIndices() const {
    QVector<int> result;
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if(!item) return;
        if(item->data(0, Qt::UserRole).isValid()) result.append(item->data(0, Qt::UserRole).toInt());
        for(int i = 0; i < item->childCount(); ++i) visit(item->child(i));
    };
    visit(m_tree->currentItem());
    return result;
}

// 切换指定构件的高亮颜色和透明度，取消选择时从原始材质数据恢复。
void ModelBrowser::setHighlight(int index, bool selected) {
    if(index < 0 || index >= m_rendered.size()) return;
    const auto &parts = m_scene.elements[index].parts;
    for(qsizetype i = 0; i < parts.size(); ++i) {
        auto *material = m_rendered[index].materials[i];
        const QColor color = selected ? QColor("#ffb638") : parts[i].color;
        material->setProperty("diffuse", color);
        material->setProperty("ambient", selected ? color.darker(125) : color.darker(170));
        if(parts[i].opacity < 0.999f) material->setProperty("alpha", selected ? 0.9f : parts[i].opacity);
    }
}

// 取消上次高亮、解析当前叶节点并更新属性面板；分组节点仍可进行批量可见性操作。
void ModelBrowser::selectCurrent() {
    setHighlight(m_selected, false);
    auto *item = m_tree->currentItem();
    m_selected = item && item->data(0, Qt::UserRole).isValid() ? item->data(0, Qt::UserRole).toInt() : -1;
    setHighlight(m_selected, true);
    const bool available = !currentIndices().isEmpty();
    m_hide->setEnabled(available); m_isolate->setEnabled(available); m_focus->setEnabled(available);
    showProperties(m_selected);
}

// 重建属性树，显示当前构件的基本信息、实例参数和类型参数；无叶节点选择时显示选择提示或分组数量。
void ModelBrowser::showProperties(int index) {
    m_properties->clear();
    auto section = [&](const QString &name) {
        auto *item = new QTreeWidgetItem(m_properties, {name});
        item->setFirstColumnSpanned(true);
        QFont font = item->font(0); font.setBold(true); item->setFont(0, font);
        return item;
    };
    auto row = [](QTreeWidgetItem *parent, const QString &name, const QString &value) {
        auto *item = new QTreeWidgetItem(parent, {name, value});
        item->setToolTip(0, name); item->setToolTip(1, value);
    };
    if(index < 0) {
        auto *general = section(QStringLiteral("构件详情"));
        row(general, QStringLiteral("提示"), currentIndices().isEmpty() ? QStringLiteral("点击模型或构件树查看属性")
            : QStringLiteral("已选分组，共 %1 个构件").arg(currentIndices().size()));
        m_properties->expandAll(); return;
    }
    const auto &element = m_scene.elements[index];
    const auto &properties = element.properties;
    const auto type = properties.value("type").toObject();
    auto *general = section(QStringLiteral("基本信息"));
    row(general, QStringLiteral("项目"), display(m_scene.manifest, "document"));
    row(general, QStringLiteral("构件 ID"), element.id);
    row(general, "UniqueId", display(properties, "unique_id"));
    row(general, QStringLiteral("名称"), display(properties, "name"));
    row(general, QStringLiteral("类别"), display(properties, "category"));
    row(general, QStringLiteral("楼层"), display(properties, "level"));
    row(general, QStringLiteral("族"), display(type, "family"));
    row(general, QStringLiteral("类型"), display(type, "name"));
    row(general, QStringLiteral("类型 ID"), display(properties, "type_id"));
    for(const char *key : {"metadata_warning", "geometry_warning"})
        if(properties.contains(key)) row(general, QStringLiteral("导出提示"), properties.value(key).toString());
    if(!m_scene.hasMetadata) row(general, QStringLiteral("提示"), QStringLiteral("请更新监听器并重新导出以获取属性"));
    auto *materials = section(QStringLiteral("材质"));
    for(const auto &part : element.parts) {
        const auto description = m_scene.manifest.value("materials").toObject().value(part.material).toObject();
        row(materials, display(description, "name", QStringLiteral("默认材质")),
            QStringLiteral("%1 · 不透明度 %2%").arg(part.color.name()).arg(qRound(part.opacity * 100)));
    }
    const auto addParameters = [&](const QString &name, const QJsonArray &parameters) {
        auto *group = section(name);
        for(const auto &value : parameters) {
            const auto parameter = value.toObject();
            row(group, parameter.value("name").toString(), parameter.value("value").toString());
        }
        if(parameters.isEmpty()) row(group, QStringLiteral("提示"), QStringLiteral("无可显示参数"));
    };
    addParameters(QStringLiteral("实例参数 · 项目显示单位"), properties.value("parameters").toArray());
    addParameters(QStringLiteral("类型参数 · 项目显示单位"), type.value("parameters").toArray());
    m_properties->expandAll();
}

// 根据搜索文本过滤构件树及祖先分组的显示；搜索仅筛选列表，不改变模型实体的可见性。
void ModelBrowser::filterTree() {
    if(m_loading && m_buildTimer->isActive()) return;
    const QString query = m_search->text().trimmed().toCaseFolded();
    m_tree->setUpdatesEnabled(false);
    for(qsizetype i = 0; i < m_rendered.size(); ++i) {
        auto *item = m_rendered[i].item;
        item->setHidden(!m_rendered[i].searchText.contains(query));
    }
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if(!item->childCount()) return !item->isHidden();
        bool any = false;
        for(int i = 0; i < item->childCount(); ++i) any = visit(item->child(i)) || any;
        item->setHidden(!any);
        if(any && !query.isEmpty()) item->setExpanded(true);
        return any;
    };
    for(int i = 0; i < m_tree->topLevelItemCount(); ++i) visit(m_tree->topLevelItem(i));
    m_tree->setUpdatesEnabled(true);
}

// 把叶节点勾选状态同步到数据层 visible 与渲染实体，并刷新统计。
void ModelBrowser::syncVisibility() {
    m_visibilityTimer->stop();
    if(m_loading) return;
    for(qsizetype i = 0; i < m_rendered.size(); ++i) {
        const bool visible = m_rendered[i].item->checkState(0) == Qt::Checked;
        if(m_scene.elements[i].visible!=visible) {
            m_scene.elements[i].visible = visible;
            m_rendered[i].entity->setEnabled(visible);
        }
    }
    updateSummary();
}

// 执行可见性操作：mode=0 隐藏当前组，mode=1 仅显示当前组，mode=2 显示全部。
// 批量改勾选时阻断信号，完成后统一同步；隔离后对可见模型重新取景。
void ModelBrowser::setVisibility(int mode) {
    if(m_loading) return;
    const auto selected = currentIndices();
    if(mode != 2 && selected.isEmpty()) return;
    const QSet<int> selection(selected.begin(), selected.end());
    { QSignalBlocker blocker(m_tree);
      for(qsizetype i = 0; i < m_rendered.size(); ++i) {
          const bool visible = mode == 2 || (mode == 1 ? selection.contains(int(i))
              : m_scene.elements[i].visible && !selection.contains(int(i)));
          m_rendered[i].item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
      } }
    syncVisibility();
    if(mode == 1) fit(m_scene.bounds(true));
}

// 将逻辑像素鼠标坐标反投影为近远平面射线，选择最近可见三角形对应的构件。
// 命中后展开树的祖先并滚动到叶节点；未命中则清除选择。
void ModelBrowser::pickAt(const QPoint &position) {
    if(m_loading) return;
    if(m_visibilityTimer->isActive()) syncVisibility();
    const QRect viewport(0, 0, m_window->width(), m_window->height());
    if(viewport.isEmpty()) return;
    // Both mouse coordinates and viewport use logical pixels, including high-DPI windows.
    const float y = float(viewport.height() - position.y());
    const auto near = QVector3D(float(position.x()), y, 0).unproject(m_camera->viewMatrix(), m_camera->projectionMatrix(), viewport);
    const auto far = QVector3D(float(position.x()), y, 1).unproject(m_camera->viewMatrix(), m_camera->projectionMatrix(), viewport);
    const int index = m_scene.pick(near, (far - near).normalized());
    if(index < 0) { m_tree->setCurrentItem(nullptr); m_tree->clearSelection(); return; }
    m_search->clear();
    m_searchTimer->stop();filterTree();
    auto *item = m_rendered[index].item;
    for(auto *parent = item->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
    m_tree->setCurrentItem(item); m_tree->scrollToItem(item);
}

// 以包围盒中心为观察中心，依据水平与垂直视场中较小者计算安全取景距离，并更新裁剪面。
void ModelBrowser::fit(const ModelBounds &bounds) {
    if(!bounds.valid) return;
    const auto center = (bounds.minimum + bounds.maximum) * 0.5f;
    const float radius = std::max(0.1f, (bounds.maximum - bounds.minimum).length() * 0.5f);
    const float vertical = qDegreesToRadians(m_camera->lens()->fieldOfView() * 0.5f);
    const float horizontal = std::atan(std::tan(vertical) * std::max(0.1f, m_camera->lens()->aspectRatio()));
    const float distance = radius * 1.15f / std::sin(std::min(vertical, horizontal));
    auto direction = (m_camera->position() - m_camera->viewCenter()).normalized();
    if(direction.isNull()) direction = QVector3D(1, 0.6f, 1).normalized();
    m_camera->setViewCenter(center); m_camera->setPosition(center + direction * distance);
    m_camera->lens()->setNearPlane(std::max(0.001f, distance / 10000));
    m_camera->lens()->setFarPlane(std::max(1000.0f, distance * 20));
}
// 对整个场景的包围盒取景，包含当前隐藏构件，便于恢复全模型范围。
void ModelBrowser::fitAll() { fit(m_scene.bounds()); }
// 显示项目范围、可见构件数与三角形数；旧版网格另提示缺少元数据，警告放入工具提示。
void ModelBrowser::updateSummary() {
    qsizetype visible = 0;
    for(const auto &element : m_scene.elements) if(element.visible) ++visible;
    const QString scope = m_scene.hasMetadata && m_scene.manifest.value("scope").toString() == "project"
        ? QStringLiteral("全项目") : QStringLiteral("模型预览");
    m_summary->setText(QStringLiteral("%1 · %2\n显示 %3 / %4 个构件 · %5 个三角形")
        .arg(display(m_scene.manifest, "document", QStringLiteral("模型")), scope)
        .arg(visible).arg(m_scene.elements.size()).arg(m_scene.triangleCount));
    if(!m_scene.hasMetadata)
        m_summary->setText(QStringLiteral("旧版网格 · 缺少构件属性\n%1 个三角形；请重新加载新版 Revit 监听器后导出")
            .arg(m_scene.triangleCount));
    m_summary->setToolTip(m_scene.warnings.join('\n'));
}
