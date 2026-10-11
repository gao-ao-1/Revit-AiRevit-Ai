#include "error_guard.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
static void require(bool ok, const char *message) { if(!ok) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        ErrorGuard guard; guard.load(temp.path());
        const QString code = "from pyrevit import revit, DB\nx = 1\n";
        const QString err = "Traceback:\n  File D:/old/ai.py, line 12\nAttributeError: 'LocationCurve' object has no attribute 'Point'";
        require(guard.record(err, code, true), "first error not recorded");
        require(!guard.record(QString(err).replace("old", "new"), code, true), "trace path prevented deduplication");
        ErrorGuard restarted; restarted.load(temp.path());
        QString issue;
        require(!restarted.check("# new comment\n" + code + "\n", issue), "failed script not persisted");
        require(restarted.check(QString(code).replace("x = 1", "x = 2"), issue), "changed script blocked");
        require(restarted.prompt().contains("累计 2 次") && restarted.prompt().contains("building.opening_at"), "count or fix missing");
        require(!restarted.check("building.level(doc, None, None)", issue), "invalid level passed");
        require(!restarted.check("DB.ElementTypeId.Wall", issue), "invented API passed");
        require(!restarted.check("scope.StartNewStairs(a.Id, b.Id)", issue), "invented stairs API passed");
        require(ErrorGuard::advice("AttributeError: StartNewStairs").contains("stairs_u"), "stairs repair advice missing");
        require(!restarted.check("doc.Create.NewFootPrintRoof(curves, level, kind, None)", issue), "None roof mapping passed");
        require(restarted.check("# DB.ElementTypeId.Wall\nx = 3", issue), "comment triggered API rule");
        const QString retry = "x = 9";
        restarted.record("Network timeout", retry, false);
        require(restarted.check(retry, issue), "network failure blacklisted script");
        require(ErrorGuard::fingerprint("x='a b'") != ErrorGuard::fingerprint("x='ab'"), "string spaces lost");
        QTemporaryDir migration;
        QFile old(migration.filePath("ai_error_memory_v3.json"));
        require(old.open(QIODevice::WriteOnly), "legacy write failed");
        old.write("[\"AttributeError: Name\"]"); old.close();
        ErrorGuard migrated; migrated.load(migration.path());
        require(migrated.prompt().contains("building.element_name"), "legacy migration lost fix");
        QTemporaryDir corrupt;
        QFile broken(corrupt.filePath("ai_error_memory_v4.json"));
        broken.open(QIODevice::WriteOnly); broken.write("{broken"); broken.close();
        ErrorGuard damaged; damaged.load(corrupt.path()); damaged.record(err);
        require(!damaged.lastStorageError().isEmpty(), "corrupt file not reported");
        broken.open(QIODevice::ReadOnly); require(broken.readAll() == "{broken", "corrupt history overwritten");
        std::cout << "Error memory persistence, deduplication, guards and migration OK\n";
        return 0;
    } catch(const std::exception &error) { std::cerr << error.what(); return 1; }
}
