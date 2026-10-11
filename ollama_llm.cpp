#include "ollama_llm.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QEventLoop>
#include <QUrl>

OllamaLLM::OllamaLLM(QObject *parent)
    : QObject(parent), m_model("deepseek-coder:latest"), m_host("127.0.0.1"), m_port(11434)
{
}

void OllamaLLM::setConnection(const std::string &model, const std::string &host, int port)
{
    m_model = model;
    m_host = host;
    m_port = port;
}

std::string OllamaLLM::role_to_str(Role r) const
{
    if(r == Role::System) return "system";
    if(r == Role::Assistant) return "assistant";
    return "user";
}

std::string OllamaLLM::extract_python_code(const std::string &fullText) const
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

        // 明确的非 Python 语言标记，直接跳过
        if(lang == "plaintext" || lang == "text" || lang == "ascii"
           || lang == "html" || lang == "css" || lang == "json"
           || lang == "bash" || lang == "shell" || lang == "plaintext")
            continue;

        // 对块内容做 Python 关键字检测：必须含 import/def/from/class 等才接受
        if(block.contains("import ") || block.contains("def ")
           || block.contains("from ") || block.contains("class ")
           || block.contains("DB.") || block.contains("revit.")
           || block.contains("Transaction")){
            return block.toStdString();
        }
    }

    // 3. 兜底：没有代码围栏时，若整段文本本身像 Python（含 import 且非 HTML），直接使用
    QString stripped = text.trimmed();
    if(!stripped.isEmpty() && !stripped.startsWith('<')
       && (stripped.contains("import ") || stripped.contains("def ") || stripped.startsWith('#'))){
        return stripped.toStdString();
    }
    return "";
}

QJsonArray OllamaLLM::buildMessages(const std::vector<ChatMessage> &messages) const
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

void OllamaLLM::processStreamLine(const QByteArray &line)
{
    QByteArray payload = line.trimmed();
    // Ollama 流式返回的是 NDJSON（每行一个 JSON 对象），部分封装可能是 SSE 的 "data:" 前缀
    if(payload.startsWith("data:"))
        payload = payload.mid(5).trimmed();
    if(payload.isEmpty() || payload == "[DONE]") return;

    QJsonDocument d = QJsonDocument::fromJson(payload);
    if(!d.isObject()) return;
    if(d.object().contains("error"))
        m_streamError = d.object().value("error").toString();
    if(d.object().value("done_reason").toString() == "length")
        m_streamError = "模型输出达到长度限制，代码可能不完整，未推送到 Revit。请分阶段描述建筑需求。";
    QString content = d.object().value("message").toObject().value("content").toString();
    if(content.isEmpty()) return;

    m_accumulated += content;
    emit streamChunk(content);
    if(m_streamCb) m_streamCb(content.toStdString());
}

void OllamaLLM::generate_async(const std::vector<ChatMessage> &messages)
{
    // 若有正在进行的请求，先取消（避免并发与野指针）
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
    reqObj["options"] = QJsonObject{{"temperature", 0.1}, {"num_ctx", 16384}, {"num_predict", 8192}};  // 代码生成用低温度，但 0 会让某些模型卡住
    reqObj["messages"] = buildMessages(messages);

    QUrl url(QString("http://%1:%2/api/chat").arg(QString::fromStdString(m_host)).arg(m_port));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

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

void OllamaLLM::finishReply(QNetworkReply *reply)
{
    // 处理可能残留的最后一个 SSE 行
    if(!m_buffer.isEmpty()){
        processStreamLine(m_buffer);
        m_buffer.clear();
    }

    GenerateResult res{};
    if(reply->error() != QNetworkReply::NoError){
        res.success = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(httpStatus == 404){
            res.error = "模型不存在（404）：模型框里的名字必须与 `ollama list` 输出完全一致，"
                        "例如 qwen2.5-coder:7b-instruct-q4_K_M";
        }else if(reply->error() == QNetworkReply::OperationCanceledError){
            res.error = "生成已取消";
        }else{
            res.error = "Ollama 连接错误: " + reply->errorString().toStdString();
        }
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

void OllamaLLM::cancel()
{
    if(m_activeReply)
        m_activeReply->abort();
}

void OllamaLLM::generate_stream(const std::vector<ChatMessage> &messages, SSEChunkCallback on_chunk)
{
    m_streamCb = on_chunk;
    generate_async(messages);
}

GenerateResult OllamaLLM::generate(const std::vector<ChatMessage> &messages)
{
    GenerateResult res{};

    QJsonObject reqObj;
    reqObj["model"] = QString::fromStdString(m_model);
    reqObj["stream"] = false;
    reqObj["options"] = QJsonObject{{"temperature", 0.1}, {"num_ctx", 16384}, {"num_predict", 8192}};  // 代码生成用低温度，但 0 会让某些模型卡住
    reqObj["messages"] = buildMessages(messages);

    QUrl url(QString("http://%1:%2/api/chat").arg(QString::fromStdString(m_host)).arg(m_port));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_net.post(req, QJsonDocument(reqObj).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if(reply->error() != QNetworkReply::NoError){
        res.success = false;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        res.error = (httpStatus == 404)
                    ? "模型不存在（404）：模型框里的名字必须与 `ollama list` 输出完全一致"
                    : "Ollama 连接错误: " + reply->errorString().toStdString();
        reply->deleteLater();
        return res;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if(!doc.isObject()){
        res.success = false;
        res.error = "Ollama 返回数据解析失败";
        reply->deleteLater();
        return res;
    }

    QString content = doc.object().value("message").toObject().value("content").toString();
    res.success = true;
    res.text = content.toStdString();
    decodeAssistantResponse(res);
    reply->deleteLater();
    return res;
}
