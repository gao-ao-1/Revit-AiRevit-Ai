#include "script_executor.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
ScriptExecutor::ScriptExecutor(QObject *parent) : QObject(parent) {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &ScriptExecutor::onPoll);
}
void ScriptExecutor::setScriptDir(const std::string &dir) {
    m_scriptDir = QString::fromStdString(dir);
}
void ScriptExecutor::push_script_async(const std::string &script) {
    m_pollTimer->stop();
    if(m_scriptDir.trimmed().isEmpty() || !QDir().mkpath(m_scriptDir)) {
        reportFailure("无法创建脚本目录，请检查目录设置。");
        return;
    }
    m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSaveFile file(QDir(m_scriptDir).filePath("ai_gen.py"));
    const QByteArray data = QByteArray("# -*- coding: utf-8 -*-\n# recvt_request_id: ")
        + m_requestId.toUtf8() + "\n" + QByteArray::fromStdString(script);
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        reportFailure("无法原子写入 ai_gen.py：" + file.errorString());
        return;
    }
    m_elapsed.start();
    m_pollTimer->start();
}
void ScriptExecutor::reportFailure(const QString &error) {
    m_pollTimer->stop();
    ExecuteResult result;
    result.error = error.toStdString();
    emit finished(result);
}
void ScriptExecutor::onPoll() {
    QFile file(QDir(m_scriptDir).filePath("result.json"));
    if(file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(data, &error);
        if(error.error == QJsonParseError::NoError && json.isObject()) {
            const auto obj = json.object();
            if(obj.value("request_id").toString() == m_requestId
               && obj.value("success").isBool()) {
                m_pollTimer->stop();
                ExecuteResult result;
                result.success = obj.value("success").toBool();
                result.output = data.toStdString();
                result.error = obj.value("error").toString().toStdString();
                emit finished(result);
                return;
            }
        }
    }
    if(m_elapsed.elapsed() > 300000)
        reportFailure("5 分钟内未收到当前任务结果。请确认已启动新版 Revit 监听器、双方目录一致。"
                      "任务可能仍在执行，请先检查 Revit；本程序不会自动重新建模。");
}
