#pragma once
#include <QJsonArray>
#include <QString>

class ErrorGuard {
public:
    void load(const QString &directory);
    bool record(const QString &error, const QString &script = QString(), bool failedModel = false);
    QString prompt() const;
    bool check(const QString &script, QString &issue) const;
    QString lastStorageError() const { return m_storageError; }
    static QString fingerprint(const QString &script);
    static QString summary(const QString &error);
    static QString advice(const QString &error);
private:
    bool save();
    QString m_path;
    QString m_storageError;
    QJsonArray m_records;
    QJsonArray m_failedScripts;
};
