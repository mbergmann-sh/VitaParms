// SPDX-License-Identifier: MIT
#include "database.h"
#include "bmi.h"
#include "medschedule.h"

#include "sqlquery.h"
#include "sqlite3mc.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QVariant>

namespace {

const QString kDtFmt = QStringLiteral("yyyy-MM-ddTHH:mm:ss");

QVariant opt(const std::optional<int> &o)    { return o ? QVariant(*o) : QVariant(); }
QVariant opt(const std::optional<double> &o) { return o ? QVariant(*o) : QVariant(); }
QVariant dateVar(const QDate &d) { return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant(); }

std::optional<int> optInt(const QVariant &v)
{
    if (v.isNull()) return std::nullopt;
    return v.toInt();
}
std::optional<double> optDouble(const QVariant &v)
{
    if (v.isNull()) return std::nullopt;
    return v.toDouble();
}

QString jsonStr(const QJsonObject &o)
{
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonObject jsonObj(const QVariant &v)
{
    if (v.isNull()) return {};
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}

bool fail(const SqlQuery &q, QString *error)
{
    if (error) *error = q.lastError().text();
    qWarning("SQL-Fehler: %s", qPrintable(q.lastError().text()));
    return false;
}

Patient patientFrom(const SqlQuery &q)
{
    Patient p;
    p.id          = q.value(QStringLiteral("id")).toLongLong();
    p.lastName    = q.value(QStringLiteral("last_name")).toString();
    p.firstName   = q.value(QStringLiteral("first_name")).toString();
    p.birthDate   = QDate::fromString(q.value(QStringLiteral("birth_date")).toString(), Qt::ISODate);
    p.sex         = q.value(QStringLiteral("sex")).toString();
    p.street      = q.value(QStringLiteral("street")).toString();
    p.zip         = q.value(QStringLiteral("zip")).toString();
    p.city        = q.value(QStringLiteral("city")).toString();
    p.phone       = q.value(QStringLiteral("phone")).toString();
    p.mobile      = q.value(QStringLiteral("mobile")).toString();
    p.diagnoses   = q.value(QStringLiteral("diagnoses")).toString();
    p.notes       = q.value(QStringLiteral("notes")).toString();
    p.amputations = Bmi::decode(q.value(QStringLiteral("amputations")).toString());
    p.careLevel   = q.value(QStringLiteral("care_level")).toInt();
    p.careReview  = QDate::fromString(q.value(QStringLiteral("care_review")).toString(), Qt::ISODate);
    p.disabilityDegree = q.value(QStringLiteral("disability_degree")).toInt();
    p.disabilityMarks  = q.value(QStringLiteral("disability_marks")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    p.disabilityValidUntil = QDate::fromString(q.value(QStringLiteral("disability_valid")).toString(), Qt::ISODate);
    p.extra       = jsonObj(q.value(QStringLiteral("extra")));
    return p;
}

VitalRecord vitalFrom(const SqlQuery &q)
{
    VitalRecord r;
    r.id           = q.value(QStringLiteral("id")).toLongLong();
    r.patientId    = q.value(QStringLiteral("patient_id")).toLongLong();
    r.measuredAt   = QDateTime::fromString(q.value(QStringLiteral("measured_at")).toString(), kDtFmt);
    r.context      = q.value(QStringLiteral("context")).toString();
    r.isAdmission  = q.value(QStringLiteral("is_admission")).toInt() != 0;
    r.pulse        = optInt(q.value(QStringLiteral("pulse")));
    r.systolic     = optInt(q.value(QStringLiteral("systolic")));
    r.diastolic    = optInt(q.value(QStringLiteral("diastolic")));
    r.temperature  = optDouble(q.value(QStringLiteral("temperature")));
    r.spo2         = optInt(q.value(QStringLiteral("spo2")));
    r.peakFlow     = optInt(q.value(QStringLiteral("peak_flow")));
    r.stool        = optInt(q.value(QStringLiteral("stool")));
    r.inr          = optDouble(q.value(QStringLiteral("inr")));
    r.fluidIn      = optInt(q.value(QStringLiteral("fluid_in")));
    r.fluidOut     = optInt(q.value(QStringLiteral("fluid_out")));
    r.edema        = optInt(q.value(QStringLiteral("edema")));
    {
        const QJsonObject bi = jsonObj(q.value(QStringLiteral("balance_info")));
        r.fluidInKind  = bi.value(QStringLiteral("inKind")).toString();
        r.fluidOutKind = bi.value(QStringLiteral("outKind")).toString();
        r.edemaSite    = bi.value(QStringLiteral("site")).toString();
        r.edemaSide    = bi.value(QStringLiteral("side")).toString();
    }
    if (r.stool) {
        const QJsonObject si = jsonObj(q.value(QStringLiteral("stool_info")));
        r.stoolDetail.amount    = si.value(QStringLiteral("amount")).toString();
        r.stoolDetail.color     = si.value(QStringLiteral("color")).toString();
        r.stoolDetail.admixture = si.value(QStringLiteral("admixture")).toString();
        r.stoolDetail.symptoms  = si.value(QStringLiteral("symptoms")).toString();
    }
    r.glucose      = optDouble(q.value(QStringLiteral("glucose")));
    r.weight       = optDouble(q.value(QStringLiteral("weight")));
    r.height       = optDouble(q.value(QStringLiteral("height")));
    r.bmi          = optDouble(q.value(QStringLiteral("bmi")));
    r.observations = q.value(QStringLiteral("observations")).toString();
    r.extra        = jsonObj(q.value(QStringLiteral("extra")));
    return r;
}

Medication medFrom(const SqlQuery &q)
{
    Medication m;
    m.id         = q.value(QStringLiteral("id")).toLongLong();
    m.patientId  = q.value(QStringLiteral("patient_id")).toLongLong();
    m.name       = q.value(QStringLiteral("name")).toString();
    m.substance  = q.value(QStringLiteral("substance")).toString();
    m.strength   = q.value(QStringLiteral("strength")).toString();
    m.form       = q.value(QStringLiteral("form")).toString();
    m.pzn        = q.value(QStringLiteral("pzn")).toString();
    m.category   = std::clamp(q.value(QStringLiteral("category")).toInt(), 0, Medication::CategoryCount - 1);
    MedSchedule::fromJsonString(m, q.value(QStringLiteral("rhythm")).toString());
    m.rhythmNote = q.value(QStringLiteral("rhythm_note")).toString();
    m.morning    = q.value(QStringLiteral("morning")).toString();
    m.noon       = q.value(QStringLiteral("noon")).toString();
    m.evening    = q.value(QStringLiteral("evening")).toString();
    m.night      = q.value(QStringLiteral("night")).toString();
    m.unit       = q.value(QStringLiteral("unit")).toString();
    m.reason     = q.value(QStringLiteral("reason")).toString();
    m.notes      = q.value(QStringLiteral("notes")).toString();
    m.startDate  = QDate::fromString(q.value(QStringLiteral("start_date")).toString(), Qt::ISODate);
    m.endDate    = QDate::fromString(q.value(QStringLiteral("end_date")).toString(), Qt::ISODate);
    m.archived   = q.value(QStringLiteral("archived")).toInt() != 0;
    m.archivedAt = QDateTime::fromString(q.value(QStringLiteral("archived_at")).toString(), kDtFmt);
    m.extra      = jsonObj(q.value(QStringLiteral("extra")));
    return m;
}

} // namespace

Database &Database::instance()
{
    static Database db;
    return db;
}

SqlDb Database::db() const
{
    return SqlDb(m_db);
}

bool Database::isOpen() const
{
    return m_db != nullptr;
}

bool Database::fileHasContent(const QString &path)
{
    QFileInfo fi(path);
    return fi.exists() && fi.size() > 0;
}

static int probeReadable(sqlite3 *h)
{
    return sqlite3_exec(h, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr);
}

Database::OpenResult Database::open(const QString &path, const QByteArray &key, QString *error)
{
    const bool reopen = m_wasOpen;
    close();

    sqlite3 *h = nullptr;
    int rc = sqlite3_open_v2(QFile::encodeName(path).constData(), &h,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = h ? QString::fromUtf8(sqlite3_errmsg(h)) : tr("Datei kann nicht geöffnet werden.");
        sqlite3_close(h);
        return OpenResult::Error;
    }
    sqlite3_busy_timeout(h, 5000);
    if (!key.isEmpty())
        sqlite3_key_v2(h, "main", key.constData(), int(key.size()));

    rc = probeReadable(h);
    if (rc == SQLITE_NOTADB) {
        sqlite3_close(h);
        if (error) *error = tr("Falscher Schlüssel oder keine VitaParms-Datenbank.");
        return key.isEmpty() ? OpenResult::NeedKey : OpenResult::WrongKey;
    }
    if (rc != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(h));
        sqlite3_close(h);
        return OpenResult::Error;
    }

    m_db = h;
    m_path = path;
    m_key = key;
    m_wasOpen = true;
    SqlDb d = db();
    d.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    // Kein WAL: das Umschlüsseln (rekey) ist nur im Rollback-Journal-Modus möglich.
    d.exec(QStringLiteral("PRAGMA journal_mode = DELETE"));
    d.exec(QStringLiteral("PRAGMA secure_delete = ON"));
    d.exec(QStringLiteral("PRAGMA temp_store = MEMORY"));

    if (!createSchema(error)) {
        close();
        return OpenResult::Error;
    }
    archiveExpiredMedications();
    if (reopen)
        emit databaseReopened();
    return key.isEmpty() ? OpenResult::OpenedPlain : OpenResult::Ok;
}

bool Database::rekey(const QByteArray &newKey, QString *error)
{
    if (!m_db) return false;
    const int rc = sqlite3_rekey_v2(m_db, "main", newKey.constData(), int(newKey.size()));
    if (rc != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    m_key.fill(0);
    m_key = newKey;
    return true;
}

void Database::close()
{
    if (!m_db) return;
    sqlite3_close_v2(m_db);
    m_db = nullptr;
    m_key.fill(0);
    m_key.clear();
}

bool Database::isEncryptedFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // Unverschlüsselte SQLite-Dateien beginnen mit "SQLite format 3"
    return f.size() > 0 && !f.read(16).startsWith("SQLite format 3");
}

bool Database::createSchema(QString *error)
{
    SqlQuery q(db());
    q.exec(QStringLiteral("PRAGMA user_version"));
    const int version = q.next() ? q.value(0).toInt() : 0;
    q.finish();
    if (version >= kSchemaVersion)
        return true;

    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS patients ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " last_name TEXT NOT NULL DEFAULT '',"
            " first_name TEXT NOT NULL DEFAULT '',"
            " birth_date TEXT, sex TEXT,"
            " street TEXT, zip TEXT, city TEXT,"
            " phone TEXT, mobile TEXT,"
            " diagnoses TEXT, notes TEXT,"
            " amputations TEXT,"
            " extra TEXT,"
            " created_at TEXT DEFAULT (datetime('now','localtime')),"
            " modified_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS vitals ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " measured_at TEXT NOT NULL,"
            " context TEXT,"
            " is_admission INTEGER NOT NULL DEFAULT 0,"
            " pulse INTEGER, systolic INTEGER, diastolic INTEGER,"
            " temperature REAL, spo2 INTEGER, glucose REAL,"
            " weight REAL, height REAL, bmi REAL,"
            " observations TEXT,"
            " extra TEXT,"
            " created_at TEXT DEFAULT (datetime('now','localtime')),"
            " modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_vitals_patient_time "
                       "ON vitals(patient_id, measured_at)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS medications ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " name TEXT NOT NULL, substance TEXT, strength TEXT, form TEXT,"
            " morning TEXT, noon TEXT, evening TEXT, night TEXT, unit TEXT,"
            " reason TEXT, notes TEXT,"
            " start_date TEXT, end_date TEXT,"
            " archived INTEGER NOT NULL DEFAULT 0, archived_at TEXT,"
            " extra TEXT,"
            " created_at TEXT DEFAULT (datetime('now','localtime')),"
            " modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_meds_patient ON medications(patient_id)"),
        // ---- Version 2: digitale Krankenakte
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS documents ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " doc_date TEXT, category TEXT, title TEXT NOT NULL DEFAULT '',"
            " issuer TEXT, notes TEXT, file_name TEXT, sha256 BLOB,"
            " size INTEGER, pages INTEGER, full_text TEXT, extra TEXT,"
            " added_at TEXT DEFAULT (datetime('now','localtime')),"
            " modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_docs_patient_date ON documents(patient_id, doc_date)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS document_files ("
            " document_id INTEGER PRIMARY KEY REFERENCES documents(id) ON DELETE CASCADE,"
            " data BLOB NOT NULL)"),
        // ---- Version 3: Terminplaner und Einstellungen
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS appointments ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER REFERENCES patients(id) ON DELETE CASCADE,"
            " owner TEXT NOT NULL DEFAULT '',"
            " title TEXT NOT NULL DEFAULT '', location TEXT,"
            " start_at TEXT NOT NULL, end_at TEXT, all_day INTEGER NOT NULL DEFAULT 0,"
            " priority INTEGER NOT NULL DEFAULT 1, notes TEXT,"
            " remind_minutes INTEGER NOT NULL DEFAULT -1,"
            " remind_visual INTEGER NOT NULL DEFAULT 1, remind_sound INTEGER NOT NULL DEFAULT 1,"
            " remind_email INTEGER NOT NULL DEFAULT 0, email_to TEXT,"
            " reminded_at TEXT, snooze_until TEXT, done INTEGER NOT NULL DEFAULT 0,"
            " created_by TEXT, extra TEXT,"
            " created_at TEXT DEFAULT (datetime('now','localtime')), modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_appt_start ON appointments(start_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_appt_owner ON appointments(owner, start_at)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)"),
        // ---- Version 4: Terminserien und Protokoll "Medikamente stellen"
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS appointment_series ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " rule TEXT NOT NULL, dtstart TEXT NOT NULL, exdates TEXT, generated_until TEXT,"
            " created_by TEXT, created_at TEXT DEFAULT (datetime('now','localtime')))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS med_preparations ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " prepared_at TEXT NOT NULL, prepared_by TEXT, slot INTEGER NOT NULL DEFAULT -1,"
            " total INTEGER NOT NULL DEFAULT 0, confirmed INTEGER NOT NULL DEFAULT 0,"
            " complete INTEGER NOT NULL DEFAULT 0, items TEXT, note TEXT, modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_prep_patient ON med_preparations(patient_id, prepared_at)"),
        // ---- Version 8: Bestandsführung (Packungen, Buchungen, Bestellungen)
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS med_stock ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " medication_id INTEGER NOT NULL REFERENCES medications(id) ON DELETE CASCADE,"
            " quantity REAL NOT NULL DEFAULT 0, expiry TEXT, received TEXT, note TEXT,"
            " created_at TEXT DEFAULT (datetime('now','localtime')), modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_stock_med ON med_stock(medication_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS med_orders ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " created_at TEXT NOT NULL, created_by TEXT, status INTEGER NOT NULL DEFAULT 0,"
            " ordered_at TEXT, ordered_by TEXT, delivered_at TEXT, note TEXT, modified_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS med_order_items ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " order_id INTEGER NOT NULL REFERENCES med_orders(id) ON DELETE CASCADE,"
            " medication_id INTEGER REFERENCES medications(id) ON DELETE SET NULL,"
            " name TEXT NOT NULL DEFAULT '', substance TEXT, strength TEXT, form TEXT, pzn TEXT,"
            " amount TEXT, reason TEXT, delivered INTEGER NOT NULL DEFAULT 0, delivered_qty REAL,"
            " delivered_at TEXT, modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_order_items ON med_order_items(order_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS med_stock_moves ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " patient_id INTEGER NOT NULL REFERENCES patients(id) ON DELETE CASCADE,"
            " medication_id INTEGER NOT NULL REFERENCES medications(id) ON DELETE CASCADE,"
            " stock_id INTEGER REFERENCES med_stock(id) ON DELETE SET NULL,"
            " at TEXT NOT NULL, for_date TEXT, kind INTEGER NOT NULL DEFAULT 0, quantity REAL NOT NULL DEFAULT 0,"
            " by_user TEXT, note TEXT, prep_key TEXT, modified_at TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_moves_med ON med_stock_moves(medication_id, at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_moves_prep ON med_stock_moves(prep_key)"),
    };

    if (!db().transaction()) {
        if (error) *error = db().lastError().text();
        return false;
    }
    for (const QString &s : ddl) {
        if (!q.exec(s)) {
            db().rollback();
            return fail(q, error);
        }
    }
    // Alle Anweisungen sind idempotent (IF NOT EXISTS) – ältere Datenbanken werden so
    // automatisch ergänzt. Neue Spalten bestehender Tabellen:
    if (!ensureColumn(QStringLiteral("appointments"), QStringLiteral("series_id"),
                      QStringLiteral("INTEGER REFERENCES appointment_series(id) ON DELETE SET NULL"), error)
        || !ensureColumn(QStringLiteral("appointments"), QStringLiteral("series_orig"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("medications"), QStringLiteral("pzn"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("medications"), QStringLiteral("rhythm"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("medications"), QStringLiteral("rhythm_note"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("medications"), QStringLiteral("category"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
        || !ensureColumn(QStringLiteral("med_preparations"), QStringLiteral("for_date"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("med_preparations"), QStringLiteral("days"), QStringLiteral("INTEGER NOT NULL DEFAULT 1"), error)
        || !ensureColumn(QStringLiteral("appointments"), QStringLiteral("series_detached"),
                         QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("peak_flow"), QStringLiteral("INTEGER"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("stool"), QStringLiteral("INTEGER"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("stool_info"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("inr"), QStringLiteral("REAL"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("fluid_in"), QStringLiteral("INTEGER"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("fluid_out"), QStringLiteral("INTEGER"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("edema"), QStringLiteral("INTEGER"), error)
        || !ensureColumn(QStringLiteral("vitals"), QStringLiteral("balance_info"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("patients"), QStringLiteral("care_level"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
        || !ensureColumn(QStringLiteral("patients"), QStringLiteral("care_review"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("patients"), QStringLiteral("disability_degree"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error)
        || !ensureColumn(QStringLiteral("patients"), QStringLiteral("disability_marks"), QStringLiteral("TEXT"), error)
        || !ensureColumn(QStringLiteral("patients"), QStringLiteral("disability_valid"), QStringLiteral("TEXT"), error)
        || !migratePeakFlow(version, error)
        || !q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_appt_series ON appointments(series_id, series_orig)"))
        || !ensureSyncSchema(error)) {
        db().rollback();
        return q.lastError().text().isEmpty() ? false : fail(q, error);
    }
    q.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    q.finish();
    if (!db().commit()) {
        if (error) *error = db().lastError().text();
        return false;
    }
    return true;
}

bool Database::ensureColumn(const QString &table, const QString &column, const QString &decl, QString *error)
{
    SqlQuery q(db());
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return fail(q, error);
    while (q.next())
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    q.finish();
    if (!q.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, decl))) return fail(q, error);
    return true;
}

bool Database::migratePeakFlow(int fromVersion, QString *error)
{
    if (fromVersion >= 6) return true;
    struct Row { qint64 id; QJsonObject extra; int value; };
    QList<Row> rows;
    {
        SqlQuery q(db());
        if (!q.exec(QStringLiteral("SELECT id, extra FROM vitals WHERE peak_flow IS NULL AND extra LIKE '%eak%'")))
            return fail(q, error);
        static const QRegularExpression keyRx(QStringLiteral("^\\s*(peak[- ]?flow|pef)\\s*$"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression numRx(QStringLiteral("(\\d{2,4})"));
        while (q.next()) {
            QJsonObject e = jsonObj(q.value(1));
            for (auto it = e.begin(); it != e.end(); ++it) {
                if (!keyRx.match(it.key()).hasMatch()) continue;
                int v = 0;
                if (it.value().isDouble()) v = qRound(it.value().toDouble());
                else if (const auto m = numRx.match(it.value().toString()); m.hasMatch()) v = m.captured(1).toInt();
                if (v >= 30 && v <= 1500) {
                    e.erase(it);
                    rows << Row{ q.value(0).toLongLong(), e, v };
                }
                break;
            }
        }
    }
    for (const Row &r : rows) {
        SqlQuery u(db());
        u.prepare(QStringLiteral("UPDATE vitals SET peak_flow=?, extra=? WHERE id=?"));
        u.addBindValue(r.value);
        u.addBindValue(jsonStr(r.extra));
        u.addBindValue(r.id);
        if (!u.exec()) return fail(u, error);
    }
    return true;
}

// ---------------------------------------------------------------- Abgleich (Schema 7)

const QList<Database::SyncTable> &Database::syncTables()
{
    static const QList<SyncTable> t = {
        { "patients",           nullptr,      {} },
        { "appointment_series", nullptr,      {} },
        { "documents",          "patient_id", {} },
        { "vitals",             "patient_id", {} },
        { "medications",        "patient_id", {} },
        { "appointments",       "patient_id", { QStringLiteral("reminded_at"), QStringLiteral("snooze_until") } },
        { "med_preparations",   "patient_id", {} },
        { "med_stock",          "patient_id", {} },
        { "med_orders",         "patient_id", {} },
        { "med_order_items",    "patient_id", {} },
        { "med_stock_moves",    "patient_id", {} },
    };
    return t;
}

QString Database::syncNow()
{
    return QStringLiteral("strftime('%Y-%m-%dT%H:%M:%f','now')");
}

bool Database::ensureSyncSchema(QString *error)
{
    SqlQuery q(db());
    const QString now = syncNow();
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sync_deleted (uid TEXT PRIMARY KEY, tbl TEXT NOT NULL, at TEXT NOT NULL)"))
        || !q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS sync_state (id INTEGER PRIMARY KEY CHECK (id = 1), changes INTEGER NOT NULL DEFAULT 0)"))
        || !q.exec(QStringLiteral("INSERT OR IGNORE INTO sync_state (id, changes) VALUES (1, 0)")))
        return fail(q, error);
    // suspend = 1: Änderungen beim Zusammenführen nicht als neue Bearbeitung werten
    if (!ensureColumn(QStringLiteral("sync_state"), QStringLiteral("suspend"), QStringLiteral("INTEGER NOT NULL DEFAULT 0"), error))
        return false;
    const QString bump = QStringLiteral(" UPDATE sync_state SET changes = changes + 1 WHERE id = 1;");
    for (const SyncTable &t : syncTables()) {
        const QString tbl = QString::fromLatin1(t.name);
        if (!ensureColumn(tbl, QStringLiteral("uid"), QStringLiteral("TEXT"), error)
            || !ensureColumn(tbl, QStringLiteral("sync_at"), QStringLiteral("TEXT"), error))
            return false;
        // Vorhandene Zeilen erhalten eine Kennung; Terminvorkommen einer Serie eine
        // feste (Serie@Zeitpunkt), damit beide Geräte dieselben Vorkommen erzeugen.
        QString uidExpr = QStringLiteral("lower(hex(randomblob(16)))");
        if (tbl == QLatin1String("appointments")) {
            if (!q.exec(QStringLiteral("UPDATE appointment_series SET uid = lower(hex(randomblob(16))) WHERE uid IS NULL")))
                return fail(q, error);
            uidExpr = QStringLiteral("CASE WHEN series_id IS NOT NULL AND series_orig IS NOT NULL THEN "
                                     "(SELECT s.uid FROM appointment_series s WHERE s.id = series_id) || '@' || series_orig "
                                     "ELSE lower(hex(randomblob(16))) END");
        }
        if (!q.exec(QStringLiteral("UPDATE %1 SET uid = %2 WHERE uid IS NULL").arg(tbl, uidExpr))
            || !q.exec(QStringLiteral("UPDATE %1 SET sync_at = '1970-01-01T00:00:00.000' WHERE sync_at IS NULL").arg(tbl))
            || !q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_%1_uid ON %1(uid)").arg(tbl)))
            return fail(q, error);

        // Spalten für die Änderungserkennung
        QStringList cols;
        {
            SqlQuery c(db());
            c.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tbl));
            while (c.next()) {
                const QString n = c.value(1).toString();
                if (n == QLatin1String("id") || n == QLatin1String("uid") || n == QLatin1String("sync_at")
                    || n == QLatin1String("modified_at") || t.localColumns.contains(n))
                    continue;
                cols << QStringLiteral("NEW.%1 IS NOT OLD.%1").arg(n);
            }
        }
        QString newUid = QStringLiteral("lower(hex(randomblob(16)))");
        if (tbl == QLatin1String("appointments"))
            newUid = QStringLiteral("CASE WHEN NEW.series_id IS NOT NULL AND NEW.series_orig IS NOT NULL THEN "
                                    "(SELECT s.uid FROM appointment_series s WHERE s.id = NEW.series_id) || '@' || NEW.series_orig "
                                    "ELSE lower(hex(randomblob(16))) END");
        const QStringList ddl = {
            QStringLiteral("DROP TRIGGER IF EXISTS vp_sync_ins_%1").arg(tbl),
            QStringLiteral("DROP TRIGGER IF EXISTS vp_sync_upd_%1").arg(tbl),
            QStringLiteral("DROP TRIGGER IF EXISTS vp_sync_del_%1").arg(tbl),
            QStringLiteral("CREATE TRIGGER vp_sync_ins_%1 AFTER INSERT ON %1 BEGIN"
                           " UPDATE %1 SET uid = coalesce(NEW.uid, %2), sync_at = coalesce(NEW.sync_at, %3)"
                           " WHERE rowid = NEW.rowid;"
                           " DELETE FROM sync_deleted WHERE uid = (SELECT uid FROM %1 WHERE rowid = NEW.rowid);%4 END")
                .arg(tbl, newUid, now, bump),
            QStringLiteral("CREATE TRIGGER vp_sync_upd_%1 AFTER UPDATE ON %1 WHEN NEW.sync_at IS OLD.sync_at AND (%2)"
                           " AND (SELECT suspend FROM sync_state WHERE id = 1) = 0 BEGIN"
                           " UPDATE %1 SET sync_at = %3 WHERE rowid = NEW.rowid;%4 END")
                .arg(tbl, cols.join(QStringLiteral(" OR ")), now, bump),
            QStringLiteral("CREATE TRIGGER vp_sync_del_%1 AFTER DELETE ON %1 WHEN OLD.uid IS NOT NULL BEGIN"
                           " INSERT OR REPLACE INTO sync_deleted (uid, tbl, at) VALUES (OLD.uid, '%1', %2);%3 END")
                .arg(tbl, now, bump),
        };
        for (const QString &d : ddl)
            if (!q.exec(d)) return fail(q, error);
    }
    // Dokument-Inhalt ersetzt → Dokument gilt als geändert
    if (!q.exec(QStringLiteral("DROP TRIGGER IF EXISTS vp_sync_docfile"))
        || !q.exec(QStringLiteral("CREATE TRIGGER vp_sync_docfile AFTER UPDATE OF data ON document_files BEGIN"
                                  " UPDATE documents SET sync_at = %1 WHERE id = NEW.document_id;%2 END").arg(now, bump)))
        return fail(q, error);
    return true;
}

qint64 Database::syncChangeCounter()
{
    SqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT changes FROM sync_state WHERE id = 1")) && q.next())
        return q.value(0).toLongLong();
    return 0;
}

void Database::notifyExternalChange()
{
    emit patientsChanged();
    emit appointmentsChanged();
}

void Database::notifyDocumentsChanged()
{
    SqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT DISTINCT patient_id FROM documents")))
        while (q.next()) emit documentsChanged(q.value(0).toLongLong());
}

bool Database::beginBulk()
{
    return db().transaction();
}

bool Database::commitBulk()
{
    return db().commit();
}

bool Database::backupTo(const QString &target, QString *error, bool withoutFiles)
{
    if (!m_db) return false;
    if (QFile::exists(target) && !QFile::remove(target)) {
        if (error) *error = tr("Zieldatei kann nicht überschrieben werden.");
        return false;
    }
    sqlite3 *t = nullptr;
    if (sqlite3_open_v2(QFile::encodeName(target).constData(), &t,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (error) *error = t ? QString::fromUtf8(sqlite3_errmsg(t)) : tr("Zieldatei kann nicht angelegt werden.");
        sqlite3_close(t);
        return false;
    }
    // Sicherung mit demselben Schlüssel verschlüsseln
    if (!m_key.isEmpty())
        sqlite3_key_v2(t, "main", m_key.constData(), int(m_key.size()));
    sqlite3_backup *b = sqlite3_backup_init(t, "main", m_db, "main");
    int rc = SQLITE_ERROR;
    if (b) {
        rc = sqlite3_backup_step(b, -1);
        sqlite3_backup_finish(b);
    }
    bool ok = (rc == SQLITE_DONE) && sqlite3_errcode(t) == SQLITE_OK;
    if (ok && withoutFiles) {
        char *msg = nullptr;
        ok = sqlite3_exec(t, "DELETE FROM document_files; VACUUM;", nullptr, nullptr, &msg) == SQLITE_OK;
        if (msg) sqlite3_free(msg);
    }
    if (!ok && error) *error = QString::fromUtf8(sqlite3_errmsg(t));
    sqlite3_close(t);
    if (!ok) QFile::remove(target);
    return ok;
}

// ---------------------------------------------------------------- Patienten

QList<Patient> Database::patients()
{
    QList<Patient> l;
    SqlQuery q(db());
    q.exec(QStringLiteral("SELECT * FROM patients ORDER BY last_name COLLATE NOCASE, "
                          "first_name COLLATE NOCASE"));
    while (q.next())
        l << patientFrom(q);
    return l;
}

std::optional<Patient> Database::patient(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM patients WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return patientFrom(q);
    return std::nullopt;
}

bool Database::savePatient(Patient &p, QString *error)
{
    SqlQuery q(db());
    if (p.id == 0) {
        q.prepare(QStringLiteral(
            "INSERT INTO patients (last_name, first_name, birth_date, sex, street, zip, city,"
            " phone, mobile, diagnoses, notes, amputations, extra, care_level, care_review,"
            " disability_degree, disability_marks, disability_valid, modified_at)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
    } else {
        q.prepare(QStringLiteral(
            "UPDATE patients SET last_name=?, first_name=?, birth_date=?, sex=?, street=?,"
            " zip=?, city=?, phone=?, mobile=?, diagnoses=?, notes=?, amputations=?, extra=?,"
            " care_level=?, care_review=?, disability_degree=?, disability_marks=?, disability_valid=?,"
            " modified_at=datetime('now','localtime') WHERE id=?"));
    }
    q.addBindValue(p.lastName.trimmed());
    q.addBindValue(p.firstName.trimmed());
    q.addBindValue(dateVar(p.birthDate));
    q.addBindValue(p.sex);
    q.addBindValue(p.street);
    q.addBindValue(p.zip);
    q.addBindValue(p.city);
    q.addBindValue(p.phone);
    q.addBindValue(p.mobile);
    q.addBindValue(p.diagnoses);
    q.addBindValue(p.notes);
    q.addBindValue(Bmi::encode(p.amputations));
    q.addBindValue(jsonStr(p.extra));
    q.addBindValue(qBound(0, p.careLevel, 5));
    q.addBindValue(p.careLevel > 0 ? dateVar(p.careReview) : QVariant());
    q.addBindValue(qBound(0, p.disabilityDegree, 100));
    q.addBindValue(p.disabilityDegree > 0 ? p.disabilityMarks.join(QLatin1Char(' ')) : QString());
    q.addBindValue(p.disabilityDegree > 0 ? dateVar(p.disabilityValidUntil) : QVariant());
    if (p.id != 0)
        q.addBindValue(p.id);
    if (!q.exec())
        return fail(q, error);
    if (p.id == 0)
        p.id = q.lastInsertId().toLongLong();
    emit patientsChanged();
    return true;
}

bool Database::deletePatient(qint64 id, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM patients WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        return fail(q, error);
    emit patientsChanged();
    return true;
}

// --------------------------------------------------------------- Medikation

QList<Medication> Database::medications(qint64 patientId, bool includeArchived)
{
    QList<Medication> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM medications WHERE patient_id = ? %1 "
                             "ORDER BY archived, name COLLATE NOCASE")
                  .arg(includeArchived ? QString() : QStringLiteral("AND archived = 0")));
    q.addBindValue(patientId);
    if (q.exec())
        while (q.next())
            l << medFrom(q);
    return l;
}

bool Database::expireMedication(Medication &m, const QDate &today)
{
    if (m.archived || !m.endDate.isValid() || m.endDate >= today) return false;
    m.archived = true;
    m.archivedAt = QDateTime(m.endDate.addDays(1), QTime(0, 0));
    if (!m.extra.contains(QStringLiteral("stopOn")))     // vorgemerktes Absetzen: bleibt „abgesetzt am …“
        m.extra[QStringLiteral("autoArchived")] = true; // Ende erreicht (nicht von Hand abgesetzt)
    return true;
}

int Database::archiveExpiredMedications()
{
    if (!isOpen()) return 0;
    QList<qint64> patients;
    {
        SqlQuery q(db());
        if (q.exec(QStringLiteral("SELECT DISTINCT patient_id FROM medications WHERE archived = 0 AND end_date IS NOT NULL"
                                  " AND end_date <> '' AND end_date < date('now','localtime')")))
            while (q.next()) patients << q.value(0).toLongLong();
    }
    int n = 0;
    for (qint64 pid : patients) {
        QList<Medication> meds = medications(pid, true);
        int changed = 0;
        for (Medication &m : meds) changed += expireMedication(m);
        if (changed && saveMedications(pid, meds, {}, nullptr)) n += changed;
    }
    if (n) emit patientsChanged();
    return n;
}

bool Database::saveMedications(qint64 patientId, QList<Medication> &meds,
                               const QList<qint64> &deletedIds, QString *error)
{
    SqlDb d = db();
    d.transaction();
    SqlQuery q(d);
    for (qint64 id : deletedIds) {
        q.prepare(QStringLiteral("DELETE FROM medications WHERE id = ? AND patient_id = ?"));
        q.addBindValue(id);
        q.addBindValue(patientId);
        if (!q.exec()) { d.rollback(); return fail(q, error); }
    }
    for (Medication &m : meds) {
        m.patientId = patientId;
        expireMedication(m);          // Enddatum vorbei → Historie
        if (m.id == 0) {
            q.prepare(QStringLiteral(
                "INSERT INTO medications (patient_id, name, substance, strength, form, morning,"
                " noon, evening, night, unit, reason, notes, start_date, end_date, archived,"
                " archived_at, extra, pzn, rhythm, rhythm_note, category, modified_at)"
                " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
            q.addBindValue(patientId);
        } else {
            q.prepare(QStringLiteral(
                "UPDATE medications SET name=?, substance=?, strength=?, form=?, morning=?,"
                " noon=?, evening=?, night=?, unit=?, reason=?, notes=?, start_date=?,"
                " end_date=?, archived=?, archived_at=?, extra=?, pzn=?, rhythm=?, rhythm_note=?, category=?,"
                " modified_at=datetime('now','localtime') WHERE id=?"));
        }
        q.addBindValue(m.name);
        q.addBindValue(m.substance);
        q.addBindValue(m.strength);
        q.addBindValue(m.form);
        q.addBindValue(m.morning);
        q.addBindValue(m.noon);
        q.addBindValue(m.evening);
        q.addBindValue(m.night);
        q.addBindValue(m.unit);
        q.addBindValue(m.reason);
        q.addBindValue(m.notes);
        q.addBindValue(dateVar(m.startDate));
        q.addBindValue(dateVar(m.endDate));
        q.addBindValue(m.archived ? 1 : 0);
        q.addBindValue(m.archivedAt.isValid() ? QVariant(m.archivedAt.toString(kDtFmt)) : QVariant());
        q.addBindValue(jsonStr(m.extra));
        q.addBindValue(m.pzn);
        q.addBindValue(MedSchedule::toJsonString(m));
        q.addBindValue(m.rhythmNote);
        q.addBindValue(m.category);
        if (m.id != 0)
            q.addBindValue(m.id);
        if (!q.exec()) { d.rollback(); return fail(q, error); }
        if (m.id == 0)
            m.id = q.lastInsertId().toLongLong();
    }
    return d.commit();
}

// --------------------------------------------------------------- Vitalwerte

QList<VitalRecord> Database::vitals(qint64 patientId, const QDateTime &from,
                                    const QDateTime &to, bool includeAdmission)
{
    QString sql = QStringLiteral("SELECT * FROM vitals WHERE patient_id = ?");
    if (from.isValid()) sql += QStringLiteral(" AND measured_at >= ?");
    if (to.isValid())   sql += QStringLiteral(" AND measured_at < ?");
    if (!includeAdmission) sql += QStringLiteral(" AND is_admission = 0");
    sql += QStringLiteral(" ORDER BY measured_at DESC, id DESC");

    SqlQuery q(db());
    q.prepare(sql);
    q.addBindValue(patientId);
    if (from.isValid()) q.addBindValue(from.toString(kDtFmt));
    if (to.isValid())   q.addBindValue(to.toString(kDtFmt));

    QList<VitalRecord> l;
    if (q.exec())
        while (q.next())
            l << vitalFrom(q);
    return l;
}

std::optional<VitalRecord> Database::vital(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM vitals WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return vitalFrom(q);
    return std::nullopt;
}

std::optional<VitalRecord> Database::admissionVitals(qint64 patientId)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM vitals WHERE patient_id = ? AND is_admission = 1 "
                             "ORDER BY measured_at LIMIT 1"));
    q.addBindValue(patientId);
    if (q.exec() && q.next())
        return vitalFrom(q);
    return std::nullopt;
}

std::optional<double> Database::latestHeight(qint64 patientId)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT height FROM vitals WHERE patient_id = ? AND height IS NOT NULL "
                             "ORDER BY measured_at DESC LIMIT 1"));
    q.addBindValue(patientId);
    if (q.exec() && q.next())
        return q.value(0).toDouble();
    return std::nullopt;
}

bool Database::saveVital(VitalRecord &r, QString *error)
{
    SqlQuery q(db());
    if (r.id == 0) {
        q.prepare(QStringLiteral(
            "INSERT INTO vitals (patient_id, measured_at, context, is_admission, pulse, systolic,"
            " diastolic, temperature, spo2, glucose, weight, height, bmi, observations, extra, peak_flow,"
            " stool, stool_info, inr, fluid_in, fluid_out, edema, balance_info, modified_at)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
        q.addBindValue(r.patientId);
    } else {
        q.prepare(QStringLiteral(
            "UPDATE vitals SET measured_at=?, context=?, is_admission=?, pulse=?, systolic=?,"
            " diastolic=?, temperature=?, spo2=?, glucose=?, weight=?, height=?, bmi=?,"
            " observations=?, extra=?, peak_flow=?, stool=?, stool_info=?, inr=?, fluid_in=?, fluid_out=?, edema=?, balance_info=?, modified_at=datetime('now','localtime') WHERE id=?"));
    }
    q.addBindValue(r.measuredAt.toString(kDtFmt));
    q.addBindValue(r.context);
    q.addBindValue(r.isAdmission ? 1 : 0);
    q.addBindValue(opt(r.pulse));
    q.addBindValue(opt(r.systolic));
    q.addBindValue(opt(r.diastolic));
    q.addBindValue(opt(r.temperature));
    q.addBindValue(opt(r.spo2));
    q.addBindValue(opt(r.glucose));
    q.addBindValue(opt(r.weight));
    q.addBindValue(opt(r.height));
    q.addBindValue(opt(r.bmi));
    q.addBindValue(r.observations);
    q.addBindValue(jsonStr(r.extra));
    q.addBindValue(opt(r.peakFlow));
    q.addBindValue(opt(r.stool));
    {
        QJsonObject si;
        const StoolDetail &d = r.stoolDetail;
        if (!d.amount.trimmed().isEmpty())    si.insert(QStringLiteral("amount"), d.amount.trimmed());
        if (!d.color.trimmed().isEmpty())     si.insert(QStringLiteral("color"), d.color.trimmed());
        if (!d.admixture.trimmed().isEmpty()) si.insert(QStringLiteral("admixture"), d.admixture.trimmed());
        if (!d.symptoms.trimmed().isEmpty())  si.insert(QStringLiteral("symptoms"), d.symptoms.trimmed());
        q.addBindValue(r.stool && !si.isEmpty() ? QVariant(jsonStr(si)) : QVariant());
    }
    q.addBindValue(opt(r.inr));
    q.addBindValue(opt(r.fluidIn));
    q.addBindValue(opt(r.fluidOut));
    q.addBindValue(opt(r.edema));
    {
        QJsonObject bi;
        if (r.fluidIn && !r.fluidInKind.trimmed().isEmpty())   bi.insert(QStringLiteral("inKind"), r.fluidInKind.trimmed());
        if (r.fluidOut && !r.fluidOutKind.trimmed().isEmpty()) bi.insert(QStringLiteral("outKind"), r.fluidOutKind.trimmed());
        if (r.edema && *r.edema > 0 && !r.edemaSite.trimmed().isEmpty()) bi.insert(QStringLiteral("site"), r.edemaSite.trimmed());
        if (r.edema && *r.edema > 0 && !r.edemaSide.trimmed().isEmpty()) bi.insert(QStringLiteral("side"), r.edemaSide.trimmed());
        q.addBindValue(bi.isEmpty() ? QVariant() : QVariant(jsonStr(bi)));
    }
    if (r.id != 0)
        q.addBindValue(r.id);
    if (!q.exec())
        return fail(q, error);
    if (r.id == 0)
        r.id = q.lastInsertId().toLongLong();
    emit vitalsChanged(r.patientId);
    return true;
}

bool Database::deleteVital(qint64 id, QString *error)
{
    const auto r = vital(id);
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM vitals WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        return fail(q, error);
    if (r)
        emit vitalsChanged(r->patientId);
    return true;
}

// ----------------------------------------------------------- Krankenakte

static MedicalDocument docFrom(const SqlQuery &q, bool withText)
{
    MedicalDocument d;
    d.id        = q.value(QStringLiteral("id")).toLongLong();
    d.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    d.docDate   = QDate::fromString(q.value(QStringLiteral("doc_date")).toString(), Qt::ISODate);
    d.category  = q.value(QStringLiteral("category")).toString();
    d.title     = q.value(QStringLiteral("title")).toString();
    d.issuer    = q.value(QStringLiteral("issuer")).toString();
    d.notes     = q.value(QStringLiteral("notes")).toString();
    d.fileName  = q.value(QStringLiteral("file_name")).toString();
    d.sha256    = q.value(QStringLiteral("sha256")).toByteArray();
    d.size      = q.value(QStringLiteral("size")).toLongLong();
    d.pages     = q.value(QStringLiteral("pages")).toInt();
    if (withText)
        d.fullText = q.value(QStringLiteral("full_text")).toString();
    d.addedAt   = QDateTime::fromString(q.value(QStringLiteral("added_at")).toString(),
                                        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    d.extra     = jsonObj(q.value(QStringLiteral("extra")));
    if (const QVariant hd = q.value(QStringLiteral("has_data")); hd.isValid() && !hd.isNull())
        d.hasData = hd.toBool();
    return d;
}

QList<MedicalDocument> Database::documents(qint64 patientId)
{
    QList<MedicalDocument> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT d.*, EXISTS (SELECT 1 FROM document_files f WHERE f.document_id = d.id) AS has_data"
                             " FROM documents d WHERE d.patient_id = ? ORDER BY d.doc_date DESC, d.id DESC"));
    q.addBindValue(patientId);
    if (q.exec())
        while (q.next())
            l << docFrom(q, true);
    return l;
}

std::optional<MedicalDocument> Database::document(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT d.*, EXISTS (SELECT 1 FROM document_files f WHERE f.document_id = d.id) AS has_data"
                             " FROM documents d WHERE d.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return docFrom(q, true);
    return std::nullopt;
}

QByteArray Database::documentData(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT data FROM document_files WHERE document_id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
    return {};
}

bool Database::addDocument(MedicalDocument &d, const QByteArray &pdf, QString *error)
{
    SqlDb c = db();
    c.transaction();
    SqlQuery q(c);
    q.prepare(QStringLiteral(
        "INSERT INTO documents (patient_id, doc_date, category, title, issuer, notes, file_name,"
        " sha256, size, pages, full_text, extra, modified_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
    q.addBindValue(d.patientId);
    q.addBindValue(dateVar(d.docDate));
    q.addBindValue(d.category);
    q.addBindValue(d.title);
    q.addBindValue(d.issuer);
    q.addBindValue(d.notes);
    q.addBindValue(d.fileName);
    q.addBindValue(d.sha256);
    q.addBindValue(qint64(pdf.size()));
    q.addBindValue(d.pages);
    q.addBindValue(d.fullText);
    q.addBindValue(jsonStr(d.extra));
    if (!q.exec()) { c.rollback(); return fail(q, error); }
    d.id = q.lastInsertId().toLongLong();
    d.size = pdf.size();
    q.prepare(QStringLiteral("INSERT INTO document_files (document_id, data) VALUES (?, ?)"));
    q.addBindValue(d.id);
    q.addBindValue(pdf);
    if (!q.exec()) { c.rollback(); d.id = 0; return fail(q, error); }
    if (!c.commit()) {
        if (error) *error = c.lastError().text();
        return false;
    }
    emit documentsChanged(d.patientId);
    return true;
}

bool Database::updateDocument(const MedicalDocument &d, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE documents SET doc_date=?, category=?, title=?, issuer=?, notes=?, extra=?,"
        " modified_at=datetime('now','localtime') WHERE id=?"));
    q.addBindValue(dateVar(d.docDate));
    q.addBindValue(d.category);
    q.addBindValue(d.title);
    q.addBindValue(d.issuer);
    q.addBindValue(d.notes);
    q.addBindValue(jsonStr(d.extra));
    q.addBindValue(d.id);
    if (!q.exec())
        return fail(q, error);
    emit documentsChanged(d.patientId);
    return true;
}

bool Database::deleteDocument(qint64 id, QString *error)
{
    const auto d = document(id);
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM documents WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        return fail(q, error);
    if (d) emit documentsChanged(d->patientId);
    return true;
}

std::optional<MedicalDocument> Database::findDocumentByHash(qint64 patientId, const QByteArray &sha256)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM documents WHERE patient_id = ? AND sha256 = ? LIMIT 1"));
    q.addBindValue(patientId);
    q.addBindValue(sha256);
    if (q.exec() && q.next())
        return docFrom(q, false);
    return std::nullopt;
}

QList<Database::DocFileRef> Database::documentFileRefs()
{
    QList<DocFileRef> l;
    SqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT d.id, d.uid, d.sha256, (f.document_id IS NOT NULL) FROM documents d"
                              " LEFT JOIN document_files f ON f.document_id = d.id")))
        while (q.next())
            l << DocFileRef{ q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toByteArray(), q.value(3).toBool() };
    return l;
}

bool Database::hasDocumentData(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT 1 FROM document_files WHERE document_id = ?"));
    q.addBindValue(id);
    return q.exec() && q.next();
}

bool Database::storeDocumentData(qint64 id, const QByteArray &data, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO document_files (document_id, data) VALUES (?, ?)"));
    q.addBindValue(id);
    q.addBindValue(data);
    if (!q.exec()) return fail(q, error);
    return true;
}

QStringList Database::documentCategories(qint64 patientId)
{
    QStringList l;
    SqlQuery q(db());
    if (patientId) {
        q.prepare(QStringLiteral("SELECT DISTINCT category FROM documents WHERE patient_id = ? "
                                 "AND category <> '' ORDER BY category COLLATE NOCASE"));
        q.addBindValue(patientId);
    } else {
        q.prepare(QStringLiteral("SELECT DISTINCT category FROM documents WHERE category <> '' "
                                 "ORDER BY category COLLATE NOCASE"));
    }
    if (q.exec())
        while (q.next())
            l << q.value(0).toString();
    return l;
}

// ----------------------------------------------------------- Terminplaner

static QVariant dtVar(const QDateTime &d) { return d.isValid() ? QVariant(d.toString(kDtFmt)) : QVariant(); }

static Appointment apptFrom(const SqlQuery &q)
{
    Appointment a;
    a.id            = q.value(QStringLiteral("id")).toLongLong();
    a.patientId     = q.value(QStringLiteral("patient_id")).toLongLong();
    a.owner         = q.value(QStringLiteral("owner")).toString();
    a.title         = q.value(QStringLiteral("title")).toString();
    a.location      = q.value(QStringLiteral("location")).toString();
    a.start         = QDateTime::fromString(q.value(QStringLiteral("start_at")).toString(), kDtFmt);
    a.end           = QDateTime::fromString(q.value(QStringLiteral("end_at")).toString(), kDtFmt);
    a.allDay        = q.value(QStringLiteral("all_day")).toInt() != 0;
    a.priority      = q.value(QStringLiteral("priority")).toInt();
    a.notes         = q.value(QStringLiteral("notes")).toString();
    a.remindMinutes = q.value(QStringLiteral("remind_minutes")).toInt();
    a.remindVisual  = q.value(QStringLiteral("remind_visual")).toInt() != 0;
    a.remindSound   = q.value(QStringLiteral("remind_sound")).toInt() != 0;
    a.remindEmail   = q.value(QStringLiteral("remind_email")).toInt() != 0;
    a.emailTo       = q.value(QStringLiteral("email_to")).toString();
    a.remindedAt    = QDateTime::fromString(q.value(QStringLiteral("reminded_at")).toString(), kDtFmt);
    a.snoozeUntil   = QDateTime::fromString(q.value(QStringLiteral("snooze_until")).toString(), kDtFmt);
    a.done          = q.value(QStringLiteral("done")).toInt() != 0;
    a.createdBy     = q.value(QStringLiteral("created_by")).toString();
    a.extra         = jsonObj(q.value(QStringLiteral("extra")));
    a.seriesId      = q.value(QStringLiteral("series_id")).toLongLong();
    a.seriesOrig    = QDateTime::fromString(q.value(QStringLiteral("series_orig")).toString(), kDtFmt);
    a.detached      = q.value(QStringLiteral("series_detached")).toInt() != 0;
    return a;
}

QList<Appointment> Database::appointments(const QDateTime &from, const QDateTime &to,
                                          const QString &owner, qint64 patientId, bool includeDone)
{
    QString sql = QStringLiteral("SELECT * FROM appointments WHERE 1=1");
    if (from.isValid()) sql += QStringLiteral(" AND COALESCE(end_at, start_at) >= ?");
    if (to.isValid())   sql += QStringLiteral(" AND start_at < ?");
    if (!owner.isEmpty()) sql += QStringLiteral(" AND owner = ? COLLATE NOCASE");
    if (patientId)      sql += QStringLiteral(" AND patient_id = ?");
    if (!includeDone)   sql += QStringLiteral(" AND done = 0");
    sql += QStringLiteral(" ORDER BY start_at, priority DESC, id");
    SqlQuery q(db());
    q.prepare(sql);
    if (from.isValid()) q.addBindValue(from.toString(kDtFmt));
    if (to.isValid())   q.addBindValue(to.toString(kDtFmt));
    if (!owner.isEmpty()) q.addBindValue(owner);
    if (patientId)      q.addBindValue(patientId);
    QList<Appointment> l;
    if (q.exec())
        while (q.next()) l << apptFrom(q);
    return l;
}

std::optional<Appointment> Database::appointment(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM appointments WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) return apptFrom(q);
    return std::nullopt;
}

bool Database::saveAppointment(Appointment &a, QString *error)
{
    if (!writeAppointment(a, error)) return false;
    emit appointmentsChanged();
    return true;
}

bool Database::writeAppointment(Appointment &a, QString *error)
{
    SqlQuery q(db());
    if (a.id == 0) {
        q.prepare(QStringLiteral(
            "INSERT INTO appointments (patient_id, owner, title, location, start_at, end_at, all_day,"
            " priority, notes, remind_minutes, remind_visual, remind_sound, remind_email, email_to,"
            " reminded_at, snooze_until, done, created_by, extra, series_id, series_orig, series_detached,"
            " modified_at)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
    } else {
        q.prepare(QStringLiteral(
            "UPDATE appointments SET patient_id=?, owner=?, title=?, location=?, start_at=?, end_at=?,"
            " all_day=?, priority=?, notes=?, remind_minutes=?, remind_visual=?, remind_sound=?,"
            " remind_email=?, email_to=?, reminded_at=?, snooze_until=?, done=?, created_by=?, extra=?,"
            " series_id=?, series_orig=?, series_detached=?,"
            " modified_at=datetime('now','localtime') WHERE id=?"));
    }
    q.addBindValue(a.patientId ? QVariant(a.patientId) : QVariant());
    q.addBindValue(a.owner);
    q.addBindValue(a.title);
    q.addBindValue(a.location);
    q.addBindValue(a.start.toString(kDtFmt));
    q.addBindValue(dtVar(a.end));
    q.addBindValue(a.allDay ? 1 : 0);
    q.addBindValue(a.priority);
    q.addBindValue(a.notes);
    q.addBindValue(a.remindMinutes);
    q.addBindValue(a.remindVisual ? 1 : 0);
    q.addBindValue(a.remindSound ? 1 : 0);
    q.addBindValue(a.remindEmail ? 1 : 0);
    q.addBindValue(a.emailTo);
    q.addBindValue(dtVar(a.remindedAt));
    q.addBindValue(dtVar(a.snoozeUntil));
    q.addBindValue(a.done ? 1 : 0);
    q.addBindValue(a.createdBy);
    q.addBindValue(jsonStr(a.extra));
    q.addBindValue(a.seriesId ? QVariant(a.seriesId) : QVariant());
    q.addBindValue(a.seriesId ? dtVar(a.seriesOrig) : QVariant());
    q.addBindValue(a.seriesId && a.detached ? 1 : 0);
    if (a.id) q.addBindValue(a.id);
    if (!q.exec()) return fail(q, error);
    if (a.id == 0) a.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::deleteAppointment(qint64 id, QString *error)
{
    // Vorkommen einer Serie: als Ausnahme merken, sonst würde es neu angelegt
    if (const auto a = appointment(id); a && a->seriesId) return deleteOccurrence(id, error);
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM appointments WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return fail(q, error);
    emit appointmentsChanged();
    return true;
}

QList<Appointment> Database::pendingReminders(const QString &owner, const QDateTime &now)
{
    // Kandidaten: nicht erledigt, noch nicht erinnert, Beginn höchstens 6 h zurück
    SqlQuery q(db());
    q.prepare(QStringLiteral(
        "SELECT * FROM appointments WHERE done = 0 AND reminded_at IS NULL"
        " AND (remind_minutes >= 0 OR snooze_until IS NOT NULL)"
        " AND owner = ? COLLATE NOCASE AND start_at >= ? ORDER BY start_at"));
    q.addBindValue(owner);
    q.addBindValue(now.addSecs(-6 * 3600).toString(kDtFmt));
    QList<Appointment> l;
    if (q.exec())
        while (q.next()) {
            const Appointment a = apptFrom(q);
            const QDateTime t = a.reminderTime();
            if (t.isValid() && t <= now) l << a;
        }
    return l;
}

bool Database::markReminded(qint64 id, const QDateTime &when)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE appointments SET reminded_at = ?, snooze_until = NULL WHERE id = ?"));
    q.addBindValue(when.toString(kDtFmt));
    q.addBindValue(id);
    const bool ok = q.exec();
    if (ok) emit appointmentsChanged();
    return ok;
}

bool Database::snoozeAppointment(qint64 id, const QDateTime &until)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE appointments SET reminded_at = NULL, snooze_until = ? WHERE id = ?"));
    q.addBindValue(until.toString(kDtFmt));
    q.addBindValue(id);
    const bool ok = q.exec();
    if (ok) emit appointmentsChanged();
    return ok;
}

bool Database::setAppointmentDone(qint64 id, bool done)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE appointments SET done = ?, modified_at=datetime('now','localtime') WHERE id = ?"));
    q.addBindValue(done ? 1 : 0);
    q.addBindValue(id);
    const bool ok = q.exec();
    if (ok) emit appointmentsChanged();
    return ok;
}

QString Database::setting(const QString &key, const QString &fallback)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return fallback;
}

bool Database::setSetting(const QString &key, const QString &value)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                             "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QStringList Database::vitalExtraKeys(qint64 patientId)
{
    QStringList keys;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT extra FROM vitals WHERE patient_id = ? AND extra IS NOT NULL "
                             "AND extra <> ''"));
    q.addBindValue(patientId);
    if (q.exec()) {
        while (q.next()) {
            const QJsonObject o = jsonObj(q.value(0));
            for (auto it = o.begin(); it != o.end(); ++it)
                if (!keys.contains(it.key()))
                    keys << it.key();
        }
    }
    keys.sort(Qt::CaseInsensitive);
    return keys;
}

// ----------------------------------------------------------- Terminserien

namespace {

QString datesToString(const QList<QDate> &l)
{
    QStringList s;
    for (const QDate &d : l) s << d.toString(Qt::ISODate);
    return s.join(QLatin1Char(','));
}

QList<QDate> datesFromString(const QString &s)
{
    QList<QDate> l;
    for (const QString &p : s.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QDate d = QDate::fromString(p.trimmed(), Qt::ISODate);
        if (d.isValid()) l << d;
    }
    return l;
}

AppointmentSeries seriesFrom(const SqlQuery &q)
{
    AppointmentSeries s;
    s.id = q.value(QStringLiteral("id")).toLongLong();
    s.rule = Recurrence::fromJson(QJsonDocument::fromJson(q.value(QStringLiteral("rule")).toString().toUtf8()).object());
    s.dtstart = QDateTime::fromString(q.value(QStringLiteral("dtstart")).toString(), kDtFmt);
    s.exdates = datesFromString(q.value(QStringLiteral("exdates")).toString());
    s.generatedUntil = QDate::fromString(q.value(QStringLiteral("generated_until")).toString(), Qt::ISODate);
    s.createdBy = q.value(QStringLiteral("created_by")).toString();
    return s;
}

} // namespace

std::optional<AppointmentSeries> Database::series(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM appointment_series WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) return seriesFrom(q);
    return std::nullopt;
}

QList<AppointmentSeries> Database::allSeries()
{
    QList<AppointmentSeries> l;
    SqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT * FROM appointment_series ORDER BY id")))
        while (q.next()) l << seriesFrom(q);
    return l;
}

QList<Appointment> Database::seriesAppointments(qint64 seriesId)
{
    QList<Appointment> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM appointments WHERE series_id = ? ORDER BY series_orig, id"));
    q.addBindValue(seriesId);
    if (q.exec())
        while (q.next()) l << apptFrom(q);
    return l;
}

bool Database::writeSeries(AppointmentSeries &s, QString *error)
{
    SqlQuery q(db());
    if (s.id == 0)
        q.prepare(QStringLiteral("INSERT INTO appointment_series (rule, dtstart, exdates, generated_until, created_by)"
                                 " VALUES (?,?,?,?,?)"));
    else
        q.prepare(QStringLiteral("UPDATE appointment_series SET rule=?, dtstart=?, exdates=?, generated_until=?,"
                                 " created_by=? WHERE id=?"));
    q.addBindValue(QString::fromUtf8(QJsonDocument(s.rule.toJson()).toJson(QJsonDocument::Compact)));
    q.addBindValue(s.dtstart.toString(kDtFmt));
    q.addBindValue(datesToString(s.exdates));
    q.addBindValue(s.generatedUntil.isValid() ? QVariant(s.generatedUntil.toString(Qt::ISODate)) : QVariant());
    q.addBindValue(s.createdBy);
    if (s.id) q.addBindValue(s.id);
    if (!q.exec()) return fail(q, error);
    if (s.id == 0) s.id = q.lastInsertId().toLongLong();
    return true;
}

QDate Database::horizonFor(const AppointmentSeries &s) const
{
    return std::max(QDate::currentDate(), s.dtstart.date()).addDays(seriesHorizonDays());
}

bool Database::generateOccurrences(AppointmentSeries &s, const Appointment &tmpl, const QDate &to,
                                   const QHash<QDate, Appointment> &carry, int *created, QString *error)
{
    // bereits vorhandene Vorkommen (auch einzeln verschobene) nicht doppelt anlegen
    QSet<QDate> existing;
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT series_orig FROM appointments WHERE series_id = ?"));
        q.addBindValue(s.id);
        if (!q.exec()) return fail(q, error);
        while (q.next()) existing.insert(QDateTime::fromString(q.value(0).toString(), kDtFmt).date());
    }
    const QSet<QDate> ex(s.exdates.cbegin(), s.exdates.cend());
    const qint64 dur = tmpl.end.isValid() ? tmpl.start.secsTo(tmpl.end) : -1;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QDate &d : s.rule.dates(s.dtstart.date(), to)) {
        if (existing.contains(d) || ex.contains(d)) continue;
        Appointment a = tmpl;
        a.id = 0;
        a.seriesId = s.id;
        a.detached = false;
        a.start = QDateTime(d, s.dtstart.time());
        a.seriesOrig = a.start;
        a.end = dur >= 0 ? a.start.addSecs(dur) : QDateTime();
        a.snoozeUntil = {};
        a.done = false;
        a.remindedAt = {};
        if (const auto it = carry.constFind(d); it != carry.cend()) {
            a.done = it->done;
            a.remindedAt = it->remindedAt;
        }
        // Vergangene Vorkommen nicht nachträglich erinnern
        if (!a.remindedAt.isValid() && a.start < now) a.remindedAt = now;
        if (!writeAppointment(a, error)) return false;
        if (created) ++*created;
    }
    if (!s.generatedUntil.isValid() || s.generatedUntil < to) s.generatedUntil = to;
    return writeSeries(s, error);
}

bool Database::createSeriesNoTx(Appointment &first, Recurrence rule, const QHash<QDate, Appointment> &carry,
                                QString *error)
{
    rule.normalize(first.start.date());
    if (rule.isNone()) {
        first.seriesId = 0;
        first.seriesOrig = {};
        first.detached = false;
        return writeAppointment(first, error);
    }
    AppointmentSeries s;
    s.rule = rule;
    s.dtstart = first.start;
    s.createdBy = first.createdBy;
    if (!writeSeries(s, error)) return false;
    first.seriesId = s.id;
    first.seriesOrig = first.start;
    first.detached = false;
    if (!writeAppointment(first, error)) return false;
    return generateOccurrences(s, first, horizonFor(s), carry, nullptr, error);
}

bool Database::createSeries(Appointment &first, Recurrence rule, QString *error)
{
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    if (!createSeriesNoTx(first, rule, {}, error)) { d.rollback(); return false; }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

bool Database::cutSeriesNoTx(qint64 seriesId, const QDateTime &fromOrig, QHash<QDate, Appointment> *carry,
                             QString *error)
{
    auto s = series(seriesId);
    if (!s) return true;
    // Zustand der wegfallenden Vorkommen merken (erledigt / erinnert)
    for (const Appointment &a : seriesAppointments(seriesId))
        if (a.seriesOrig >= fromOrig && carry) carry->insert(a.seriesOrig.date(), a);
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM appointments WHERE series_id = ? AND series_orig >= ?"));
    q.addBindValue(seriesId);
    q.addBindValue(fromOrig.toString(kDtFmt));
    if (!q.exec()) return fail(q, error);
    q.finish();

    int remaining = 0;
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM appointments WHERE series_id = ?"));
    q.addBindValue(seriesId);
    if (q.exec() && q.next()) remaining = q.value(0).toInt();
    q.finish();

    if (fromOrig.date() <= s->dtstart.date() || remaining == 0) {
        q.prepare(QStringLiteral("DELETE FROM appointment_series WHERE id = ?"));
        q.addBindValue(seriesId);
        if (!q.exec()) return fail(q, error);
        // übrig gebliebene Einzelstücke (sollte es nicht geben) werden zu Einzelterminen
        return true;
    }
    s->rule.count = 0;   // "Anzahl" wird zum Enddatum – die früheren Vorkommen bleiben exakt erhalten
    s->rule.until = fromOrig.date().addDays(-1);
    if (s->generatedUntil.isValid() && s->generatedUntil > s->rule.until) s->generatedUntil = s->rule.until;
    return writeSeries(*s, error);
}

bool Database::splitSeries(qint64 seriesId, const QDateTime &fromOrig, Appointment &first,
                           Recurrence rule, QString *error, bool carryFirst)
{
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    QHash<QDate, Appointment> carry;
    if (!cutSeriesNoTx(seriesId, fromOrig, &carry, error)) { d.rollback(); return false; }
    first.id = 0;   // die bisherige Zeile ist entfernt worden
    if (carryFirst)
        if (const auto it = carry.constFind(first.start.date()); it != carry.cend()) {
            first.done = it->done;
            first.remindedAt = it->remindedAt;
        }
    if (!first.remindedAt.isValid() && first.start < QDateTime::currentDateTime() && carryFirst)
        first.remindedAt = QDateTime::currentDateTime();
    if (!createSeriesNoTx(first, rule, carry, error)) { d.rollback(); return false; }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

bool Database::updateSeriesAll(qint64 seriesId, const Appointment &v, QString *error)
{
    auto s = series(seriesId);
    if (!s) { if (error) *error = QStringLiteral("series not found"); return false; }
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    const qint64 dur = v.end.isValid() ? v.start.secsTo(v.end) : -1;
    const QTime t = v.allDay ? QTime(0, 0) : v.start.time();
    const QDateTime now = QDateTime::currentDateTime();
    for (Appointment a : seriesAppointments(seriesId)) {
        const QDateTime oldStart = a.start;
        const int oldLead = a.remindMinutes;
        a.title = v.title;
        a.location = v.location;
        a.patientId = v.patientId;
        a.owner = v.owner;
        a.priority = v.priority;
        a.notes = v.notes;
        a.allDay = v.allDay;
        a.remindMinutes = v.remindMinutes;
        a.remindVisual = v.remindVisual;
        a.remindSound = v.remindSound;
        a.remindEmail = v.remindEmail;
        a.emailTo = v.emailTo;
        a.extra = v.extra;
        a.start = QDateTime(a.seriesOrig.date(), t);
        a.seriesOrig = a.start;
        a.end = dur >= 0 ? a.start.addSecs(dur) : QDateTime();
        a.detached = false;
        if (a.start > now && (a.start != oldStart || a.remindMinutes != oldLead)) {
            a.remindedAt = {};
            a.snoozeUntil = {};
        }
        if (!writeAppointment(a, error)) { d.rollback(); return false; }
    }
    s->dtstart = QDateTime(s->dtstart.date(), t);
    if (!writeSeries(*s, error)) { d.rollback(); return false; }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

bool Database::deleteOccurrence(qint64 appointmentId, QString *error)
{
    const auto a = appointment(appointmentId);
    if (!a) return true;
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    if (auto s = a->seriesId ? series(a->seriesId) : std::nullopt) {
        if (!s->exdates.contains(a->seriesOrig.date())) s->exdates << a->seriesOrig.date();
        std::sort(s->exdates.begin(), s->exdates.end());
        if (!writeSeries(*s, error)) { d.rollback(); return false; }
    }
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM appointments WHERE id = ?"));
    q.addBindValue(appointmentId);
    if (!q.exec()) { d.rollback(); return fail(q, error); }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

bool Database::truncateSeries(qint64 seriesId, const QDateTime &fromOrig, QString *error)
{
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    if (!cutSeriesNoTx(seriesId, fromOrig, nullptr, error)) { d.rollback(); return false; }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

bool Database::deleteSeries(qint64 seriesId, QString *error)
{
    SqlDb d = db();
    if (!d.transaction()) { if (error) *error = d.lastError().text(); return false; }
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM appointments WHERE series_id = ?"));
    q.addBindValue(seriesId);
    if (!q.exec()) { d.rollback(); return fail(q, error); }
    q.finish();
    q.prepare(QStringLiteral("DELETE FROM appointment_series WHERE id = ?"));
    q.addBindValue(seriesId);
    if (!q.exec()) { d.rollback(); return fail(q, error); }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    emit appointmentsChanged();
    return true;
}

int Database::extendSeries()
{
    if (!isOpen()) return 0;
    int created = 0;
    bool began = false;
    for (AppointmentSeries s : allSeries()) {
        const QDate target = horizonFor(s);
        if (s.generatedUntil.isValid() && s.generatedUntil >= target) continue;
        const QDate last = s.rule.lastDate(s.dtstart.date());
        if (last.isValid() && s.generatedUntil.isValid() && s.generatedUntil >= last) continue;
        // Vorlage: jüngstes nicht einzeln geändertes Vorkommen
        const QList<Appointment> rows = seriesAppointments(s.id);
        if (rows.isEmpty()) continue;
        Appointment tmpl = rows.last();
        for (auto it = rows.crbegin(); it != rows.crend(); ++it)
            if (!it->detached) { tmpl = *it; break; }
        if (!began) {
            if (!db().transaction()) return 0;
            began = true;
        }
        QString err;
        if (!generateOccurrences(s, tmpl, target, {}, &created, &err)) {
            qWarning("extendSeries: %s", qPrintable(err));
            db().rollback();
            return 0;
        }
    }
    if (began) db().commit();
    if (created) emit appointmentsChanged();
    return created;
}

QString Database::databaseUuid()
{
    QString id = setting(QStringLiteral("db/uuid"));
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        setSetting(QStringLiteral("db/uuid"), id);
    }
    return id;
}

// ----------------------------------------------------------- Protokoll Medikamente stellen

static MedPreparation prepFrom(const SqlQuery &q)
{
    MedPreparation p;
    p.id = q.value(QStringLiteral("id")).toLongLong();
    p.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    p.preparedAt = QDateTime::fromString(q.value(QStringLiteral("prepared_at")).toString(), kDtFmt);
    p.preparedBy = q.value(QStringLiteral("prepared_by")).toString();
    p.slot = q.value(QStringLiteral("slot")).toInt();
    p.total = q.value(QStringLiteral("total")).toInt();
    p.confirmed = q.value(QStringLiteral("confirmed")).toInt();
    p.complete = q.value(QStringLiteral("complete")).toInt() != 0;
    p.items = QJsonDocument::fromJson(q.value(QStringLiteral("items")).toString().toUtf8()).array();
    p.note = q.value(QStringLiteral("note")).toString();
    p.forDate = QDate::fromString(q.value(QStringLiteral("for_date")).toString(), Qt::ISODate);
    if (!p.forDate.isValid()) p.forDate = p.preparedAt.date();
    p.days = std::max(1, q.value(QStringLiteral("days")).toInt());
    return p;
}

bool Database::addPreparation(MedPreparation &p, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO med_preparations (patient_id, prepared_at, prepared_by, slot, total,"
                             " confirmed, complete, items, note, for_date, days, modified_at)"
                             " VALUES (?,?,?,?,?,?,?,?,?,?,?, datetime('now','localtime'))"));
    q.addBindValue(p.patientId);
    q.addBindValue(p.preparedAt.toString(kDtFmt));
    q.addBindValue(p.preparedBy);
    q.addBindValue(p.slot);
    q.addBindValue(p.total);
    q.addBindValue(p.confirmed);
    q.addBindValue(p.complete ? 1 : 0);
    q.addBindValue(QString::fromUtf8(QJsonDocument(p.items).toJson(QJsonDocument::Compact)));
    q.addBindValue(p.note);
    q.addBindValue((p.forDate.isValid() ? p.forDate : p.preparedAt.date()).toString(Qt::ISODate));
    q.addBindValue(std::max(1, p.days));
    if (!q.exec()) return fail(q, error);
    p.id = q.lastInsertId().toLongLong();
    emit preparationsChanged(p.patientId);
    return true;
}

bool Database::updatePreparationNote(qint64 id, const QString &note)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE med_preparations SET note = ?, modified_at = datetime('now','localtime')"
                             " WHERE id = ?"));
    q.addBindValue(note);
    q.addBindValue(id);
    return q.exec();
}

QList<MedPreparation> Database::preparations(qint64 patientId, const QDate &from, const QDate &to)
{
    QString sql = QStringLiteral("SELECT * FROM med_preparations WHERE patient_id = ?");
    if (from.isValid()) sql += QStringLiteral(" AND prepared_at >= ?");
    if (to.isValid()) sql += QStringLiteral(" AND prepared_at < ?");
    sql += QStringLiteral(" ORDER BY prepared_at DESC, id DESC");
    SqlQuery q(db());
    q.prepare(sql);
    q.addBindValue(patientId);
    if (from.isValid()) q.addBindValue(QDateTime(from, QTime(0, 0)).toString(kDtFmt));
    if (to.isValid()) q.addBindValue(QDateTime(to.addDays(1), QTime(0, 0)).toString(kDtFmt));
    QList<MedPreparation> l;
    if (q.exec())
        while (q.next()) l << prepFrom(q);
    return l;
}

std::optional<MedPreparation> Database::lastPreparation(qint64 patientId)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM med_preparations WHERE patient_id = ?"
                             " ORDER BY prepared_at DESC, id DESC LIMIT 1"));
    q.addBindValue(patientId);
    if (q.exec() && q.next()) return prepFrom(q);
    return std::nullopt;
}

bool Database::deletePreparation(qint64 id, QString *error)
{
    qint64 pid = 0;
    {
        SqlQuery q(db());
        q.prepare(QStringLiteral("SELECT patient_id FROM med_preparations WHERE id = ?"));
        q.addBindValue(id);
        if (q.exec() && q.next()) pid = q.value(0).toLongLong();
    }
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM med_preparations WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return fail(q, error);
    emit preparationsChanged(pid);
    return true;
}

bool Database::updateMedicationLeaflet(qint64 medId, const QString &pzn, const QJsonObject &extra, QString *error)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE medications SET pzn = ?, extra = ?, modified_at = datetime('now','localtime')"
                             " WHERE id = ?"));
    q.addBindValue(pzn);
    q.addBindValue(jsonStr(extra));
    q.addBindValue(medId);
    if (!q.exec()) return fail(q, error);
    return true;
}
