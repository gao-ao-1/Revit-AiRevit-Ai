#include <QApplication>
#include <QFont>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle("Fusion");
    QApplication::setApplicationName("Revit-AI 控制台");

    QFont f("Microsoft YaHei UI", 10);
    QApplication::setFont(f);

    a.setStyleSheet(R"(
        QMainWindow, QWidget#central { background:#edf1f7; }
        QWidget#headerCard { background:#17253e; border-radius:14px; }
        QWidget#headerCard QLabel { color:#c1cee3; }
        QWidget#headerCard QLabel#brand { color:#ffffff; }
        QLabel#logo { background:#3b75f5; color:white; border-radius:10px; padding:8px 14px; font-weight:700; }
        QWidget#headerCard QPushButton { color:#e4edff; background:#263752; border:1px solid #3a4c68; }
        QWidget#headerCard QPushButton:hover { background:#344c72; border-color:#5c80b9; }
        QWidget#headerCard QLabel#statusBadge { background:#224d43; color:#c0efd8; }
        QWidget#settingsCard, QWidget#chatCard, QWidget#previewCard {
            background:#ffffff; border:1px solid #dce3ed; border-radius:14px;
        }
        QLabel { color:#56657a; background:transparent; border:none; }
        QLabel#brand, QLabel#sectionTitle { color:#172b4d; font-weight:600; }
        QLabel#statusBadge { background:#e8f4ef; border-radius:12px; padding:7px 14px; }
        QSplitter::handle { background:transparent; width:10px; }
        QSplitter::handle:hover { background:#d3dff2; border-radius:4px; }
        QComboBox { background:white; border:1px solid #dfe5ee; border-radius:6px; padding:7px 10px; }
        QPushButton { background:#f7f9fc; color:#334766; border:1px solid #dfe5ee;
            border-radius:7px; padding:8px 14px; }
        QPushButton:hover { background:#eaf0fb; border-color:#b6c9ec; }
        QPushButton:disabled { color:#a8b2c0; background:#f4f6f9; }
        QTextEdit {
            background:#ffffff;
            border:1px solid #d9dee6;
            border-radius:8px;
            padding:10px;
            color:#22252a;
            selection-background-color:#2d6cdf;
        }
        QTextEdit#conversation { background:#ffffff; border:none; padding:12px; }
        QWidget#composer { background:#f6f8fc; border:1px solid #dce5f2; border-radius:12px; }
        QTextEdit#messageInput { background:transparent; border:none; padding:6px; }
        QLabel#muted { color:#8592a6; }
        QLabel#memoryBadge { background:#eff5ff; color:#4c6faa; border-radius:8px; padding:4px 9px; }
        QPushButton#promptChip { background:#f1f5fc; color:#476185; border:none; padding:6px 10px; border-radius:8px; }
        QPushButton#promptChip:hover { background:#e2ebfc; color:#245fd0; }
        QPushButton#sendBtn {
            background:#346ff0; color:white; border:none; border-radius:9px;
            padding:9px 26px; font-weight:600;
        }
        QPushButton#sendBtn:hover { background:#2459c2; }
        QPushButton#sendBtn:pressed { background:#1d4aa3; }
        QPushButton#sendBtn:disabled { background:#b9c4d4; color:#f0f2f6; }
        QPushButton#stopBtn {
            background:#ffffff; color:#e5484d; border:1px solid #e5484d; border-radius:6px;
            padding:8px 18px;
        }
        QPushButton#stopBtn:hover { background:#fdecec; }
        QPushButton#stopBtn:disabled { color:#c9c2c2; border-color:#dcd2d2; background:#fafafa; }
        QLineEdit, QSpinBox {
            background:#ffffff; border:1px solid #d9dee6; border-radius:6px;
            padding:7px 10px;
        }
        QLineEdit:focus, QSpinBox:focus { border-color:#2d6cdf; }
        QScrollBar:vertical { background:transparent; width:9px; }
        QScrollBar::handle:vertical { background:#cbd5e2; border-radius:4px; min-height:24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QStatusBar { background:#eef1f5; color:#5a6472; }
    )");

    MainWindow w;
    const auto args = a.arguments();
    w.show();
    if(args.contains("--settings-preview")) {
        QTimer::singleShot(100, &w, [&w, args](){
            if(args.contains("--deepseek-preview"))
                w.findChild<QComboBox *>("providerCombo")->setCurrentIndex(1);
            w.findChild<QPushButton *>("connectionSettingsButton")->click();
        });
    }
    if(args.contains("--large-preview"))
        QTimer::singleShot(100, &w, [&w](){ w.resize(1920, 1080); });
    const int capture = args.indexOf("--screenshot");
    if(capture >= 0 && capture + 1 < args.size()) {
        QTimer::singleShot(1500, &w, [&w, &a, args, capture](){
            w.grab().save(args[capture + 1]);
            QFile metrics(args[capture + 1] + ".txt");
            if(metrics.open(QIODevice::WriteOnly))
                metrics.write(QString("%1x%2 font=%3").arg(w.width()).arg(w.height()).arg(w.font().pointSizeF()).toUtf8());
            qInfo() << "Window" << w.size() << "font points" << w.font().pointSizeF();
            a.quit();
        });
    }
    return a.exec();
}
