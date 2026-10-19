#pragma once
#include <QObject>
#include <QString>
#include <functional>
QString cadPythonPath();
// Result callback is delivered on the owner's thread; an empty error means success.
void saveCadExport(const QString &source, const QString &destination, bool iges, QObject *owner,
                   std::function<void(const QString &, const QString &)> done);
