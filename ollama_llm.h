#pragma once
#include "llm_client.h"
#include <string>
#include <vector>
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QByteArray>
#include <QString>

class QNetworkReply;

class OllamaLLM : public QObject, public LLMClient
{
    Q_OBJECT
public:
    explicit OllamaLLM(QObject *parent = nullptr);

    void setConnection(const std::string &model, const std::string &host, int port);

    // 阻塞式（保留兼容，UI 已改用 generate_async）
    GenerateResult generate(const std::vector<ChatMessage>& messages) override;
    // 流式回调（保留兼容）
    void generate_stream(const std::vector<ChatMessage>& messages, SSEChunkCallback on_chunk) override;

    // 异步 + 流式：逐块 emit streamChunk，最终 emit finished
    void generate_async(const std::vector<ChatMessage>& messages);
    void cancel();

signals:
    void finished(GenerateResult res);
    void streamChunk(const QString &chunk);

private:
    std::string m_model;
    std::string m_host;
    int m_port = 11434;

    QNetworkAccessManager m_net;
    QNetworkReply* m_activeReply = nullptr;
    QByteArray m_buffer;
    QString m_accumulated;
    QString m_streamError;
    SSEChunkCallback m_streamCb;

    std::string role_to_str(Role r) const;
    std::string extract_python_code(const std::string &fullText) const;
    QJsonArray buildMessages(const std::vector<ChatMessage> &messages) const;
    void processStreamLine(const QByteArray &line);
    void finishReply(QNetworkReply *reply);
};
