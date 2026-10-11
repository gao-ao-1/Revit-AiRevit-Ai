#include "script_executor.h"
#include "llm_client.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if(!condition) throw std::runtime_error(message);
}
static void wait(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static QByteArray read(const QString &path) {
    QFile f(path); require(f.open(QIODevice::ReadOnly), "read failed"); return f.readAll();
}
static void write(const QString &path, const QByteArray &data) {
    QFile f(path); require(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "write failed");
    require(f.write(data) == data.size(), "short write");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(isConversationOnly("你好！"), "greeting was not protected");
        require(!isConversationOnly("你好，请创建一面墙"), "greeting plus command incorrectly blocked");
        GenerateResult chat;
        chat.success = true;
        chat.text = R"({"action":"chat","message":"你好，你想聊什么？"})";
        decodeAssistantResponse(chat);
        require(!chat.execute_requested && chat.python_script.empty(), "chat became an execution");
        require(chat.display_text == "你好，你想聊什么？", "chat display leaked JSON");
        chat.text = "Here is an example:\n```python\nfrom pyrevit import revit, DB\n```";
        decodeAssistantResponse(chat);
        require(!chat.execute_requested && chat.python_script.empty(), "code explanation became an execution");
        chat.text = R"json({"action":"chat","message":"示例：```python print(1)```","script":"print(1)"})json";
        decodeAssistantResponse(chat);
        require(!chat.execute_requested && chat.python_script.empty(), "chat script field became an execution");
        chat.text = R"({"action":"model","message":"创建墙","script":"from pyrevit import revit, DB\n"})";
        decodeAssistantResponse(chat);
        require(chat.execute_requested && !chat.python_script.empty(), "explicit model action was lost");
        chat.text = R"({"action":"model","script":""})";
        decodeAssistantResponse(chat);
        require(!chat.success && !chat.execute_requested, "empty model script accepted");
        chat.text = "{\"action\":\"model\",\"script\":\"partial";
        decodeAssistantResponse(chat);
        require(!chat.execute_requested, "partial JSON executed");
        SessionStore sessions;
        sessions.append_msg(1, {Role::User, "two floors with windows"});
        auto messages = sessions.get_or_create(1);
        require(messages.size() == 2 && messages.front().role == Role::System, "first request lost system prompt");
        require(QString::fromStdString(messages.front().content).contains(QStringLiteral("墙的 Location 是 LocationCurve")), "helper guard prompt missing or encoding broken");
        sessions.sessions.clear();
        sessions.append_msg(1, {Role::User, "new session"});
        require(sessions.get_or_create(1).front().role == Role::System, "new session lost system prompt");
        QTemporaryDir temp;
        require(temp.isValid(), "temp dir failed");
        const auto resultPath = QDir(temp.path()).filePath("result.json");
        ScriptExecutor executor;
        executor.setScriptDir(temp.path().toStdString());
        int calls = 0;
        ExecuteResult result;
        QObject::connect(&executor, &ScriptExecutor::finished, [&](const ExecuteResult &r){ ++calls; result = r; });
        executor.push_script_async("print('hello')\n");
        const auto lines = read(QDir(temp.path()).filePath("ai_gen.py")).split('\n');
        require(lines[0].contains("coding: utf-8"), "missing encoding");
        const auto id = QString::fromUtf8(lines[1].mid(QByteArray("# recvt_request_id: ").size()));
        require(!id.isEmpty(), "missing task id");
        write(resultPath, "{\"success\":true,\"request_id\":\"stale\"}");
        wait(450); require(calls == 0, "accepted stale result");
        write(resultPath, "{\"success\":");
        wait(450); require(calls == 0, "accepted partial JSON");
        write(resultPath, QJsonDocument(QJsonObject{{"request_id", id}, {"success", true},
              {"ifc_success", false}, {"mesh_success", true}}).toJson());
        wait(450); require(calls == 1 && result.success, "export failure incorrectly failed model");
        executor.push_script_async("# recvt_export_only\n");
        wait(450); require(calls == 1, "new task accepted old result");
        const auto newLines = read(QDir(temp.path()).filePath("ai_gen.py")).split('\n');
        const auto newId = QString::fromUtf8(newLines[1].mid(QByteArray("# recvt_request_id: ").size()));
        require(newId != id, "task id reused");
        write(resultPath, QJsonDocument(QJsonObject{{"request_id", newId}, {"success", false},
              {"error", "model failure"}, {"stage", "model"}, {"rolled_back", true}}).toJson());
        wait(450); require(calls == 2 && !result.success && !result.error.empty(), "model failure lost");
        std::cout << "session and file protocol checks passed\n";
    } catch(const std::exception &e) {
        std::cerr << e.what() << '\n'; return 1;
    }
    return 0;
}
