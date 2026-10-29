// SPDX-License-Identifier: MIT
#include "dbsync.h"
#include "database.h"
#include "docfiles.h"
#include "keystore.h"
#include "session.h"
#include "sqlquery.h"
#include "websync.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>

namespace {
const char *kEnabled = "sync/enabled", *kAuto = "sync/auto", *kRev = "sync/rev", *kBaseline = "sync/baseline",
           *kKeysHash = "sync/keysHash", *kLast = "sync/last", *kResult = "sync/result";
QString s(const char *k) { return QString::fromLatin1(k); }
QString tr(const char *t) { return QCoreApplication::translate("DbSync", t); }

QStringList columns(const SqlDb &d, const QString &schema, const QString &table)
{
    QStringList l;
    SqlQuery q(d);
    if (q.exec(QStringLiteral("PRAGMA %1.table_info(%2)").arg(schema, table)))
        while (q.next()) l << q.value(1).toString();
    return l;
}

qint64 scalar(const SqlDb &d, const QString &sql, const QVariantList &binds = {})
{
    SqlQuery q(d);
    q.prepare(sql);
    for (const QVariant &b : binds) q.addBindValue(b);
    if (q.exec() && q.next()) return q.value(0).toLongLong();
    return -1;
}

QString fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}
} // namespace

// ====================================================================== Zusammenführen

namespace DbMerge {

namespace {
// Fremdschlüssel einer Tabelle: Spalte → Elterntabelle
QList<QPair<QString, QString>> foreignKeys(const Database::SyncTable &t)
{
    QList<QPair<QString, QString>> fk;
    if (t.parentColumn) fk << qMakePair(QString::fromLatin1(t.parentColumn), QStringLiteral("patients"));
    if (qstrcmp(t.name, "appointments") == 0) fk << qMakePair(QStringLiteral("series_id"), QStringLiteral("appointment_series"));
    if (qstrcmp(t.name, "med_stock") == 0 || qstrcmp(t.name, "med_stock_moves") == 0 || qstrcmp(t.name, "med_order_items") == 0)
        fk << qMakePair(QStringLiteral("medication_id"), QStringLiteral("medications"));
    if (qstrcmp(t.name, "med_stock_moves") == 0) fk << qMakePair(QStringLiteral("stock_id"), QStringLiteral("med_stock"));
    if (qstrcmp(t.name, "med_order_items") == 0) fk << qMakePair(QStringLiteral("order_id"), QStringLiteral("med_orders"));
    if (qstrcmp(t.name, "wound_entries") == 0) fk << qMakePair(QStringLiteral("wound_id"), QStringLiteral("wounds"));
    return fk;
}

QString mapExpr(const QString &parent, const QString &remoteValue)
{
    return QStringLiteral("(SELECT lp.id FROM main.%1 lp JOIN r.%1 rp ON rp.uid = lp.uid WHERE rp.id = %2)")
        .arg(parent, remoteValue);
}

// IDs in JSON-Feldern (medications.extra, appointments.extra) auf die lokalen IDs umsetzen
bool remapJson(const SqlDb &d, const QString &table, const QList<QPair<QString, QString>> &refs, QString *error)
{
    struct Row { qint64 id; QJsonObject extra; };
    QList<Row> rows;
    {
        SqlQuery q(d);
        if (!q.exec(QStringLiteral("SELECT l.id, l.extra FROM main.%1 l JOIN temp._chg c ON c.uid = l.uid AND c.tbl = '%1'"
                                   " WHERE l.extra IS NOT NULL AND l.extra <> ''").arg(table))) {
            if (error) *error = q.lastError().text();
            return false;
        }
        while (q.next())
            rows << Row{ q.value(0).toLongLong(), QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object() };
    }
    for (Row &row : rows) {
        bool changed = false;
        for (const auto &ref : refs) {
            const auto &key = ref.first;
            const auto &parent = ref.second;      // eigene Variable: Lambdas dürfen in C++17 keine structured bindings fangen
            if (!row.extra.contains(key)) continue;
            auto mapId = [&](const QJsonValue &v) {
                return scalar(d, mapExpr(parent, QStringLiteral("?")), { v.toVariant().toLongLong() });
            };
            if (row.extra.value(key).isArray()) {          // z. B. apptSeriesIds: eine Serie je Einnahmezeit
                QJsonArray mapped;
                for (const QJsonValue &v : row.extra.value(key).toArray())
                    if (const qint64 local = mapId(v); local > 0) mapped << local;
                if (mapped.isEmpty()) row.extra.remove(key);
                else row.extra[key] = mapped;
            } else {
                const qint64 local = mapId(row.extra.value(key));
                if (local > 0) row.extra[key] = local;
                else row.extra.remove(key);
            }
            changed = true;
        }
        if (!changed) continue;
        SqlQuery u(d);
        u.prepare(QStringLiteral("UPDATE main.%1 SET extra = ? WHERE id = ?").arg(table));
        u.addBindValue(QString::fromUtf8(QJsonDocument(row.extra).toJson(QJsonDocument::Compact)));
        u.addBindValue(row.id);
        if (!u.exec()) {
            if (error) *error = u.lastError().text();
            return false;
        }
    }
    return true;
}
} // namespace

bool mergeFrom(const QString &remotePath, const QByteArray &key, Stats *stats, QString *error)
{
    Database &db = Database::instance();
    SqlDb d = db.handleDb();
    Stats st;
    auto fail = [&](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
    {
        SqlQuery a(d);
        a.prepare(QStringLiteral("ATTACH DATABASE ? AS r KEY ?"));
        a.addBindValue(remotePath);
        a.addBindValue(QString::fromLatin1(key));
        if (!a.exec()) return fail(tr("Die Datenbank des anderen Geräts lässt sich nicht öffnen: %1").arg(a.lastError().text()));
    }
    auto detach = [&] { d.exec(QStringLiteral("DETACH DATABASE r")); };
    if (scalar(d, QStringLiteral("SELECT count(*) FROM r.sqlite_master")) < 0) {
        detach();
        return fail(tr("Die Datenbank des anderen Geräts passt nicht zu dieser Datenbank (anderer Schlüssel)."));
    }
    if (scalar(d, QStringLiteral("SELECT count(*) FROM r.sqlite_master WHERE name = 'sync_deleted'")) != 1) {
        detach();
        return fail(tr("Die Datenbank des anderen Geräts stammt von einer älteren VitaParms-Version. "
                       "Bitte VitaParms auf dem anderen Gerät aktualisieren."));
    }

    if (!d.transaction()) { detach(); return fail(d.lastError().text()); }
    SqlQuery q(d);
    auto run = [&](const QString &sql) {
        if (q.exec(sql)) return true;
        if (error) *error = q.lastError().text() + QStringLiteral("\n") + sql;
        return false;
    };
    bool ok = run(QStringLiteral("UPDATE sync_state SET suspend = 1 WHERE id = 1"))
           && run(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS _chg (tbl TEXT, uid TEXT, PRIMARY KEY (tbl, uid))"))
           && run(QStringLiteral("DELETE FROM temp._chg"));

    for (const Database::SyncTable &t : Database::syncTables()) {
        if (!ok) break;
        const QString tbl = QString::fromLatin1(t.name);
        const auto fks = foreignKeys(t);
        QStringList fkCols;
        for (const auto &fk : fks) fkCols << fk.first;
        const QStringList remoteCols = columns(d, QStringLiteral("r"), tbl);
        if (remoteCols.isEmpty()) continue;   // Tabelle gibt es auf der Gegenseite (ältere Version) noch nicht
        QStringList cols;
        for (const QString &c : columns(d, QStringLiteral("main"), tbl))
            if (remoteCols.contains(c) && c != QLatin1String("id") && c != QLatin1String("uid")
                && c != QLatin1String("sync_at") && !fkCols.contains(c) && !t.localColumns.contains(c))
                cols << c;

        // 1. Löschvermerke der Gegenseite (jünger als die lokale Änderung)
        const qint64 before = scalar(d, QStringLiteral("SELECT count(*) FROM main.%1").arg(tbl));
        ok = run(QStringLiteral("DELETE FROM main.%1 WHERE EXISTS (SELECT 1 FROM r.sync_deleted rd"
                                " WHERE rd.uid = main.%1.uid AND rd.at >= main.%1.sync_at)").arg(tbl));
        if (!ok) break;
        st.deleted += int(before - scalar(d, QStringLiteral("SELECT count(*) FROM main.%1").arg(tbl)));

        // 2. Zeilen, bei denen die Gegenseite gewinnt (neu oder jünger, nicht lokal gelöscht)
        ok = run(QStringLiteral("INSERT INTO temp._chg (tbl, uid) SELECT '%1', rr.uid FROM r.%1 rr"
                                " LEFT JOIN main.%1 l ON l.uid = rr.uid"
                                " WHERE rr.uid IS NOT NULL AND (l.uid IS NULL OR rr.sync_at > l.sync_at)"
                                " AND NOT EXISTS (SELECT 1 FROM main.sync_deleted ld WHERE ld.uid = rr.uid AND ld.at >= rr.sync_at)")
                     .arg(tbl));
        if (!ok) break;

        // 3. Vorhandene Zeilen aktualisieren
        QStringList setCols = cols, selCols;
        for (const QString &c : cols) selCols << QStringLiteral("rr.") + c;
        for (const QString &fk : fkCols) {
            const QString parent = fks.at(fkCols.indexOf(fk)).second;
            setCols << fk;
            selCols << QStringLiteral("CASE WHEN rr.%1 IS NULL THEN NULL ELSE coalesce(%2, main.%3.%1) END")
                           .arg(fk, mapExpr(parent, QStringLiteral("rr.") + fk), tbl);
        }
        setCols << QStringLiteral("sync_at");
        selCols << QStringLiteral("rr.sync_at");
        const qint64 updates = scalar(d, QStringLiteral("SELECT count(*) FROM temp._chg c JOIN main.%1 l ON l.uid = c.uid"
                                                        " WHERE c.tbl = '%1'").arg(tbl));
        ok = run(QStringLiteral("UPDATE main.%1 SET (%2) = (SELECT %3 FROM r.%1 rr WHERE rr.uid = main.%1.uid)"
                                " WHERE uid IN (SELECT uid FROM temp._chg WHERE tbl = '%1')")
                     .arg(tbl, setCols.join(QLatin1Char(',')), selCols.join(QLatin1Char(','))));
        if (!ok) break;
        st.updated += int(updates);

        // 4. Neue Zeilen einfügen (Eltern müssen lokal vorhanden sein)
        QStringList insCols = QStringList{ QStringLiteral("uid"), QStringLiteral("sync_at") } + cols;
        QStringList insSel = QStringList{ QStringLiteral("rr.uid"), QStringLiteral("rr.sync_at") };
        for (const QString &c : cols) insSel << QStringLiteral("rr.") + c;
        QStringList parentOk;
        for (int i = 0; i < fks.size(); ++i) {
            const QString fk = fks.at(i).first;
            insCols << fk;
            insSel << mapExpr(fks.at(i).second, QStringLiteral("rr.") + fk);
            parentOk << QStringLiteral("(rr.%1 IS NULL OR %2 IS NOT NULL)").arg(fk, mapExpr(fks.at(i).second, QStringLiteral("rr.") + fk));
        }
        const qint64 n0 = scalar(d, QStringLiteral("SELECT count(*) FROM main.%1").arg(tbl));
        ok = run(QStringLiteral("INSERT INTO main.%1 (%2) SELECT %3 FROM r.%1 rr"
                                " WHERE rr.uid IN (SELECT uid FROM temp._chg WHERE tbl = '%1')"
                                " AND rr.uid NOT IN (SELECT uid FROM main.%1 WHERE uid IS NOT NULL)%4")
                     .arg(tbl, insCols.join(QLatin1Char(',')), insSel.join(QLatin1Char(',')),
                          parentOk.isEmpty() ? QString() : QStringLiteral(" AND ") + parentOk.join(QStringLiteral(" AND "))));
        if (!ok) break;
        st.inserted += int(scalar(d, QStringLiteral("SELECT count(*) FROM main.%1").arg(tbl)) - n0);

        // Dokumentinhalte mitnehmen
        if (tbl == QLatin1String("documents"))
            ok = run(QStringLiteral("INSERT OR REPLACE INTO main.document_files (document_id, data)"
                                    " SELECT l.id, rf.data FROM temp._chg c JOIN main.documents l ON l.uid = c.uid"
                                    " JOIN r.documents rd ON rd.uid = c.uid JOIN r.document_files rf ON rf.document_id = rd.id"
                                    " WHERE c.tbl = 'documents'"));
        if (!ok) break;

        // 5. Löschvermerke übernehmen (nur für Zeilen, die es hier nicht gibt)
        ok = run(QStringLiteral("INSERT OR REPLACE INTO main.sync_deleted (uid, tbl, at)"
                                " SELECT rd.uid, rd.tbl, rd.at FROM r.sync_deleted rd WHERE rd.tbl = '%1'"
                                " AND NOT EXISTS (SELECT 1 FROM main.%1 l WHERE l.uid = rd.uid)"
                                " AND rd.at > coalesce((SELECT ld.at FROM main.sync_deleted ld WHERE ld.uid = rd.uid), '')")
                     .arg(tbl));
    }

    // Verweise in JSON-Feldern
    if (ok) {
        QString err;
        ok = remapJson(d, QStringLiteral("medications"),
                       { { QStringLiteral("apptSeriesId"), QStringLiteral("appointment_series") },
                         { QStringLiteral("apptSeriesIds"), QStringLiteral("appointment_series") },
                         { QStringLiteral("leafletDocId"), QStringLiteral("documents") } }, &err)
          && remapJson(d, QStringLiteral("patients"),
                       { { QStringLiteral("pvDocId"), QStringLiteral("documents") },
                         { QStringLiteral("vvDocId"), QStringLiteral("documents") },
                         { QStringLiteral("bvDocId"), QStringLiteral("documents") } }, &err)
          && remapJson(d, QStringLiteral("appointments"),
                       { { QStringLiteral("medicationId"), QStringLiteral("medications") } }, &err);
        if (!ok && error) *error = err;
    }
    if (ok) ok = run(QStringLiteral("UPDATE sync_state SET suspend = 0 WHERE id = 1"));
    if (ok) ok = d.commit();
    if (!ok) {
        d.rollback();
        d.exec(QStringLiteral("UPDATE sync_state SET suspend = 0 WHERE id = 1"));
    }
    q.finish();
    d.exec(QStringLiteral("DROP TABLE IF EXISTS temp._chg"));
    detach();
    if (stats) *stats = st;
    return ok;
}

} // namespace DbMerge

// ====================================================================== Netzwerk

namespace {
struct Http {
    int status = 0;
    QByteArray body;
    QString error;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

QNetworkRequest request(const QString &baseUrl, const QString &key, const QUrlQuery &query = {},
                        const QString &endpoint = QStringLiteral("sync.php"))
{
    QUrl u(WebSync::normalizedUrl(baseUrl) + QStringLiteral("api/") + endpoint);
    u.setQuery(query);
    QNetworkRequest r(u);
    r.setRawHeader("X-VitaParms-Key", key.toUtf8());
    r.setRawHeader("Accept-Language", QLocale().bcp47Name().toUtf8());
    r.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("VitaParms/%1").arg(QCoreApplication::applicationVersion()));
    r.setTransferTimeout(120000);
    return r;
}

Http finish(QNetworkReply *reply, QIODevice *sink)
{
    QEventLoop loop;
    if (sink)
        QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, sink] { sink->write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Http h;
    h.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (sink) sink->write(reply->readAll());
    else h.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        h.error = reply->errorString();
        if (sink) h.body.clear();
    }
    reply->deleteLater();
    return h;
}

QString message(const Http &h)
{
    const QString e = h.json().value(QStringLiteral("error")).toString();
    if (!e.isEmpty()) return e;
    if (h.status == 404) return tr("Auf der Website fehlt api/sync.php (HTTP 404). Bitte den Website-Ordner aktualisieren.");
    if (!h.error.isEmpty()) return h.error;
    return tr("Unerwartete Antwort der Website (HTTP %1).").arg(h.status);
}

// Datei vom Server laden; Antwort mit Fehlertext (JSON) wird erkannt
bool download(QNetworkAccessManager &nam, const QString &url, const QString &key, const QString &what,
              const QString &target, QString *error)
{
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    const Http h = finish(nam.get(request(url, key, QUrlQuery{ { QStringLiteral("get"), what } })), &f);
    f.close();
    if (h.status != 200 || !h.error.isEmpty()) {
        QFile::remove(target);
        if (error) *error = h.error.isEmpty() ? tr("Download fehlgeschlagen (HTTP %1).").arg(h.status) : h.error;
        return false;
    }
    return true;
}

// ---- Dokumentinhalte (api/files.php)
const QString kFilesApi = QStringLiteral("files.php");

bool listServerFiles(QNetworkAccessManager &nam, const DbSync::Config &c, QSet<QString> *ids, QString *error)
{
    const Http h = finish(nam.get(request(c.url, c.uploadKey, QUrlQuery{ { QStringLiteral("list"), QStringLiteral("1") } }, kFilesApi)), nullptr);
    if (h.status != 200 || !h.json().value(QStringLiteral("ok")).toBool()) {
        if (error) *error = message(h);
        return false;
    }
    ids->clear();
    for (const QJsonValue &v : h.json().value(QStringLiteral("files")).toArray())
        ids->insert(v.toString());
    return true;
}

bool uploadServerFile(QNetworkAccessManager &nam, const DbSync::Config &c, const QString &id, const QByteArray &sealed, QString *error)
{
    QNetworkRequest r = request(c.url, c.uploadKey, QUrlQuery{ { QStringLiteral("put"), id } }, kFilesApi);
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    const Http h = finish(nam.post(r, sealed), nullptr);
    if (h.status != 200 || !h.json().value(QStringLiteral("ok")).toBool()) {
        if (error) *error = message(h);
        return false;
    }
    return true;
}

bool downloadServerFile(QNetworkAccessManager &nam, const DbSync::Config &c, const QString &id, QByteArray *sealed, QString *error)
{
    const Http h = finish(nam.get(request(c.url, c.uploadKey, QUrlQuery{ { QStringLiteral("get"), id } }, kFilesApi)), nullptr);
    if (h.status != 200 || !h.error.isEmpty() || !h.body.startsWith("VPF1")) {
        if (error) *error = message(h);
        return false;
    }
    *sealed = h.body;
    return true;
}

void pruneServerFiles(QNetworkAccessManager &nam, const DbSync::Config &c, const QStringList &keep)
{
    QNetworkRequest r = request(c.url, c.uploadKey, QUrlQuery{ { QStringLiteral("prune"), QStringLiteral("1") } }, kFilesApi);
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    finish(nam.post(r, QJsonDocument(QJsonObject{ { QStringLiteral("keep"), QJsonArray::fromStringList(keep) } }).toJson(QJsonDocument::Compact)), nullptr);
}
} // namespace

// ====================================================================== Abgleich

DbSync &DbSync::instance()
{
    static DbSync *inst = new DbSync;
    return *inst;
}

DbSync::DbSync()
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(60 * 1000);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        const Config c = config();
        if (c.autoSync && isReady(c) && !m_busy) sync();
    });
}

DbSync::Config DbSync::config()
{
    Database &db = Database::instance();
    const WebSync::Config w = WebSync::config();
    Config c;
    c.enabled = db.setting(s(kEnabled)) == QLatin1String("1");
    c.autoSync = db.setting(s(kAuto), QStringLiteral("1")) == QLatin1String("1");
    c.url = w.url;
    c.uploadKey = w.uploadKey;
    return c;
}

void DbSync::saveConfig(const Config &c)
{
    Database &db = Database::instance();
    db.setSetting(s(kEnabled), c.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    db.setSetting(s(kAuto), c.autoSync ? QStringLiteral("1") : QStringLiteral("0"));
    WebSync::Config w = WebSync::config();
    w.url = c.url;
    w.uploadKey = c.uploadKey;
    WebSync::saveConfig(w);
}

bool DbSync::isReady(const Config &c, QString *why)
{
    auto no = [why](const QString &m) { if (why) *why = m; return false; };
    if (!c.enabled) return no(tr("Der Abgleich ist nicht eingeschaltet."));
    const QUrl u(WebSync::normalizedUrl(c.url));
    if (c.url.trimmed().isEmpty() || !u.isValid()) return no(tr("Die Adresse der Website fehlt oder ist ungültig."));
    const bool local = u.host() == QLatin1String("localhost") || u.host() == QLatin1String("127.0.0.1");
    if (u.scheme() != QLatin1String("https") && !local) return no(tr("Die Website muss über https:// erreichbar sein."));
    if (u.scheme() == QLatin1String("https")) {
        QString tls;
        if (!tlsAvailable(&tls)) return no(tls);
    }
    if (c.uploadKey.size() < 16) return no(tr("Der Upload-Schlüssel fehlt."));
    if (!Database::instance().isEncrypted()) return no(tr("Nur verschlüsselte Datenbanken können abgeglichen werden."));
    return true;
}

DbSync::Status DbSync::status()
{
    Database &db = Database::instance();
    Status st;
    st.revision = db.setting(s(kRev), QStringLiteral("0")).toLongLong();
    st.last = QDateTime::fromString(db.setting(s(kLast)), Qt::ISODate);
    st.lastResult = db.setting(s(kResult));
    st.pending = db.syncChangeCounter() != db.setting(s(kBaseline), QStringLiteral("-1")).toLongLong();
    return st;
}

void DbSync::watchDatabase()
{
    if (m_watching) return;
    m_watching = true;
    Database &db = Database::instance();
    connect(&db, &Database::patientsChanged, this, &DbSync::markDirty);
    connect(&db, &Database::vitalsChanged, this, &DbSync::markDirty);
    connect(&db, &Database::appointmentsChanged, this, &DbSync::markDirty);
    connect(&db, &Database::documentsChanged, this, &DbSync::markDirty);
    connect(&db, &Database::preparationsChanged, this, &DbSync::markDirty);
    // Änderungen anderer Geräte regelmäßig abholen
    auto *poll = new QTimer(this);
    poll->setInterval(10 * 60 * 1000);
    connect(poll, &QTimer::timeout, this, [this] {
        const Config c = config();
        if (c.autoSync && isReady(c) && !m_busy) sync();
    });
    poll->start();
}

void DbSync::markDirty()
{
    const Config c = config();
    if (c.autoSync && isReady(c) && !m_busy) m_debounce->start();
}

void DbSync::sync()
{
    if (m_busy) return;
    m_busy = true;
    emit started();
    QString msg;
    const bool ok = runSync(&msg);
    Database &db = Database::instance();
    db.setSetting(s(kResult), msg);
    if (ok) db.setSetting(s(kLast), QDateTime::currentDateTime().toString(Qt::ISODate));
    m_busy = false;
    emit finished(ok, msg);
}

bool DbSync::syncBlocking(int timeoutMs, QString *message)
{
    Q_UNUSED(timeoutMs)   // Netzwerkzeitlimit je Anfrage: 2 Minuten
    if (m_busy) {
        QEventLoop wait;
        connect(this, &DbSync::finished, &wait, &QEventLoop::quit);
        wait.exec();
    }
    bool result = false;
    auto conn = connect(this, &DbSync::finished, this, [&](bool ok, const QString &m) {
        result = ok;
        if (message) *message = m;
    });
    sync();
    disconnect(conn);
    return result;
}

bool DbSync::runSync(QString *message)
{
    const Config c = config();
    QString why;
    if (!isReady(c, &why)) { *message = why; return false; }
    Database &db = Database::instance();
    const QString keysPath = KeyStore::pathFor(db.path());
    QNetworkAccessManager nam;
    QTemporaryDir tmp;
    DbMerge::Stats total;
    bool filesApi = false;                 // Server verwahrt Dokumentinhalte getrennt (api/files.php)
    QSet<QString> serverFiles;
    int filesUp = 0, filesDown = 0;
    bool done = false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        const Http st = finish(nam.get(request(c.url, c.uploadKey, QUrlQuery{ { QStringLiteral("state"), QStringLiteral("1") } })), nullptr);
        if (st.status != 200 || !st.json().value(QStringLiteral("ok")).toBool()) {
            *message = ::message(st);
            return false;
        }
        const qint64 serverRev = st.json().value(QStringLiteral("rev")).toVariant().toLongLong();
        if (attempt == 0) filesApi = st.json().value(QStringLiteral("files")).toInt() >= 1;
        const QString serverKeys = st.json().value(QStringLiteral("keysHash")).toString();
        const qint64 lastRev = db.setting(s(kRev), QStringLiteral("0")).toLongLong();
        const QString lastKeys = db.setting(s(kKeysHash));
        const bool localDirty = db.syncChangeCounter() != db.setting(s(kBaseline), QStringLiteral("-1")).toLongLong();
        QString localKeys = fileHash(keysPath);
        bool keysLocalChanged = localKeys != lastKeys;
        QString note;

        if (serverRev > 0 && serverRev != lastRev) {
            // Stand der anderen Geräte holen und einarbeiten
            const QString remote = tmp.filePath(QStringLiteral("remote-%1.vpdb").arg(attempt));
            QString err;
            if (!download(nam, c.url, c.uploadKey, QStringLiteral("db"), remote, &err)) { *message = err; return false; }
            DbMerge::Stats stats;
            if (!DbMerge::mergeFrom(remote, Session::dbKey(), &stats, &err)) { *message = err; return false; }
            total.inserted += stats.inserted;
            total.updated += stats.updated;
            total.deleted += stats.deleted;
            QFile::remove(remote);
            if (stats.total() > 0) db.notifyExternalChange();

            if (serverKeys != lastKeys && serverKeys != localKeys) {
                if (!keysLocalChanged) {
                    // Anmeldekonten wurden auf einem anderen Gerät geändert → übernehmen
                    const QString remoteKeys = tmp.filePath(QStringLiteral("keys-%1").arg(attempt));
                    if (!download(nam, c.url, c.uploadKey, QStringLiteral("keys"), remoteKeys, &err)) { *message = err; return false; }
                    KeyStore probe;
                    if (probe.load(remoteKeys) && probe.verify(Session::dbKey())) {
                        QFile::remove(keysPath + QStringLiteral(".bak"));
                        QFile::copy(keysPath, keysPath + QStringLiteral(".bak"));
                        QFile::remove(keysPath);
                        if (QFile::copy(remoteKeys, keysPath)) {
                            Session::keyStore().load(keysPath);
                            localKeys = fileHash(keysPath);
                            note = tr(" Anmeldekonten übernommen.");
                        }
                    }
                } else {
                    note = tr(" Die Anmeldekonten wurden auf beiden Geräten geändert – die Konten dieses Geräts gelten.");
                }
            }
            keysLocalChanged = localKeys != serverKeys;
            if (!localDirty && !keysLocalChanged) {
                db.setSetting(s(kRev), QString::number(serverRev));
                db.setSetting(s(kBaseline), QString::number(db.syncChangeCounter()));
                db.setSetting(s(kKeysHash), serverKeys);
                *message = (total.total() ? tr("Abgeglichen: %1 neu, %2 geändert, %3 gelöscht.")
                                                .arg(total.inserted).arg(total.updated).arg(total.deleted)
                                          : tr("Bereits auf dem neuesten Stand.")) + note;
                done = true;
                break;
            }
        } else if (serverRev > 0 && !localDirty && !keysLocalChanged) {
            *message = tr("Bereits auf dem neuesten Stand.");
            done = true;
            break;
        }

        // Eigenen (zusammengeführten) Stand hochladen – Dokumentinhalte vorab und einzeln
        QString err;
        if (filesApi) {
            if (!listServerFiles(nam, c, &serverFiles, &err)) { *message = err; return false; }
            const QHash<QString, qint64> mine = DocFiles::present(Session::dbKey());
            for (auto it = mine.cbegin(); it != mine.cend(); ++it) {
                if (serverFiles.contains(it.key())) continue;
                const QByteArray data = db.documentData(it.value());
                if (data.isEmpty()) continue;
                if (!uploadServerFile(nam, c, it.key(), DocFiles::seal(data, Session::dbKey()), &err)) { *message = err; return false; }
                serverFiles.insert(it.key());
                ++filesUp;
            }
        }
        const qint64 counter = db.syncChangeCounter();
        const QString copy = tmp.filePath(QStringLiteral("upload-%1.vpdb").arg(attempt));
        if (!db.backupTo(copy, &err, filesApi)) { *message = err; return false; }
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart base;
        base.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"base\""));
        base.setBody(QByteArray::number(serverRev));
        multi->append(base);
        for (const auto &[name, path] : { qMakePair(QStringLiteral("db"), copy), qMakePair(QStringLiteral("keys"), keysPath) }) {
            auto *f = new QFile(path, multi);
            if (!f->open(QIODevice::ReadOnly)) { delete multi; *message = f->errorString(); return false; }
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"%1\"; filename=\"%1.bin\"").arg(name));
            part.setBodyDevice(f);
            multi->append(part);
        }
        QNetworkReply *reply = nam.post(request(c.url, c.uploadKey), multi);
        multi->setParent(reply);
        const Http up = finish(reply, nullptr);
        QFile::remove(copy);
        if (up.status == 409) continue;            // ein anderes Gerät war schneller → erneut einarbeiten
        if (up.status != 200 || !up.json().value(QStringLiteral("ok")).toBool()) { *message = ::message(up); return false; }
        db.setSetting(s(kRev), QString::number(up.json().value(QStringLiteral("rev")).toVariant().toLongLong()));
        db.setSetting(s(kBaseline), QString::number(counter));
        db.setSetting(s(kKeysHash), up.json().value(QStringLiteral("keysHash")).toString());
        *message = (total.total() ? tr("Abgeglichen: %1 neu, %2 geändert, %3 gelöscht; eigener Stand hochgeladen.")
                                        .arg(total.inserted).arg(total.updated).arg(total.deleted)
                                  : tr("Eigener Stand hochgeladen.")) + note;
        done = true;
        break;
    }
    if (!done) {
        *message = tr("Der Abgleich ist mehrfach mit anderen Geräten zusammengestoßen. Bitte gleich noch einmal versuchen.");
        return false;
    }
    if (!filesApi) return true;

    // Fehlende Dokumentinhalte holen, nicht mehr gebrauchte auf dem Server löschen
    QString err;
    const QHash<QString, qint64> missing = DocFiles::missing(Session::dbKey());
    if (!missing.isEmpty()) {
        if (!listServerFiles(nam, c, &serverFiles, &err)) { *message += QLatin1Char(' ') + err; return false; }
        for (auto it = missing.cbegin(); it != missing.cend(); ++it) {
            if (!serverFiles.contains(it.key())) continue;
            QByteArray sealed;
            if (!downloadServerFile(nam, c, it.key(), &sealed, &err)
                || !DocFiles::storeSealed(it.key(), sealed, Session::dbKey(), &err)) {
                *message += QLatin1Char(' ') + err;
                return false;
            }
            ++filesDown;
        }
        if (filesDown) db.notifyDocumentsChanged();
    }
    QStringList keep;
    for (const Database::DocFileRef &r : db.documentFileRefs())
        if (!r.uid.isEmpty()) keep << DocFiles::fileId(Session::dbKey(), r.uid);
    pruneServerFiles(nam, c, keep);
    if (filesUp || filesDown)
        *message += QLatin1Char(' ') + tr("Dokumente: %1 hochgeladen, %2 geholt.").arg(filesUp).arg(filesDown);
    const int still = DocFiles::missing(Session::dbKey()).size();
    if (still)
        *message += QLatin1Char(' ') + tr("%1 Dokument(e) noch ohne Inhalt – sie kommen mit dem nächsten Abgleich des Geräts, auf dem sie erfasst wurden.").arg(still);
    return true;
}

bool DbSync::tlsAvailable(QString *text)
{
    if (QSslSocket::supportsSsl()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
        if (text) *text = tr("TLS bereit: %1 (%2)").arg(QSslSocket::sslLibraryVersionString(), QSslSocket::activeBackend());
#else
        if (text) *text = tr("TLS bereit: %1 (%2)").arg(QSslSocket::sslLibraryVersionString(), QStringLiteral("openssl"));
#endif
        return true;
    }
    if (text) {
#ifdef Q_OS_ANDROID
        *text = tr("Diese App kann keine verschlüsselten Verbindungen (HTTPS/TLS) aufbauen: Die OpenSSL-Bibliotheken "
                   "(libssl_3.so, libcrypto_3.so) fehlen im Installationspaket. Abhilfe siehe mobile/README.md, "
                   "Abschnitt „HTTPS“. Der Abgleich über das USB-Kabel funktioniert trotzdem.");
#else
        *text = tr("Verschlüsselte Verbindungen (HTTPS/TLS) sind nicht möglich: Es fehlt eine TLS-Bibliothek (%1).")
                    .arg(QSslSocket::sslLibraryBuildVersionString());
#endif
    }
    return false;
}

qint64 DbSync::serverRevision(const QString &url, const QString &uploadKey, QString *message)
{
    if (QUrl(WebSync::normalizedUrl(url)).scheme() == QLatin1String("https")) {
        QString tls;
        if (!tlsAvailable(&tls)) { if (message) *message = tls; return -1; }
    }
    QNetworkAccessManager nam;
    const Http st = finish(nam.get(request(url, uploadKey, QUrlQuery{ { QStringLiteral("state"), QStringLiteral("1") } })), nullptr);
    if (st.status != 200 || !st.json().value(QStringLiteral("ok")).toBool()) {
        if (message) *message = ::message(st);
        return -1;
    }
    const qint64 rev = st.json().value(QStringLiteral("rev")).toVariant().toLongLong();
    if (message)
        *message = rev > 0 ? tr("Auf dem Server liegt eine Datenbank (Stand %1, %2).")
                                 .arg(rev).arg(QLocale().formattedDataSize(st.json().value(QStringLiteral("size")).toVariant().toLongLong()))
                           : tr("Auf dem Server liegt noch keine Datenbank.");
    return rev;
}

bool DbSync::downloadInitial(const QString &url, const QString &uploadKey, const QString &dbPath, QString *error)
{
    QString msg;
    const qint64 rev = serverRevision(url, uploadKey, &msg);
    if (rev <= 0) {
        if (error) *error = rev < 0 ? msg : tr("Auf dem Server liegt noch keine Datenbank. "
                                               "Bitte zuerst am Desktop „Datenbank abgleichen“ wählen.");
        return false;
    }
    QNetworkAccessManager nam;
    const QString dbTmp = dbPath + QStringLiteral(".download"), keysTmp = KeyStore::pathFor(dbPath) + QStringLiteral(".download");
    if (!download(nam, url, uploadKey, QStringLiteral("db"), dbTmp, error)) return false;
    if (!download(nam, url, uploadKey, QStringLiteral("keys"), keysTmp, error)) { QFile::remove(dbTmp); return false; }
    // Stand merken: nach der Anmeldung übernimmt adoptInitialState() ihn in die Datenbank
    {
        QString m;
        QNetworkAccessManager n2;
        const Http st = finish(n2.get(request(url, uploadKey, QUrlQuery{ { QStringLiteral("state"), QStringLiteral("1") } })), nullptr);
        QFile side(dbPath + QStringLiteral(".syncstate"));
        if (side.open(QIODevice::WriteOnly | QIODevice::Truncate)) side.write(st.body);
    }
    for (const auto &[from, to] : { qMakePair(dbTmp, dbPath), qMakePair(keysTmp, KeyStore::pathFor(dbPath)) }) {
        QFile::remove(to);
        if (!QFile::rename(from, to)) {
            if (error) *error = tr("Die Datei %1 lässt sich nicht speichern.").arg(to);
            return false;
        }
    }
    return true;
}

void DbSync::adoptInitialState()
{
    Database &db = Database::instance();
    QFile side(db.path() + QStringLiteral(".syncstate"));
    if (!side.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(side.readAll()).object();
    side.close();
    side.remove();
    if (!o.value(QStringLiteral("ok")).toBool()) return;
    db.setSetting(s(kRev), QString::number(o.value(QStringLiteral("rev")).toVariant().toLongLong()));
    db.setSetting(s(kKeysHash), o.value(QStringLiteral("keysHash")).toString());
    db.setSetting(s(kBaseline), QString::number(db.syncChangeCounter()));
    db.setSetting(s(kEnabled), QStringLiteral("1"));
}
