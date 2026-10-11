#include "llm_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

bool isConversationOnly(const std::string &input)
{
    QString text = QString::fromStdString(input).trimmed().toLower();
    text.remove(QRegularExpression("[\\s，。！？,.!?]+"));
    static const QStringList greetings = {"你好", "您好", "嗨", "hello", "hi", "hey",
        "谢谢", "谢谢你", "早上好", "晚上好", "下午好", "在吗", "再见"};
    return greetings.contains(text);
}

void decodeAssistantResponse(GenerateResult &result)
{
    result.execute_requested = false;
    result.python_script.clear();
    result.display_text = result.text;
    QString text = QString::fromStdString(result.text).trimmed();
    if(text.startsWith("```json") && text.endsWith("```"))
        text = text.mid(7, text.size() - 10).trimmed();
    const auto doc = QJsonDocument::fromJson(text.toUtf8());
    if(!doc.isObject()) return; // Plain prose/code stays in the chat, never auto-executes.
    const auto obj = doc.object();
    if(obj.value("action").toString() == "chat") {
        result.display_text = obj.value("message").toString().toStdString();
        return;
    }
    if(obj.value("action").toString() != "model") return;
    const QString script = obj.value("script").toString().trimmed();
    if(script.isEmpty()) {
        result.success = false;
        result.error = "AI 返回了建模动作但没有完整脚本，请重新描述需求。";
        return;
    }
    result.execute_requested = true;
    result.python_script = script.toStdString();
    result.display_text = (obj.value("message").toString() + "\n\n" + script).toStdString();
}
