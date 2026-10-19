// 只读构件浏览器：同步楼层/类别树、实例与类型属性、三维高亮和可见性。
// 场景元素、渲染记录与树叶节点通过索引关联；隐藏、隔离和搜索不会修改 Revit 文档。

#pragma once
#include "model_scene.h"
#include <QWidget>
#include <QPointer>
#include <Qt3DCore/QEntity>
#include <Qt3DRender/QCamera>
#include <QTimer>
#include <functional>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QToolButton;
class QWindow;

// Read-only model inspection. Visibility changes affect only the preview.
class ModelBrowser : public QWidget {
public:
    // 创建构件树、属性树、搜索框及操作按钮，连接选择、搜索和可见性事件。
    // 相机和场景根节点由外部提供；浏览器仅管理自己的模型内容子树。
    ModelBrowser(Qt3DCore::QEntity *root, Qt3DRender::QCamera *camera, QWindow *window, QWidget *parent = nullptr);
    // 先在候选 ModelScene 中完整解析，成功后才替换现有场景并重建树与实体。
    // 失败保留浏览器原场景；主窗口调用方可按业务需求另行清空旧预览。
    bool load(const QString &mesh, const QString &metadata, QString &error);
    void loadAsync(const QString &mesh, const QString &metadata, std::function<void(const QString &)> done);
    bool isLoading() const { return m_loading; }
    // 删除浏览器自有渲染子树，清空索引、模型数据、属性树和操作状态。
    void clear();
    // 将逻辑像素鼠标坐标反投影为近远平面射线，选择最近可见三角形对应的构件。
    // 命中后展开树的祖先并滚动到叶节点；未命中则清除选择。
    void pickAt(const QPoint &position);
    // 对整个场景的包围盒取景，包含当前隐藏构件，便于恢复全模型范围。
    void fitAll();
    const ModelScene &scene() const { return m_scene; }
private:
    struct RenderedElement {
        Qt3DCore::QEntity *entity = nullptr;
        QVector<QObject *> materials;
        QTreeWidgetItem *item = nullptr;
        QString searchText;
    };
    ModelScene m_scene;
    Qt3DCore::QEntity *m_root;
    // 浏览器拥有的渲染子树，QPointer 在对象销毁后自动置空。
    QPointer<Qt3DCore::QEntity> m_content;
    Qt3DRender::QCamera *m_camera;
    QWindow *m_window;
    // 与 m_scene.elements 一一对应的实体、材质及树叶节点。
    QVector<RenderedElement> m_rendered;
    QTreeWidget *m_tree, *m_properties;
    QLineEdit *m_search;
    QLabel *m_summary;
    QToolButton *m_hide, *m_isolate, *m_focus;
    // 当前单个高亮构件索引；-1 表示无叶节点选择。
    int m_selected = -1;
    quint64 m_loadGeneration = 0;
    bool m_loading = false;
    QTimer *m_buildTimer = nullptr, *m_searchTimer = nullptr, *m_visibilityTimer = nullptr;
    std::function<void(const QString &)> m_loadDone;
    void buildElement(int index);
    void buildBatch();
    // 为每个构件建立实体，再按材质分块创建三角形几何；透明部分使用带 alpha 的材质。
    void buildScene();
    // 按楼层和类别建立可勾选树；叶节点 UserRole 保存场景索引，构建时阻断信号避免中间态回调。
    void buildTree();
    // 取消上次高亮、解析当前叶节点并更新属性面板；分组节点仍可进行批量可见性操作。
    void selectCurrent();
    // 重建属性树，显示构件信息与参数；无叶节点选择时显示提示或分组数量。
    void showProperties(int index);
    // 切换指定构件的高亮颜色和透明度，取消选择时从原始材质数据恢复。
    void setHighlight(int index, bool selected);
    // 根据搜索文本过滤构件树及祖先分组的显示；搜索仅筛选列表，不改变模型实体的可见性。
    void filterTree();
    // 把叶节点勾选状态同步到数据层 visible 与渲染实体，并刷新统计。
    void syncVisibility();
    // 执行可见性操作：mode=0 隐藏当前组，mode=1 仅显示当前组，mode=2 显示全部。
    // 批量改勾选时阻断信号，完成后统一同步；隔离后对可见模型重新取景。
    void setVisibility(int mode);
    // 以包围盒中心为观察中心，依据水平与垂直视场中较小者计算安全取景距离，并更新裁剪面。
    void fit(const ModelBounds &bounds);
    // 递归收集当前树节点下的全部构件索引，使楼层或类别选择也能用于隐藏、隔离和聚焦。
    QVector<int> currentIndices() const;
    // 显示项目范围、可见构件数与三角形数；旧版网格另提示缺少元数据，警告放入工具提示。
    void updateSummary();
};
