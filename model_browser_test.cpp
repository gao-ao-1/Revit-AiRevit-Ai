// 构件浏览回归测试：构造 OBJ/属性快照验证坐标、材质、拾取、树控件与场景重载。
// 包括大整数 ID、空项目、损坏数据和旧版 OBJ，检查失败与兼容路径。

#include "model_browser.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QLineEdit>
#include <QWindow>
#include <Qt3DExtras/QPhongAlphaMaterial>
#include <iostream>
#include <stdexcept>
#include <QEventLoop>
#include <QTimer>

// 断言不成立时抛出异常，由测试入口打印原因并返回非零退出码。
static void require(bool ok, const char *message) { if(!ok) throw std::runtime_error(message); }
static void waitForEvents(int ms = 1) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
// 写入测试夹具并检查写入成功，便于模拟正常和损坏的通信文件。
static void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "cannot write fixture");
}

// 构件浏览回归测试：构造 OBJ/属性快照验证坐标、材质、拾取、树控件与场景重载。
// 包括大整数 ID、空项目、损坏数据和旧版 OBJ，检查失败与兼容路径。
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory unavailable");
        const QString mesh = directory.filePath("model.obj"), metadata = directory.filePath("elements.json");
        write(mesh, "o element_9007199254740993\nusemtl stone\nv 0 0 0\nv 2 0 0\nv 0 0 2\nf 1 2 3\n"
                    "o element_202\nusemtl glass\nv 0 -2 0\nv 2 -2 0\nv 0 -2 2\nf -3 -2 -1\n");
        auto manifest = QJsonDocument::fromJson(R"({"schema_version":1,"document":"构件测试", "scope":"project",
          "length_unit":"feet","up_axis":"Z",
          "types":{"10":{"name":"200mm 外墙","family":"基本墙","parameters":[{"name":"宽度","value":"200 mm"}]}},
          "materials":{"stone":{"name":"石材","color":[170,160,150],"opacity":1},
                       "glass":{"name":"玻璃","color":[120,190,220],"opacity":0.4}},
          "elements":[
            {"id":"9007199254740993","name":"外墙","category":"墙","category_id":"-1","level":"首层","level_id":"7",
             "type_id":"10","triangles":1,"parameters":[{"name":"高度","value":"3000 mm"}]},
            {"id":"202","name":"门","category":"门","category_id":"-2","level":"二层","level_id":"8","triangles":1}
          ]})").object();
        write(metadata, QJsonDocument(manifest).toJson());
        ModelScene scene;
        QString error;
        require(scene.load(mesh, metadata, error), qPrintable(error));
        require(scene.elements.size() == 2 && scene.triangleCount == 2, "objects were merged");
        require(scene.elements[0].id == "9007199254740993", "64-bit element ID lost precision");
        require(scene.elements[0].properties.value("type").toObject().value("name").toString() == QStringLiteral("200mm 外墙"), "type metadata lost");
        require(scene.elements[1].parts[0].color == QColor(120,190,220), "material color lost");
        require(scene.elements[1].parts[0].positions[0] == QVector3D(0,0,2), "Revit coordinate conversion wrong");
        const QVector3D origin(0.5f,0.5f,10), ray(0,0,-1);
        require(scene.pick(origin, ray) == 1, "nearest overlapping element not picked");
        scene.elements[1].visible = false;
        require(scene.pick(origin, ray) == 0, "hidden element intercepted selection");
        require(scene.pick(QVector3D(3,3,10), ray) == -1, "empty space selected a component");

        Qt3DCore::QEntity root;
        Qt3DRender::QCamera camera;
        camera.lens()->setPerspectiveProjection(45, 1, 0.01f, 1000);
        camera.setPosition(origin); camera.setViewCenter(QVector3D(0.5f,0.5f,0));
        QWindow window; window.resize(600,600);
        ModelBrowser browser(&root, &camera, &window);
        require(browser.load(mesh, metadata, error), qPrintable(error));
        auto *tree = browser.findChild<QTreeWidget *>("elementTree");
        auto *properties = browser.findChild<QTreeWidget *>("elementProperties");
        require(tree->topLevelItemCount() == 2, "floor hierarchy missing");
        auto leaf = [&](int index) -> QTreeWidgetItem * {
            for(QTreeWidgetItemIterator it(tree); *it; ++it)
                if((*it)->data(0, Qt::UserRole).isValid() && (*it)->data(0, Qt::UserRole).toInt() == index) return *it;
            return nullptr;
        };
        auto *wall = leaf(0), *door = leaf(1);
        require(wall && door, "element leaf missing");
        camera.setPosition(origin); camera.setViewCenter(QVector3D(0.5f,0.5f,0));
        browser.pickAt(QPoint(300,300));
        require(tree->currentItem() == door, "viewport selection did not sync tree");
        auto *glass = root.findChild<Qt3DExtras::QPhongAlphaMaterial *>();
        // QPhongAlphaMaterial incorporates its separate alpha in the returned diffuse QColor.
        require(glass && glass->diffuse().rgb() == QColor("#ffb638").rgb() && std::abs(glass->alpha() - 0.9f) < 0.001f,
                "selected geometry did not highlight");
        tree->setCurrentItem(wall);
        require(glass->diffuse().rgb() == QColor(120,190,220).rgb() && std::abs(glass->alpha() - 0.4f) < 0.001f, "deselection did not restore material");
        bool foundHeight = false, foundWidth = false;
        for(QTreeWidgetItemIterator it(properties); *it; ++it) {
            foundHeight |= (*it)->text(1) == "3000 mm";
            foundWidth |= (*it)->text(1) == "200 mm";
        }
        require(foundHeight && foundWidth, "instance/type properties absent");
        browser.findChild<QToolButton *>("hideElement")->click();
        require(!browser.scene().elements[0].visible && browser.scene().elements[1].visible, "hide changed wrong elements");
        browser.findChild<QToolButton *>("isolateElement")->click();
        require(browser.scene().elements[0].visible && !browser.scene().elements[1].visible, "isolate failed on hidden selection");
        browser.findChild<QToolButton *>("showAllElements")->click();
        require(browser.scene().elements[0].visible && browser.scene().elements[1].visible, "show all failed");
        wall->parent()->parent()->setCheckState(0, Qt::Unchecked);
        waitForEvents();
        require(!browser.scene().elements[0].visible, "floor checkbox did not hide its descendants");
        tree->setCurrentItem(wall->parent()->parent());
        browser.findChild<QToolButton *>("isolateElement")->click();
        require(browser.scene().elements[0].visible && !browser.scene().elements[1].visible, "floor isolation failed");
        browser.findChild<QToolButton *>("focusElement")->click();
        require((camera.viewCenter() - QVector3D(1,1,0)).length() < 0.01f, "focus did not center selected floor");
        browser.findChild<QLineEdit *>("elementSearch")->setText("202");
        waitForEvents(160);
        require(wall->isHidden() && !door->isHidden(), "search did not filter tree");
        require(browser.scene().elements[0].visible, "tree search changed model visibility");

        // A corrupt new snapshot cannot replace a valid scene with misleading partial data.
        write(mesh, "o element_missing\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
        require(!browser.load(mesh, metadata, error), "mismatched metadata accepted");
        require(browser.scene().elements.size() == 2, "failed load destroyed the previous scene");
        write(mesh, "o element_202\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 0 2 3\n");
        require(!scene.load(mesh, metadata, error), "invalid face index accepted");
        manifest.insert("elements", QJsonArray());
        write(metadata, QJsonDocument(manifest).toJson());
        write(mesh, "# valid empty project\n");
        require(browser.load(mesh, metadata, error), qPrintable(error));
        require(browser.scene().elements.isEmpty() && tree->topLevelItemCount() == 0 && properties->topLevelItemCount() == 0,
                "empty project left stale geometry or properties");
        require(root.findChildren<Qt3DExtras::QPhongAlphaMaterial *>().isEmpty(), "reload leaked rendered elements");
        QFile::remove(metadata);
        write(mesh, "v 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
        require(scene.load(mesh, {}, error) && !scene.hasMetadata && scene.elements.size() == 1, "legacy OBJ compatibility lost");
        require(!scene.load(mesh, metadata, error), "explicit missing metadata silently treated as legacy");
        bool completed=false;QString asyncError;
        browser.loadAsync(mesh, {}, [&](const QString &issue){completed=true;asyncError=issue;});
        require(browser.isLoading() && !completed, "async load blocked the caller");
        for(int i=0;i<100 && !completed;++i) waitForEvents(20);
        require(completed && asyncError.isEmpty() && !browser.isLoading() && browser.scene().elements.size()==1, "async load did not finish");
        completed=false;
        browser.loadAsync(mesh, {}, [&](const QString &){completed=true;});
        browser.clear();waitForEvents(100);
        require(!completed && browser.scene().elements.isEmpty(), "stale async result restored a cleared model");
        std::cout << "model groups, properties, materials, picking, visibility, search, reload and legacy compatibility passed\n";
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
