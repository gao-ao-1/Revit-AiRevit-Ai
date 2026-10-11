#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
#include "types.h"
class ScriptExecutor : public QObject {
    Q_OBJECT
public:
    explicit ScriptExecutor(QObject *parent = nullptr);
    void setScriptDir(const std::string &dir);
    void push_script_async(const std::string &python_script);
signals:
    void finished(const ExecuteResult &res);
private slots:
    void onPoll();
private:
    void reportFailure(const QString &error);
    QTimer *m_pollTimer = nullptr;
    QElapsedTimer m_elapsed;
    QString m_scriptDir;
    QString m_requestId;
};
