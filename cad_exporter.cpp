#include "cad_exporter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QProcess>
#include <QTimer>
#include <memory>

QString cadPythonPath() {
    const QString configured=qEnvironmentVariable("RECVT_CAD_PYTHON");
    if(!configured.isEmpty() && QFileInfo::exists(configured)) return configured;
    QDir directory(QCoreApplication::applicationDirPath());
    for(int i=0;i<5;++i) {
        const QString path=directory.filePath(".cad-venv/Scripts/python.exe");
        if(QFileInfo::exists(path)) return path;
        if(!directory.cdUp()) break;
    }
    return {};
}

void saveCadExport(const QString &source,const QString &destination,bool iges,QObject *owner,
                   std::function<void(const QString &,const QString &)> done) {
    if(!iges) {
        auto *watcher=new QFutureWatcher<QString>(owner);
        QObject::connect(watcher,&QFutureWatcher<QString>::finished,owner,[watcher,done](){
            const QString error=watcher->result();watcher->deleteLater();done(error,{});
        });
        watcher->setFuture(QtConcurrent::run([source,destination]()->QString{
            QFile input(source);QSaveFile output(destination);
            if(!input.open(QIODevice::ReadOnly))return input.errorString();
            if(!output.open(QIODevice::WriteOnly))return output.errorString();
            while(!input.atEnd()) {
                const auto chunk=input.read(1024*1024);
                if(input.error()!=QFileDevice::NoError)return input.errorString();
                if(output.write(chunk)!=chunk.size())return output.errorString();
            }
            return output.commit()?QString():output.errorString();
        }));
        return;
    }
    const QString python=cadPythonPath(), script=QDir(QCoreApplication::applicationDirPath()).filePath("cad_converter.py");
    if(python.isEmpty() || !QFileInfo::exists(script)) {done(QStringLiteral("IGES 转换组件未安装。请使用 Python 3.11 运行程序目录的 setup_iges.cmd，或先导出 STEP。"),{});return;}
    auto *process=new QProcess(owner);auto *timer=new QTimer(process);timer->setSingleShot(true);
    auto finished=std::make_shared<bool>(false);auto output=std::make_shared<QByteArray>();
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto collect=[process,output](){output->append(process->readAll());if(output->size()>32768)*output=output->right(32768);};
    QObject::connect(process,&QProcess::readyRead,owner,collect);
    auto finish=[process,timer,finished,collect,output,done](const QString &error){
        if(*finished)return;*finished=true;timer->stop();collect();
        done(error,QString::fromUtf8(*output));process->deleteLater();
    };
    QObject::connect(process,&QProcess::errorOccurred,owner,[finish,process](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)finish(process->errorString());
    });
    QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[finish,process](int code,QProcess::ExitStatus status){
        finish(code==0 && status==QProcess::NormalExit?QString():QStringLiteral("IGES 转换失败；原始 STEP 文件仍保留。")+process->errorString());
    });
    QObject::connect(timer,&QTimer::timeout,owner,[process,finish](){process->kill();finish(QStringLiteral("IGES 转换超过 10 分钟，已停止转换；可以使用原始 STEP 文件。"));});
    process->start(python,{script,source,destination});timer->start(600000);
}
