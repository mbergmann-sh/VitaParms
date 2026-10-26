// SPDX-License-Identifier: MIT
#include "dbtransfer.h"
#include "database.h"
#include "keystore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString tr(const char *s) { return QCoreApplication::translate("DbTransfer", s); }
const QByteArray kMagic = QByteArrayLiteral("VITAPARMS-DB-BUNDLE\n");
const QByteArray kSqlitePlain = QByteArrayLiteral("SQLite format 3");

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error) *error = tr("Die Datei %1 lässt sich nicht schreiben: %2").arg(QDir::toNativeSeparators(path), f.errorString());
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool validKeys(const QByteArray &keys)
{
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(keys, &pe).object();
    return pe.error == QJsonParseError::NoError && o.value(QStringLiteral("format")).toInt() == 1;
}

// Bundle: Kennung, eine Zeile JSON {"db": n, "keys": m}, dann die Bytes
bool unpack(const QByteArray &all, QByteArray *db, QByteArray *keys)
{
    if (!all.startsWith(kMagic)) return false;
    const int nl = all.indexOf('\n', kMagic.size());
    if (nl < 0) return false;
    const QJsonObject h = QJsonDocument::fromJson(all.mid(kMagic.size(), nl - kMagic.size())).object();
    const qint64 n = h.value(QStringLiteral("db")).toVariant().toLongLong(), m = h.value(QStringLiteral("keys")).toVariant().toLongLong();
    if (n <= 0 || m < 0 || nl + 1 + n + m != all.size()) return false;
    *db = all.mid(nl + 1, n);
    *keys = all.mid(nl + 1 + n, m);
    return true;
}
} // namespace

namespace DbTransfer {

QString bundleSuffix() { return QStringLiteral("vpdb"); }

bool readFile(const QString &pathOrUrl, QByteArray *data, QString *error)
{
    const QUrl u(pathOrUrl);
    // file:// → lokaler Pfad; content:// (Android) öffnet QFile direkt
    QFile f(u.isLocalFile() ? u.toLocalFile() : pathOrUrl);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("Die Datei lässt sich nicht öffnen: %1").arg(f.errorString());
        return false;
    }
    *data = f.readAll();
    return true;
}

bool isBundle(const QString &path)
{
    QFile f(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path);
    return f.open(QIODevice::ReadOnly) && f.read(kMagic.size()) == kMagic;
}

bool exportBundle(const QString &target, QString *error)
{
    Database &db = Database::instance();
    const QString tmp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("vitaparms-export-%1.sqlite").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!db.backupTo(tmp, error)) return false;
    QByteArray dbBytes, keys;
    const bool ok = readFile(tmp, &dbBytes, error);
    QFile::remove(tmp);
    if (!ok) return false;
    QFile kf(KeyStore::pathFor(db.path()));
    if (kf.open(QIODevice::ReadOnly)) keys = kf.readAll();
    const QByteArray head = QJsonDocument(QJsonObject{ { QStringLiteral("db"), qint64(dbBytes.size()) },
                                                       { QStringLiteral("keys"), qint64(keys.size()) },
                                                       { QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate) } })
                                .toJson(QJsonDocument::Compact);
    return writeFile(target, kMagic + head + '\n' + dbBytes + keys, error);
}

bool importDatabase(const QString &source, const QString &keysSource, const QString &target,
                    QString *safetyCopy, bool *hasKeys, QString *error)
{
    if (safetyCopy) safetyCopy->clear();
    QByteArray all;
    if (!readFile(source, &all, error)) return false;
    QByteArray dbBytes, keys;
    if (all.startsWith(kMagic)) {
        if (!unpack(all, &dbBytes, &keys)) {
            if (error) *error = tr("Die Übertragungsdatei ist beschädigt oder unvollständig.");
            return false;
        }
    } else {
        dbBytes = all;
        QString ks = keysSource;
        if (ks.isEmpty()) {   // .keys neben der Datenbank
            const QUrl u(source);
            const QString local = u.isLocalFile() ? u.toLocalFile() : source;
            if (QFile::exists(KeyStore::pathFor(local))) ks = KeyStore::pathFor(local);
        }
        if (!ks.isEmpty() && !readFile(ks, &keys, error)) return false;
    }
    all.clear();
    // Plausibel? Eine SQLite-Datei (unverschlüsselt aus alter Version) oder verschlüsselt (zufällige Bytes, Seitengröße)
    if (dbBytes.size() < 4096 || (dbBytes.size() % 512) != 0) {
        if (error) *error = tr("Das ist keine VitaParms-Datenbank.");
        return false;
    }
    if (!keys.isEmpty() && !validKeys(keys)) {
        if (error) *error = tr("Die Schlüsseldatei (.keys) ist beschädigt oder gehört nicht zu VitaParms.");
        return false;
    }
    if (!keys.isEmpty() && dbBytes.startsWith(kSqlitePlain)) keys.clear();   // unverschlüsselt: Konten entstehen neu

    if (QFileInfo(Database::instance().path()).absoluteFilePath() == QFileInfo(target).absoluteFilePath())
        Database::instance().close();   // die laufende Datenbank wird ersetzt
    QDir().mkpath(QFileInfo(target).absolutePath());
    // bisherige Datenbank aufbewahren
    if (Database::fileHasContent(target)) {
        const QString safety = target + QStringLiteral(".vor-import-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::rename(target, safety)) {
            if (error) *error = tr("Die bisherige Datenbank lässt sich nicht beiseitelegen.");
            return false;
        }
        if (QFile::exists(KeyStore::pathFor(target))) QFile::rename(KeyStore::pathFor(target), KeyStore::pathFor(safety));
        if (safetyCopy) *safetyCopy = safety;
    }
    for (const char *suffix : { "-wal", "-shm", "-journal", ".syncstate" }) QFile::remove(target + QLatin1String(suffix));
    QFile::remove(KeyStore::pathFor(target));
    if (!writeFile(target, dbBytes, error)) return false;
    if (!keys.isEmpty() && !writeFile(KeyStore::pathFor(target), keys, error)) return false;
    if (hasKeys) *hasKeys = !keys.isEmpty();
    return true;
}

} // namespace DbTransfer
