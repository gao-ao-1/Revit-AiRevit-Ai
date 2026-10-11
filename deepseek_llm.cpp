#include "deepseek_llm.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QEventLoop>
#include <QUrl>
#include <QSslSocket>

DeepSeekLLM::DeepSeekLLM(QObject *parent)
    : QObject(parent), m_model("deepseek-chat")
{
}

void DeepSeekLLM::setApiKey(const std::string &key)
{
    m_apiKey = key;
}

void DeepSeekLLM::setModel(const std::string &model)
{
    m_model = model;
}

std::string DeepSeekLLM::role_to_str(Role r) const
{
    if(r == Role::System) return "system";
    if(r == Role::Assistant) return "assistant";
    return "user";
}

std::string DeepSeekLLM::extract_python_code(const std::string &fullText) const
{
    QString text = QString::fromStdString(fullText);

    // 1. 优先匹配 ```python ... ```（最可靠）
    QRegularExpression py(R"(```python\s*\n([\s\S]*?)```)");
    auto match = py.match(text);
    if(match.hasMatch())
        return match.captured(1).toStdString();

    // 2. 遍历所有 ``` 代码块，只接受看起来像 Python 的
    QRegularExpression any(R"(```([^\n]*)\n([\s\S]*?)```)");
    auto it = any.globalMatch(text);
    while(it.hasNext()){
        auto m = it.next();
        QString lang = m.captured(1).trimmed().toLower();
        QString block = m.captured(2);

        if(lang == "plaintext" || lang == "text" || lang == "ascii"
           || lang == "html" || lang == "css" || lang == "json"
           || lang == "bash" || lang == "shell" || lang == "plaintext")
            continue;

        if(block.contains("import ") || block.contains("def ")
           || block.contains("from ") || block.contains("class ")
           || block.contains("DB.") || block.contains("revit.")
           || block.contains("Transaction")){
            return block.toStdString();
        }
    }

    // 3. 兜底：没有代码围栏时，整段文本本身像 Python 则直接使用
    QString stripped = text.trimmed();
    if(!stripped.isEmpty() && !stripped.startsWith('<')
       && (stripped.contains("import ") || stripped.contains("def ") || stripped.startsWith('#'))){
        return stripped.toStdString();
    }
    return "";
}

QJsonArray DeepSeekLLM::buildMessages(const std::vector<ChatMessage> &messages) const
{
    QJsonArray arr;
    for(const auto &msg : messages){
        QJsonObject m;
        m["role"] = QString::fromStdString(role_to_str(msg.role));
        m["content"] = QString::fromStdString(msg.content);
        arr.append(m);
    }
    return arr;
}

void DeepSeekLLM::processStreamLine(const QByteArray &line)
{
    QByteArray payload = line.trimmed();
    // DeepSeek/OpenAI 流式返回是 SSE 的 "data:" 前缀
    if(payload.startsWith("data:"))
        payload = payload.mid(5).trimmed();
    if(payload.isEmpty() || payload == "[DONE]") return;

    QJsonDocument d = QJsonDocument::fromJson(payload);
    if(!d.isObject()) return;

    QString content;
    QJsonObject obj = d.object();
    if(obj.contains("error"))
        m_streamError = obj.value("error").toObject().value("message").toString("模型服务返回错误");
    if(obj.contains("message")){            // 兼容 Ollama 形态（备用）
        content = obj["message"].toObject()["content"].toString();
    } else if(obj.contains("choices")){
        QJsonArray choices = obj["choices"].toArray();
        if(!choices.isEmpty()){
            QJsonObject c0 = choices[0].toObject();
            if(c0.value("finish_reason").toString() == "length")
                m_streamError = "模型输出达到长度限制，代码可能不完整，未推送到 Revit。请分阶段描述建筑需求。";
            if(c0.contains("delta"))
                content = c0["delta"].toObject()["content"].toString();
            else if(c0.contains("message"))
                content = c0["message"].toObject()["content"].toString();
        }
    }
    if(content.isEmpty()) return;

    m_accumulated += content;
    emit streamChunk(content);
    if(m_streamCb) m_streamCb(content.toStdString());
}

void DeepSeekLLM::generate_async(const std::vector<ChatMessage> &messages)
{
    if(!QSslSocket::supportsSsl()) {
        GenerateResult result;
        result.error = "Qt 6 无法初始化 TLS。请检查当前 Qt 套件的 TLS 插件及对应运行库，或使用 windeployqt 部署完整运行依赖。";
        emit finished(result);
        return;
    }
    if(m_activeReply){
        QNetworkReply *old = m_activeReply;
        m_activeReply = nullptr;
        old->abort();
        old->deleteLater();
    }
    m_buffer.clear();
    m_accumulated.clear();
    m_streamError.clear();

    QJsonObject reqObj;
    reqObj["model"] = QString::fromStdString(m_model);
    reqObj["stream"] = true;
    reqObj["temperature"] = 0.1;
    reqObj["max_tokens"] = 8192;
    reqObj["messages"] = buildMessages(messages);

    QUrl url(QStringLiteral("https://api.deepseek.com/chat/completions"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(m_apiKey));

    m_activeReply = m_net.post(req, QJsonDocument(reqObj).toJson());

    QNetworkReply *active = m_activeReply;
    connect(active, &QNetworkReply::readyRead, this, [this, active](){
        if(m_activeReply != active) return;
        m_buffer += m_activeReply->readAll();
        int idx;
        while((idx = m_buffer.indexOf('\n')) >= 0){
            QByteArray line = m_buffer.left(idx);
            m_buffer.remove(0, idx + 1);
            processStreamLine(line);
        }
    });

    connect(active, &QNetworkReply::finished, this, [this, active](){
        if(m_activeReply != active) return;
        QNetworkReply *reply = m_activeReply;
        m_activeReply = nullptr;
        finishReply(reply);
    });
}

void DeepSeekLLM::finishReply(QNetworkReply *reply)
{
    if(!m_buffer.isEmpty()){
        processStreamLine(m_buffer);
        m_buffer.clear();
    }

    GenerateResult res{};
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if(reply->error() != QNetworkReply::NoError || httpStatus >= 400){
        res.success = false;
        QString msg;
        if(httpStatus == 401)
            msg = QStringLiteral("DeepSeek API Key 无效（401）。请确认 Key 正确（sk- 开头）且未过期。");
        else if(httpStatus == 402)
            msg = QStringLiteral("DeepSeek 账户余额不足（402）。请到 platform.deepseek.com 充值。");
        else if(httpStatus == 429)
            msg = QStringLiteral("DeepSeek 请求过于频繁/限流（429），请稍后重试。");
        else if(reply->error() == QNetworkReply::OperationCanceledError)
            msg = QStringLiteral("生成已取消");
        else
            msg = QString("DeepSeek 请求失败（HTTP %1）：%2")
                      .arg(httpStatus).arg(reply->errorString());
        res.error = msg.toStdString();
        emit finished(res);
        reply->deleteLater();
        return;
    }

    if(!m_streamError.isEmpty()) {
        res.error = m_streamError.toStdString();
        emit finished(res);
        reply->deleteLater();
        return;
    }
    res.success = true;
    res.text = m_accumulated.toStdString();
    decodeAssistantResponse(res);
    emit finished(res);
    reply->deleteLater();
}

void DeepSeekLLM::cancel()
{
    if(m_activeReply)
        m_activeReply->abort();
}

void DeepSeekLLM::generate_stream(const std::vector<ChatMessage> &messages, SSEChunkCallback on_chunk)
{
    m_streamCb = on_chunk;
    generate_async(messages);
}

GenerateResult DeepSeekLLM::generate(const std::vector<ChatMessage> &messages)
{
    GenerateResult res{};

    QJsonObject reqObj;
    reqObj["model"] = QString::fromStdString(m_model);
    reqObj["stream"] = false;
    reqObj["temperature"] = 0.1;
    reqObj["max_tokens"] = 8192;
    reqObj["messages"] = buildMessages(messages);

    QUrl url(QStringLiteral("https://api.deepseek.com/chat/completions"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(m_apiKey));

    QNetworkReply *reply = m_net.post(req, QJsonDocument(reqObj).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(reply->error() != QNetworkReply::NoError || httpStatus >= 400){
        res.success = false;
        res.error = (httpStatus == 401) ? "DeepSeek API Key 无效（401）"
                    : (httpStatus == 402) ? "DeepSeek 账户余额不足（402）"
                    : "DeepSeek 请求失败（HTTP " + std::to_string(httpStatus) + "）: "
                      + reply->errorString().toStdString();
        reply->deleteLater();
        return res;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if(!doc.isObject()){
        res.success = false;
        res.error = "DeepSeek 返回数据解析失败";
        reply->deleteLater();
        return res;
    }

    QString content;
    QJsonArray choices = doc.object()["choices"].toArray();
    if(!choices.isEmpty())
        content = choices[0].toObject()["message"].toObject()["content"].toString();

    res.success = true;
    res.text = content.toStdString();
    decodeAssistantResponse(res);
    reply->deleteLater();
    return res;
}
