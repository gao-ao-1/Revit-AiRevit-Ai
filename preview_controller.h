#pragma once
#include <QObject>
#include <QPoint>
#include <QVector3D>
#include <Qt3DRender/QCamera>
class QWindow;
class QWidget;

class PreviewController : public QObject {
public:
    PreviewController(Qt3DRender::QCamera *camera, QWindow *window, QWidget *container);
    void saveHome();
    void restoreHome();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void orbit(const QPoint &delta);
    void pan(const QPoint &delta);
    void zoom(float steps);
    Qt3DRender::QCamera *m_camera;
    QWindow *m_window;
    QPoint m_last;
    Qt::MouseButtons m_buttons = Qt::NoButton;
    QVector3D m_homePosition, m_homeCenter;
};
