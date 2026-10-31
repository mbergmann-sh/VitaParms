// VitaParms – Datenbank von VitaParms light übernehmen (siehe lightimport.h)
// SPDX-License-Identifier: MIT
#include "lightimport.h"

#include "autobackup.h"
#include "database.h"
#include "dbsync.h"
#include "sqlquery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace LightImport {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("LightImport", s); }
}

QString lightDataDir()
{
    // <Programmdaten>/<Organisation>/<Programm>: neben "VitaParms" liegt "VitaParmsLight"
    QDir d(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    d.cdUp();
    return d.filePath(QStringLiteral("VitaParmsLight"));
}

QString lightDatabasePath()
{
    return QDir(lightDataDir()).filePath(QStringLiteral("vitaparms.sqlite"));
}

bool importFrom(const QString &path, const QByteArray &key, Result *result, QString *error)
{
    Database &db = Database::instance();
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };
    if (!db.isOpen()) return fail(tr("Keine Datenbank geöffnet."));
    if (QFileInfo(path).canonicalFilePath() == QFileInfo(db.path()).canonicalFilePath())
        return fail(tr("Das ist die geöffnete Datenbank selbst."));

    // Schlüssel prüfen, bevor etwas geschieht
    {
        SqlDb d = db.handleDb();
        SqlQuery a(d);
        a.prepare(QStringLiteral("ATTACH DATABASE ? AS lightcheck KEY ?"));
        a.addBindValue(path);
        a.addBindValue(QString::fromLatin1(key));
        bool ok = a.exec();
        if (ok) {
            SqlQuery c(d);
            ok = c.exec(QStringLiteral("SELECT count(*) FROM lightcheck.sqlite_master")) && c.next()
                 && Database::schemaVersionOf(d, QStringLiteral("lightcheck")) > 0;
        }
        d.exec(QStringLiteral("DETACH DATABASE lightcheck"));
        if (!ok) return fail(tr("Die Datenbank lässt sich mit diesem Konto bzw. Schlüssel nicht öffnen."));
    }

    Result r;
    // Sicherung der geöffneten Datenbank (wie die automatische Sicherung, auch wenn diese ausgeschaltet ist)
    QString backupErr;
    if (!AutoBackup::run(&r.backup, &backupErr))
        return fail(tr("Vor dem Import konnte keine Sicherung angelegt werden – der Import wurde nicht ausgeführt.\n%1")
                        .arg(backupErr));

    QSet<qint64> before;
    for (const Patient &p : db.patients()) before.insert(p.id);

    DbMerge::Stats st;
    QString err;
    if (!DbMerge::mergeFrom(path, key, &st, &err)) return fail(err);
    r.inserted = st.inserted;
    r.updated = st.updated;
    for (const Patient &p : db.patients())
        if (!before.contains(p.id)) { r.persons << p.displayName(); r.personIds << p.id; }
    if (result) *result = r;
    return true;
}

} // namespace LightImport
