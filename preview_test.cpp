#include "preview_controller.h"
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        Qt3DRender::QCamera camera;
        camera.setPosition(QVector3D(10, 8, 10));
        camera.setViewCenter(QVector3D());
        camera.lens()->setPerspectiveProjection(45, 1, 0.01, 1000);
        QWindow window;
        window.resize(800, 600);
        QWidget container;
        PreviewController controller(&camera, &window, &container);
        const auto home = camera.position();
        auto drag = [&](QObject *target, Qt::MouseButton button) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), button, button, Qt::NoModifier);
            QMouseEvent move(QEvent::MouseMove, QPointF(140, 120), Qt::NoButton, button, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(140, 120), button, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QApplication::sendEvent(target, &move);
            QApplication::sendEvent(target, &release);
        };
        drag(&window, Qt::LeftButton);
        if((camera.position() - home).length() < 0.1 || camera.viewCenter().length() > 0.01)
            throw std::runtime_error("left drag did not orbit about model center");
        if(std::abs(camera.position().length() - home.length()) > 0.01)
            throw std::runtime_error("orbit changed distance");
        const auto offset = camera.position() - camera.viewCenter();
        drag(&container, Qt::RightButton);
        if(camera.viewCenter().length() < 0.1 || ((camera.position() - camera.viewCenter()) - offset).length() > 0.01)
            throw std::runtime_error("right drag did not pan camera and target together");
        const float before = (camera.position() - camera.viewCenter()).length();
        QWheelEvent wheel(QPointF(100,100), QPointF(100,100), QPoint(), QPoint(0,120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&window, &wheel);
        if((camera.position() - camera.viewCenter()).length() >= before)
            throw std::runtime_error("wheel did not zoom");
        QMouseEvent reset(QEvent::MouseButtonDblClick, QPointF(100,100), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &reset);
        if((camera.position() - home).length() > 0.01 || camera.viewCenter().length() > 0.01)
            throw std::runtime_error("double click did not restore fitted view");
        std::cout << "native-window rotation, container panning, zoom and reset passed\n";
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
