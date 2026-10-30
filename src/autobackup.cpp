// SPDX-License-Identifier: MIT
#include "autobackup.h"

#include "database.h"
#include "demodata.h"
#include "keystore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace AutoBackup {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("AutoBackup", s); }

const char kCounter[] = "backup/autoCounter";     // Änderungszähler bei der letzten Sicherung (in der DB)

// Dateinamen-Präfix je Datenbank, damit Sicherungen verschiedener Datenbanken getrennt bleiben
QString prefix()
{
    QString base = QFileInfo(Database::instance().path()).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("_"));
    return QStringLiteral("VitaParms-Auto_%1_").arg(base.isEmpty() ? QStringLiteral("db") : base);
}
} // namespace

Config config()
{
    QSettings s;
    Config c;
    c.enabled = s.value(QStringLiteral("backup/autoEnabled"), true).toBool();
    c.folder = s.value(QStringLiteral("backup/autoFolder")).toString();
    c.keep = qBound(2, s.value(QStringLiteral("backup/autoKeep"), 7).toInt(), 60);
    return c;
}

void saveConfig(const Config &c)
{
    QSettings s;
    s.setValue(QStringLiteral("backup/autoEnabled"), c.enabled);
    s.setValue(QStringLiteral("backup/autoFolder"), c.folder);
    s.setValue(QStringLiteral("backup/autoKeep"), qBound(2, c.keep, 60));
}

QString defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Sicherungen");
}

QString folder()
{
    const QString f = config().folder.trimmed();
    return f.isEmpty() ? defaultFolder() : f;
}

QList<Entry> list()
{
    QList<Entry> l;
    const QDir dir(folder());
    static const QRegularExpression stamp(QStringLiteral("_(\\d{4}-\\d{2}-\\d{2}_\\d{6})\\.sqlite$"));
    for (const QFileInfo &fi : dir.entryInfoList({ prefix() + QStringLiteral("*.sqlite") }, QDir::Files)) {
        const auto m = stamp.match(fi.fileName());
        if (!m.hasMatch()) continue;
        Entry e;
        e.path = fi.absoluteFilePath();
        e.when = QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd_HHmmss"));
        e.size = fi.size() + QFileInfo(KeyStore::pathFor(e.path)).size();
        l << e;
    }
    std::sort(l.begin(), l.end(), [](const Entry &a, const Entry &b) { return a.when > b.when; });
    return l;
}

QDateTime lastBackup()
{
    const QList<Entry> l = list();
    return l.isEmpty() ? QDateTime() : l.first().when;
}

bool dueAtStart()
{
    if (!config().enabled || DemoData::isActive() || !Database::instance().isOpen()) return false;
    const QDateTime last = lastBackup();
    return !last.isValid() || last.secsTo(QDateTime::currentDateTime()) > 24 * 3600;
}

bool dueAtExit()
{
    if (!config().enabled || DemoData::isActive() || !Database::instance().isOpen()) return false;
    Database &db = Database::instance();
    if (!lastBackup().isValid()) return true;
    return db.setting(QString::fromLatin1(kCounter), QStringLiteral("-1")).toLongLong() != db.syncChangeCounter();
}

bool run(QString *path, QString *error)
{
    Database &db = Database::instance();
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };
    if (!db.isOpen()) return fail(tr("Keine Datenbank geöffnet."));
    const QString dir = folder();
    if (!QDir().mkpath(dir))
        return fail(tr("Der Sicherungsordner %1 ist nicht erreichbar (USB-Stick oder Netzlaufwerk nicht verbunden?).")
                        .arg(QDir::toNativeSeparators(dir)));
    const qint64 counter = db.syncChangeCounter();
    const QString target = QDir(dir).filePath(prefix() + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"))
                                              + QStringLiteral(".sqlite"));
    const QString tmp = target + QStringLiteral(".part");
    QString err;
    if (!db.backupTo(tmp, &err)) {
        QFile::remove(tmp);
        return fail(tr("Die Datenbank konnte nicht gesichert werden: %1").arg(err));
    }
    const QString keysSrc = KeyStore::pathFor(db.path());
    const QString keysDst = KeyStore::pathFor(target);
    QFile::remove(keysDst);
    if (QFile::exists(keysSrc) && !QFile::copy(keysSrc, keysDst)) {
        QFile::remove(tmp);
        return fail(tr("Die Schlüsseldatei konnte nicht nach %1 kopiert werden.").arg(QDir::toNativeSeparators(dir)));
    }
    if (!QFile::rename(tmp, target)) {
        QFile::remove(tmp);
        QFile::remove(keysDst);
        return fail(tr("Die Sicherung konnte nicht gespeichert werden (Ordner %1).").arg(QDir::toNativeSeparators(dir)));
    }
    db.setSetting(QString::fromLatin1(kCounter), QString::number(counter));
    // alte Stände entfernen
    const QList<Entry> all = list();
    const int keep = config().keep;
    for (int i = keep; i < all.size(); ++i) {
        QFile::remove(all.at(i).path);
        QFile::remove(KeyStore::pathFor(all.at(i).path));
    }
    if (path) *path = target;
    return true;
}

QString staleWarning(int days)
{
    if (!config().enabled || DemoData::isActive()) return {};
    const QDateTime last = lastBackup();
    if (last.isValid() && last.daysTo(QDateTime::currentDateTime()) < days) return {};
    return last.isValid()
        ? tr("Die letzte automatische Sicherung ist vom %1 – seit %2 Tagen konnte keine Sicherung erstellt werden.")
              .arg(last.toString(QStringLiteral("dd.MM.yyyy HH:mm"))).arg(last.daysTo(QDateTime::currentDateTime()))
        : tr("Es gibt noch keine automatische Sicherung dieser Datenbank.");
}

} // namespace AutoBackup
