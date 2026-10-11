#include "preview_controller.h"
#include <QWindow>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QQuaternion>
#include <QtMath>
#include <algorithm>
#include <cmath>

PreviewController::PreviewController(Qt3DRender::QCamera *camera, QWindow *window, QWidget *container)
    : QObject(container), m_camera(camera), m_window(window)
{
    // Handle the native embedded window as well as its QWidget wrapper.
    // Camera interaction therefore does not depend on Qt3D's input aspect.
    window->installEventFilter(this);
    container->installEventFilter(this);
    container->setFocusPolicy(Qt::StrongFocus);
    container->setMouseTracking(true);
    saveHome();
}
void PreviewController::saveHome() {
    m_homePosition = m_camera->position();
    m_homeCenter = m_camera->viewCenter();
}
void PreviewController::restoreHome() {
    m_camera->setPosition(m_homePosition);
    m_camera->setViewCenter(m_homeCenter);
    m_camera->setUpVector(QVector3D(0, 1, 0));
    const float distance = (m_homePosition - m_homeCenter).length();
    m_camera->lens()->setFarPlane(std::max(1000.0f, distance * 20));
    m_camera->lens()->setNearPlane(std::max(0.001f, distance / 10000));
    m_buttons = Qt::NoButton;
}
void PreviewController::orbit(const QPoint &delta) {
    const auto center = m_camera->viewCenter();
    auto offset = m_camera->position() - center;
    offset = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), -delta.x() * 0.35f).rotatedVector(offset);
    const auto right = QVector3D::crossProduct(-offset.normalized(), QVector3D(0, 1, 0)).normalized();
    const auto tilted = QQuaternion::fromAxisAndAngle(right, -delta.y() * 0.35f).rotatedVector(offset);
    if(std::abs(tilted.normalized().y()) < 0.995f) offset = tilted;
    m_camera->setPosition(center + offset);
    m_camera->setUpVector(QVector3D(0, 1, 0));
}
void PreviewController::pan(const QPoint &delta) {
    const auto direction = (m_camera->viewCenter() - m_camera->position()).normalized();
    const auto right = QVector3D::crossProduct(direction, m_camera->upVector()).normalized();
    const auto up = QVector3D::crossProduct(right, direction).normalized();
    const float distance = (m_camera->position() - m_camera->viewCenter()).length();
    const float scale = 2 * distance * std::tan(qDegreesToRadians(m_camera->lens()->fieldOfView() / 2))
        / std::max(1, m_window->height());
    const auto shift = (right * -delta.x() + up * delta.y()) * scale;
    m_camera->setPosition(m_camera->position() + shift);
    m_camera->setViewCenter(m_camera->viewCenter() + shift);
}
void PreviewController::zoom(float steps) {
    auto offset = m_camera->position() - m_camera->viewCenter();
    const float distance = std::max(0.05f, std::min(1.0e8f, offset.length() * std::exp(-steps * 0.15f)));
    if(offset.lengthSquared() < 1e-10f) offset = QVector3D(1, 1, 1);
    m_camera->setPosition(m_camera->viewCenter() + offset.normalized() * distance);
    m_camera->lens()->setNearPlane(std::max(0.001f, distance / 10000));
    m_camera->lens()->setFarPlane(std::max(1000.0f, distance * 20));
}
bool PreviewController::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched);
    switch(event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        m_last = mouse->pos();
        m_buttons = mouse->buttons();
        m_window->requestActivate();
        return true;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const auto delta = mouse->pos() - m_last;
        m_last = mouse->pos();
        const auto buttons = mouse->buttons() | m_buttons;
        if(buttons & (Qt::RightButton | Qt::MiddleButton)) pan(delta);
        else if(buttons & Qt::LeftButton) orbit(delta);
        return buttons != Qt::NoButton;
    }
    case QEvent::MouseButtonRelease:
        m_buttons = static_cast<QMouseEvent *>(event)->buttons();
        return true;
    case QEvent::Wheel:
        zoom(static_cast<QWheelEvent *>(event)->angleDelta().y() / 120.0f);
        return true;
    case QEvent::MouseButtonDblClick:
        restoreHome();
        return true;
    case QEvent::FocusOut:
        m_buttons = Qt::NoButton;
        break;
    default: break;
    }
    return false;
}
