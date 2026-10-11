#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QColor>
#include <QJsonArray>
#include <QStringList>
#include <QVector>

#include <Qt3DCore/QEntity>
#include <Qt3DRender/QCamera>
#include <Qt3DRender/QDirectionalLight>
#include "preview_controller.h"
#include <Qt3DExtras/Qt3DWindow>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DCore/QTransform>
#include <Qt3DCore/QGeometry>
#include <Qt3DCore/QBuffer>
#include <Qt3DCore/QAttribute>
namespace GeometryApi = Qt3DCore;
#include <Qt3DRender/QGeometryRenderer>

#include "types.h"
#include "error_guard.h"
#include "ollama_llm.h"
#include "deepseek_llm.h"
#include "script_executor.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void slotSendClicked();
    void slotStopClicked();
    void slotStreamChunk(const QString &chunk);
    void slotLlmFinished(GenerateResult genRes);
    void slotRevitFinished(ExecuteResult execRes);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool m_inputComposing = false;
    void resizeEvent(QResizeEvent *event) override;
    void updateUiScale();
    qreal m_textScale = 1.0;
    bool m_scalingUi = false;
    // 顶部设置栏
    QComboBox* m_provider = nullptr;
    QLineEdit* m_edtModel = nullptr;
    QLineEdit* m_edtHost = nullptr;
    QSpinBox*  m_edtPort = nullptr;
    QLineEdit* m_edtApiKey = nullptr;
    QLineEdit* m_edtScriptDir = nullptr;
    QLabel*    m_lblStatus = nullptr;

    // 左侧聊天
    QSplitter*   m_splitter = nullptr;
    QWidget*     m_leftWidget = nullptr;
    QTextEdit*   m_txtChat = nullptr;
    QTextEdit*   m_edtInput = nullptr;
    QPushButton* m_btnSend = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnNewSession = nullptr;
    QPushButton* m_btnExport = nullptr;

    // Qt3D
    QWidget* m_3dContainer = nullptr;
    Qt3DExtras::Qt3DWindow* m_3dWindow = nullptr;
    Qt3DCore::QEntity* m_rootEntity = nullptr;
    Qt3DCore::QEntity* m_modelRoot = nullptr;
    Qt3DRender::QCamera* m_camera = nullptr;
    PreviewController* m_previewController = nullptr;

    SessionStore m_session;
    OllamaLLM* m_llm = nullptr;
    DeepSeekLLM* m_deepseek = nullptr;
    ScriptExecutor* m_executor = nullptr;
    const int64_t SESSION_ID = 1;
    static const int MAX_REGENERATE = 3;   // 出错后最多自动重新生成次数

    bool m_generating = false;
    bool m_aiHeaderShown = false;
    int  m_regenerateCount = 0;
    ErrorGuard m_errorGuard;
    QString m_pendingScript;
    QStringList m_attemptedScripts;
    bool m_chatOnly = false;

    void setupTopBar(QVBoxLayout *rootLayout);
    void setupChatPanel(QSplitter *splitter);
    void setup3d(QSplitter *splitter);

    void appendMessage(const QString &title, const QString &content,
                       const QColor &titleColor, const QColor &bodyColor = QColor("#22252a"));
    void appendStream(const QString &text);
    void setStatus(const QString &text, const QString &color);
    void finishGeneration();
    void startGeneration(const std::vector<ChatMessage> &msgs);
    void regenerateAfterError(const QString &feedback);
    std::vector<ChatMessage> buildMessagesForGeneration();
    void loadErrorMemory();
    bool rememberError(const QString &issue);
    QString errorGuardPrompt() const;
    bool validatePyRevit27(const QString &script, QString &issue) const;
    void renderRevitElements(const QJsonArray &elements);
    void renderMeshFile(const QString &path);
    void renderTriangleMesh(const QVector<QVector3D> &positions,
                            const QVector<QVector3D> &normals);
    void clear3dScene();
};

#endif // MAINWINDOW_H
