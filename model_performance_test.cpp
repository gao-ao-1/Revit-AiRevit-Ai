#include "model_scene.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    QFile file(directory.filePath("grid.obj"));
    if(!file.open(QIODevice::WriteOnly)) return 1;
    QByteArray data("o grid\n");
    constexpr int size = 100;
    for(int y=0;y<=size;++y) for(int x=0;x<=size;++x)
        data += "v " + QByteArray::number(x) + " " + QByteArray::number(y) + " 0\n";
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        const int a=y*(size+1)+x+1, b=a+1, c=a+size+1, d=c+1;
        data += "f "+QByteArray::number(a)+" "+QByteArray::number(b)+" "+QByteArray::number(d)+"\n";
        data += "f "+QByteArray::number(a)+" "+QByteArray::number(d)+" "+QByteArray::number(c)+"\n";
    }
    if(file.write(data)!=data.size()) return 1;
    file.close();
    ModelScene scene; QString error; QElapsedTimer timer;timer.start();
    if(!scene.load(file.fileName(), {}, error)) {std::cerr<<error.toStdString();return 1;}
    const auto loadMs=timer.elapsed();timer.restart();
    for(int i=0;i<400;++i)
        if(scene.pick(QVector3D(float(i%97)+.25f,10,-float(i%89)-.25f), QVector3D(0,-1,0))!=0) return 2;
    const auto pickUs=timer.nsecsElapsed()/1000;
    scene.elements[0].visible=false;
    if(scene.pick(QVector3D(.25f,10,-.25f),QVector3D(0,-1,0))!=-1) return 3;
    std::cout << "triangles=" << scene.triangleCount << " load_ms=" << loadMs << " picks=400 pick_us=" << pickUs << '\n';
    return 0;
}
