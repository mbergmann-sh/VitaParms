// SPDX-License-Identifier: MIT
#include "wounds.h"
#include "database.h"
#include "reportprinter.h"
#include "session.h"
#include "sqlquery.h"
#include "xlsxwriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace Wounds {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Wounds", s); }
const QString kDt = QStringLiteral("yyyy-MM-ddTHH:mm:ss");
SqlDb db() { return Database::instance().handleDb(); }
bool failQ(const SqlQuery &q, QString *error) { if (error) *error = q.lastError().text(); return false; }
QVariant dateVar(const QDate &d) { return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant(); }
QDate dateOf(const QVariant &v) { return QDate::fromString(v.toString().left(10), Qt::ISODate); }
QString num(double v, int prec = 1) { return QLocale().toString(v, 'f', prec); }
QString dmy(const QDate &d) { return d.isValid() ? QLocale().toString(d, QStringLiteral("dd.MM.yyyy")) : QString(); }

QJsonArray arr(const QStringList &l) { return QJsonArray::fromStringList(l); }
QStringList strs(const QJsonValue &v)
{
    QStringList l;
    for (const QJsonValue &x : v.toArray()) l << x.toString();
    return l;
}
void putOpt(QJsonObject &o, const char *k, const std::optional<double> &v) { if (v) o[QLatin1String(k)] = *v; }
void putOpt(QJsonObject &o, const char *k, const std::optional<int> &v) { if (v) o[QLatin1String(k)] = *v; }
void putStr(QJsonObject &o, const char *k, const QString &v) { if (!v.isEmpty()) o[QLatin1String(k)] = v; }
std::optional<double> optD(const QJsonObject &o, const char *k)
{
    return o.contains(QLatin1String(k)) ? std::optional<double>(o.value(QLatin1String(k)).toDouble()) : std::nullopt;
}
std::optional<int> optI(const QJsonObject &o, const char *k)
{
    return o.contains(QLatin1String(k)) ? std::optional<int>(o.value(QLatin1String(k)).toInt()) : std::nullopt;
}

QJsonObject woundData(const Wound &w)
{
    QJsonObject o;
    putStr(o, "label", w.label);
    o[QStringLiteral("kind")] = w.kind;
    if (w.grade >= 0) o[QStringLiteral("grade")] = w.grade;
    putStr(o, "diagnosis", w.diagnosis);
    putStr(o, "site", w.site);
    putStr(o, "side", w.side);
    putStr(o, "siteDetail", w.siteDetail);
    if (w.since.isValid()) o[QStringLiteral("since")] = w.since.toString(Qt::ISODate);
    putStr(o, "origin", w.origin);
    if (w.admissionDate.isValid()) o[QStringLiteral("admissionDate")] = w.admissionDate.toString(Qt::ISODate);
    if (w.recurrences > 0) o[QStringLiteral("recurrences")] = w.recurrences;
    putStr(o, "recurrenceFree", w.recurrenceFree);
    putStr(o, "previousCare", w.previousCare);
    putStr(o, "physician", w.physician);
    o[QStringLiteral("goal")] = w.goal;
    if (!w.limitations.isEmpty()) o[QStringLiteral("limitations")] = arr(w.limitations);
    putStr(o, "selfManagement", w.selfManagement);
    if (w.photoConsent) o[QStringLiteral("photoConsent")] = true;
    putStr(o, "notes", w.notes);
    return o;
}

void woundFromData(Wound &w, const QJsonObject &o)
{
    w.label = o.value(QStringLiteral("label")).toString();
    w.kind = o.value(QStringLiteral("kind")).toInt();
    w.grade = o.value(QStringLiteral("grade")).toInt(-1);
    w.diagnosis = o.value(QStringLiteral("diagnosis")).toString();
    w.site = o.value(QStringLiteral("site")).toString();
    w.side = o.value(QStringLiteral("side")).toString();
    w.siteDetail = o.value(QStringLiteral("siteDetail")).toString();
    w.since = QDate::fromString(o.value(QStringLiteral("since")).toString(), Qt::ISODate);
    w.origin = o.value(QStringLiteral("origin")).toString();
    w.admissionDate = QDate::fromString(o.value(QStringLiteral("admissionDate")).toString(), Qt::ISODate);
    w.recurrences = o.value(QStringLiteral("recurrences")).toInt();
    w.recurrenceFree = o.value(QStringLiteral("recurrenceFree")).toString();
    w.previousCare = o.value(QStringLiteral("previousCare")).toString();
    w.physician = o.value(QStringLiteral("physician")).toString();
    w.goal = o.value(QStringLiteral("goal")).toInt();
    w.limitations = strs(o.value(QStringLiteral("limitations")));
    w.selfManagement = o.value(QStringLiteral("selfManagement")).toString();
    w.photoConsent = o.value(QStringLiteral("photoConsent")).toBool();
    w.notes = o.value(QStringLiteral("notes")).toString();
}

QJsonObject entryData(const Entry &e)
{
    QJsonObject o;
    putOpt(o, "length", e.length);
    putOpt(o, "width", e.width);
    putOpt(o, "depth", e.depth);
    putStr(o, "pockets", e.pockets);
    if (e.granulation) o[QStringLiteral("granulation")] = e.granulation;
    if (e.epithel) o[QStringLiteral("epithel")] = e.epithel;
    if (e.fibrin) o[QStringLiteral("fibrin")] = e.fibrin;
    if (e.necrosis) o[QStringLiteral("necrosis")] = e.necrosis;
    if (!e.exposed.isEmpty()) o[QStringLiteral("exposed")] = arr(e.exposed);
    if (e.exudate >= 0) o[QStringLiteral("exudate")] = e.exudate;
    putStr(o, "exudateKind", e.exudateKind);
    if (e.odor >= 0) o[QStringLiteral("odor")] = e.odor;
    if (!e.edge.isEmpty()) o[QStringLiteral("edge")] = arr(e.edge);
    if (!e.surroundings.isEmpty()) o[QStringLiteral("surroundings")] = arr(e.surroundings);
    if (!e.infection.isEmpty()) o[QStringLiteral("infection")] = arr(e.infection);
    putOpt(o, "pain", e.pain);
    putOpt(o, "painChange", e.painChange);
    if (e.grade >= 0) o[QStringLiteral("grade")] = e.grade;
    if (e.phase >= 0) o[QStringLiteral("phase")] = e.phase;
    if (e.trend >= 0) o[QStringLiteral("trend")] = e.trend;
    putStr(o, "description", e.description);
    putStr(o, "cleansing", e.cleansing);
    putStr(o, "dressing", e.dressing);
    putStr(o, "fixation", e.fixation);
    putStr(o, "compression", e.compression);
    if (e.intervalDays > 0) o[QStringLiteral("intervalDays")] = e.intervalDays;
    if (e.nextChange.isValid()) o[QStringLiteral("nextChange")] = e.nextChange.toString(Qt::ISODate);
    putStr(o, "notes", e.notes);
    return o;
}

void entryFromData(Entry &e, const QJsonObject &o)
{
    e.length = optD(o, "length");
    e.width = optD(o, "width");
    e.depth = optD(o, "depth");
    e.pockets = o.value(QStringLiteral("pockets")).toString();
    e.granulation = o.value(QStringLiteral("granulation")).toInt();
    e.epithel = o.value(QStringLiteral("epithel")).toInt();
    e.fibrin = o.value(QStringLiteral("fibrin")).toInt();
    e.necrosis = o.value(QStringLiteral("necrosis")).toInt();
    e.exposed = strs(o.value(QStringLiteral("exposed")));
    e.exudate = o.value(QStringLiteral("exudate")).toInt(-1);
    e.exudateKind = o.value(QStringLiteral("exudateKind")).toString();
    e.odor = o.value(QStringLiteral("odor")).toInt(-1);
    e.edge = strs(o.value(QStringLiteral("edge")));
    e.surroundings = strs(o.value(QStringLiteral("surroundings")));
    e.infection = strs(o.value(QStringLiteral("infection")));
    e.pain = optI(o, "pain");
    e.painChange = optI(o, "painChange");
    e.grade = o.value(QStringLiteral("grade")).toInt(-1);
    e.phase = o.value(QStringLiteral("phase")).toInt(-1);
    e.trend = o.value(QStringLiteral("trend")).toInt(-1);
    e.description = o.value(QStringLiteral("description")).toString();
    e.cleansing = o.value(QStringLiteral("cleansing")).toString();
    e.dressing = o.value(QStringLiteral("dressing")).toString();
    e.fixation = o.value(QStringLiteral("fixation")).toString();
    e.compression = o.value(QStringLiteral("compression")).toString();
    e.intervalDays = o.value(QStringLiteral("intervalDays")).toInt();
    e.nextChange = QDate::fromString(o.value(QStringLiteral("nextChange")).toString(), Qt::ISODate);
    e.notes = o.value(QStringLiteral("notes")).toString();
}

QString json(const QJsonObject &o) { return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)); }
QJsonObject parse(const QVariant &v) { return QJsonDocument::fromJson(v.toString().toUtf8()).object(); }

Wound woundFrom(const SqlQuery &q)
{
    Wound w;
    w.id = q.value(QStringLiteral("id")).toLongLong();
    w.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    w.status = q.value(QStringLiteral("status")).toInt();
    w.closedAt = dateOf(q.value(QStringLiteral("closed_at")));
    w.onAdmission = q.value(QStringLiteral("on_admission")).toInt() != 0;
    w.created = QDateTime::fromString(q.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    w.createdBy = q.value(QStringLiteral("created_by")).toString();
    woundFromData(w, parse(q.value(QStringLiteral("data"))));
    return w;
}

Entry entryFrom(const SqlQuery &q, bool withPhoto)
{
    Entry e;
    e.id = q.value(QStringLiteral("id")).toLongLong();
    e.woundId = q.value(QStringLiteral("wound_id")).toLongLong();
    e.patientId = q.value(QStringLiteral("patient_id")).toLongLong();
    e.at = QDateTime::fromString(q.value(QStringLiteral("at")).toString(), Qt::ISODate);
    e.kind = q.value(QStringLiteral("kind")).toInt();
    e.by = q.value(QStringLiteral("by_user")).toString();
    if (withPhoto) e.photo = q.value(QStringLiteral("photo")).toByteArray();
    else if (q.value(QStringLiteral("has_photo")).toInt()) e.photo = QByteArray("1");   // Markierung: Foto vorhanden
    entryFromData(e, parse(q.value(QStringLiteral("data"))));
    return e;
}

QString joinNonEmpty(const QStringList &l, const QString &sep = QStringLiteral(", "))
{
    QStringList r;
    for (const QString &s : l) if (!s.trimmed().isEmpty()) r << s.trimmed();
    return r.join(sep);
}
} // namespace

// ====================================================================== Auswahllisten

QStringList kindNames()
{
    return { tr("Dekubitus"), tr("Ulcus cruris venosum"), tr("Ulcus cruris arteriosum"), tr("Ulcus cruris mixtum"),
             tr("Diabetisches Fußsyndrom"), tr("Operationswunde (sekundär heilend)"), tr("Traumatische Wunde"),
             tr("Verbrennung/Verbrühung"), tr("Tumorwunde"), tr("Sonstige Wunde") };
}
QString kindName(int kind) { return kindNames().value(kind, tr("Sonstige Wunde")); }

QStringList gradeNames(int kind)
{
    switch (kind) {
    case Pressure:      // Klassifikation nach EPUAP/NPIAP/PPPIA (2019)
        return { tr("Kategorie 1 – nicht wegdrückbare Rötung"), tr("Kategorie 2 – Teilverlust der Haut"),
                 tr("Kategorie 3 – Verlust der Haut"), tr("Kategorie 4 – vollständiger Gewebeverlust"),
                 tr("Keiner Kategorie zuzuordnen – Tiefe unbekannt"), tr("Vermutete tiefe Gewebeschädigung") };
    case DiabeticFoot:  // Wagner
        return { tr("Wagner 0 – Risikofuß, keine offene Läsion"), tr("Wagner 1 – oberflächliche Wunde"),
                 tr("Wagner 2 – Wunde bis Sehne oder Kapsel"), tr("Wagner 3 – Wunde bis Knochen/Gelenk"),
                 tr("Wagner 4 – Nekrose von Fußteilen"), tr("Wagner 5 – Nekrose des gesamten Fußes") };
    case Burn:
        return { tr("Grad 1"), tr("Grad 2a"), tr("Grad 2b"), tr("Grad 3") };
    default:
        return {};
    }
}

QString gradeSystem(int kind)
{
    switch (kind) {
    case Pressure: return tr("Kategorie (EPUAP/NPIAP)");
    case DiabeticFoot: return tr("Wagner-Grad");
    case Burn: return tr("Verbrennungsgrad");
    default: return {};
    }
}

QString gradeName(int kind, int grade)
{
    return grade >= 0 ? gradeNames(kind).value(grade) : QString();
}

QStringList sites()
{
    return { tr("Kreuzbein/Sakrum"), tr("Steiß"), tr("Gesäß"), tr("Sitzbein"), tr("Hüfte/Trochanter"), tr("Ferse"),
             tr("Innenknöchel"), tr("Außenknöchel"), tr("Unterschenkel"), tr("Fußrücken"), tr("Fußsohle"), tr("Zehen"),
             tr("Knie"), tr("Oberschenkel"), tr("Ellenbogen"), tr("Schulterblatt"), tr("Wirbelsäule"), tr("Hinterkopf"),
             tr("Ohr"), tr("Bauch"), tr("Brust"), tr("Rücken"), tr("Arm"), tr("Hand/Finger"), tr("Gesicht"), tr("Sonstige") };
}
QStringList sides() { return { tr("links"), tr("rechts"), tr("mittig"), tr("beidseits") }; }
QStringList origins() { return { tr("zu Hause"), tr("im Krankenhaus"), tr("in einer Pflegeeinrichtung"), tr("in der Kurzzeitpflege"), tr("unbekannt") }; }
QStringList goals() { return { tr("Abheilung"), tr("Verbesserung, Rezidivfreiheit"), tr("Symptomkontrolle (palliativ)") }; }
QStringList limitationChoices()
{
    return { tr("Schmerzen"), tr("Mobilität"), tr("Schlaf"), tr("Geruch"), tr("Exsudat/Verband"), tr("Kleidung/Schuhe"),
             tr("Körperbild"), tr("Ängste/Sorgen"), tr("Soziale Teilhabe"), tr("Juckreiz") };
}
QStringList exposedChoices() { return { tr("Sehne"), tr("Faszie"), tr("Muskel"), tr("Knochen"), tr("Gelenkkapsel") }; }
QStringList exudateAmounts() { return { tr("kein"), tr("wenig"), tr("mäßig"), tr("viel") }; }
QStringList exudateKinds() { return { tr("serös"), tr("serös-blutig"), tr("blutig"), tr("trüb"), tr("eitrig"), tr("grünlich") }; }
QStringList odors() { return { tr("kein Geruch"), tr("leichter Geruch"), tr("starker Geruch") }; }
QStringList edgeChoices()
{
    return { tr("intakt"), tr("gerötet"), tr("mazeriert"), tr("unterminiert"), tr("nekrotisch"), tr("hyperkeratotisch"),
             tr("eingerollt"), tr("ödematös"), tr("epithelisierend") };
}
QStringList surroundingChoices()
{
    return { tr("intakt"), tr("gerötet"), tr("überwärmt"), tr("geschwollen"), tr("mazeriert"), tr("trocken/schuppig"),
             tr("Ekzem"), tr("Hyperpigmentierung"), tr("Ödem"), tr("Blasen") };
}
QStringList infectionChoices()
{
    return { tr("Rötung"), tr("Überwärmung"), tr("Schwellung"), tr("zunehmender Schmerz"), tr("Funktionseinschränkung"),
             tr("eitriges Exsudat"), tr("übler Geruch"), tr("Fieber") };
}
QStringList phases() { return { tr("Exsudation/Reinigung"), tr("Granulation"), tr("Epithelisierung") }; }
QStringList trends() { return { tr("besser"), tr("unverändert"), tr("schlechter") }; }
QStringList statusNames() { return { tr("offen"), tr("abgeheilt"), tr("beendet") }; }
QString entryKindName(int kind) { return kind == DressingChange ? tr("Verbandwechsel") : tr("Wundbeurteilung"); }

// ====================================================================== Wunde

QString Wound::title() const
{
    QStringList parts{ kindName(kind) };
    if (grade >= 0) parts[0] += QStringLiteral(" (") + gradeName(kind, grade).section(QStringLiteral(" – "), 0, 0) + QLatin1Char(')');
    const QString where = joinNonEmpty({ site, side }, QStringLiteral(" "));
    if (!where.isEmpty()) parts << where;
    return (label.isEmpty() ? QString() : label + QStringLiteral(" – ")) + parts.join(QStringLiteral(", "));
}

std::optional<double> Entry::area() const
{
    if (!length || !width || *length <= 0 || *width <= 0) return std::nullopt;
    return std::round(*length * *width * 100.0) / 100.0;
}

QList<Wound> wounds(qint64 patientId, bool includeClosed)
{
    QList<Wound> l;
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM wounds WHERE patient_id = ?%1 ORDER BY status, id")
                  .arg(includeClosed ? QString() : QStringLiteral(" AND status = 0")));
    q.addBindValue(patientId);
    if (!q.exec()) return l;
    while (q.next()) l << woundFrom(q);
    return l;
}

std::optional<Wound> wound(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM wounds WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) return std::nullopt;
    return woundFrom(q);
}

int nextNumber(qint64 patientId)
{
    int n = 0;
    const QRegularExpression rx(QStringLiteral("(\\d+)\\s*$"));
    for (const Wound &w : wounds(patientId)) {
        const auto m = rx.match(w.label);
        n = std::max(n, m.hasMatch() ? m.captured(1).toInt() : 0);
    }
    return std::max<int>(n, int(wounds(patientId).size())) + 1;
}

bool saveWound(Wound &w, QString *error)
{
    if (!w.patientId) { if (error) *error = tr("Keine Person gewählt."); return false; }
    if (w.label.trimmed().isEmpty()) w.label = tr("Wunde %1").arg(nextNumber(w.patientId));
    if (w.status == Open) w.closedAt = {};
    else if (!w.closedAt.isValid()) w.closedAt = QDate::currentDate();
    if (w.onAdmission && !w.admissionDate.isValid()) w.admissionDate = QDate::currentDate();
    SqlQuery q(db());
    if (w.id) {
        q.prepare(QStringLiteral("UPDATE wounds SET status = ?, closed_at = ?, on_admission = ?, data = ?, modified_at = ? WHERE id = ?"));
        q.addBindValue(w.status);
        q.addBindValue(dateVar(w.closedAt));
        q.addBindValue(w.onAdmission ? 1 : 0);
        q.addBindValue(json(woundData(w)));
        q.addBindValue(QDateTime::currentDateTime().toString(kDt));
        q.addBindValue(w.id);
        if (!q.exec()) return failQ(q, error);
    } else {
        w.created = QDateTime::currentDateTime();
        w.createdBy = Session::user();
        q.prepare(QStringLiteral("INSERT INTO wounds (patient_id, created_at, created_by, status, closed_at, on_admission, data)"
                                 " VALUES (?,?,?,?,?,?,?)"));
        q.addBindValue(w.patientId);
        q.addBindValue(w.created.toString(kDt));
        q.addBindValue(w.createdBy);
        q.addBindValue(w.status);
        q.addBindValue(dateVar(w.closedAt));
        q.addBindValue(w.onAdmission ? 1 : 0);
        q.addBindValue(json(woundData(w)));
        if (!q.exec()) return failQ(q, error);
        w.id = q.lastInsertId().toLongLong();
    }
    Database::instance().notifyWoundsChanged(w.patientId);
    return true;
}

bool deleteWound(qint64 id, QString *error)
{
    const auto w = wound(id);
    if (!w) return false;
    SqlDb d = db();
    d.transaction();
    SqlQuery q(d);
    q.prepare(QStringLiteral("DELETE FROM wound_entries WHERE wound_id = ?"));
    q.addBindValue(id);
    if (!q.exec()) { d.rollback(); return failQ(q, error); }
    SqlQuery q2(d);
    q2.prepare(QStringLiteral("DELETE FROM wounds WHERE id = ?"));
    q2.addBindValue(id);
    if (!q2.exec()) { d.rollback(); return failQ(q2, error); }
    if (!d.commit()) { if (error) *error = d.lastError().text(); return false; }
    Database::instance().notifyWoundsChanged(w->patientId);
    return true;
}

QList<Entry> entries(qint64 woundId, bool withPhotos)
{
    QList<Entry> l;
    SqlQuery q(db());
    q.prepare(withPhotos ? QStringLiteral("SELECT * FROM wound_entries WHERE wound_id = ? ORDER BY at DESC, id DESC")
                         : QStringLiteral("SELECT id, wound_id, patient_id, at, kind, by_user, data,"
                                          " (photo IS NOT NULL AND length(photo) > 0) AS has_photo"
                                          " FROM wound_entries WHERE wound_id = ? ORDER BY at DESC, id DESC"));
    q.addBindValue(woundId);
    if (!q.exec()) return l;
    while (q.next()) l << entryFrom(q, withPhotos);
    return l;
}

std::optional<Entry> entry(qint64 id)
{
    SqlQuery q(db());
    q.prepare(QStringLiteral("SELECT * FROM wound_entries WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) return std::nullopt;
    return entryFrom(q, true);
}

bool saveEntry(Entry &e, QString *error)
{
    const auto w = wound(e.woundId);
    if (!w) { if (error) *error = tr("Die Wunde gibt es nicht mehr."); return false; }
    e.patientId = w->patientId;
    if (!e.at.isValid()) e.at = QDateTime::currentDateTime();
    if (e.by.isEmpty()) e.by = Session::user();
    const int sum = e.granulation + e.epithel + e.fibrin + e.necrosis;
    if (sum > 100) { if (error) *error = tr("Der Wundgrund ergibt zusammen %1 % – höchstens 100 % sind möglich.").arg(sum); return false; }
    if (e.intervalDays > 0 && !e.nextChange.isValid()) e.nextChange = e.at.date().addDays(e.intervalDays);
    SqlQuery q(db());
    if (e.id) {
        q.prepare(QStringLiteral("UPDATE wound_entries SET at = ?, kind = ?, by_user = ?, data = ?, photo = ?, modified_at = ? WHERE id = ?"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO wound_entries (at, kind, by_user, data, photo, modified_at, wound_id, patient_id)"
                                 " VALUES (?,?,?,?,?,?,?,?)"));
    }
    q.addBindValue(e.at.toString(kDt));
    q.addBindValue(e.kind);
    q.addBindValue(e.by);
    q.addBindValue(json(entryData(e)));
    q.addBindValue(e.photo.isEmpty() ? QVariant() : QVariant(e.photo));
    q.addBindValue(QDateTime::currentDateTime().toString(kDt));
    if (e.id) q.addBindValue(e.id);
    else { q.addBindValue(e.woundId); q.addBindValue(e.patientId); }
    if (!q.exec()) return failQ(q, error);
    if (!e.id) e.id = q.lastInsertId().toLongLong();
    // Klassifikation der Wunde nachführen (z. B. Dekubitus jetzt Kategorie 3)
    if (e.kind == Assessment && e.grade >= 0 && e.grade != w->grade) {
        Wound c = *w;
        c.grade = e.grade;
        saveWound(c);
    }
    Database::instance().notifyWoundsChanged(e.patientId);
    return true;
}

bool deleteEntry(qint64 id, QString *error)
{
    const auto e = entry(id);
    if (!e) return false;
    SqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM wound_entries WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return failQ(q, error);
    Database::instance().notifyWoundsChanged(e->patientId);
    return true;
}

// ====================================================================== Auswertung

QString sizeText(const Entry &e)
{
    if (!e.length && !e.width && !e.depth) return {};
    QStringList d;
    for (const auto &v : { e.length, e.width, e.depth }) d << (v ? num(*v) : QStringLiteral("–"));
    QString t = d.join(QStringLiteral(" × ")) + QStringLiteral(" cm");
    if (const auto a = e.area()) t += QStringLiteral(" (") + num(*a) + QStringLiteral(" cm²)");
    return t;
}

QString woundSummary(const Wound &w)
{
    QStringList l{ w.title() };
    if (w.since.isValid()) l << tr("seit %1").arg(dmy(w.since));
    if (w.onAdmission) l << tr("bei Aufnahme vorhanden (%1)").arg(dmy(w.admissionDate));
    if (w.status != Open) l << QStringLiteral("%1 %2").arg(statusNames().value(w.status), dmy(w.closedAt));
    return l.join(QStringLiteral(", "));
}

QString entrySummary(const Entry &e)
{
    QStringList l;
    if (const QString s = sizeText(e); !s.isEmpty()) l << s;
    QStringList ground;
    if (e.granulation) ground << tr("Granulation %1 %").arg(e.granulation);
    if (e.epithel) ground << tr("Epithel %1 %").arg(e.epithel);
    if (e.fibrin) ground << tr("Fibrin %1 %").arg(e.fibrin);
    if (e.necrosis) ground << tr("Nekrose %1 %").arg(e.necrosis);
    if (!ground.isEmpty()) l << ground.join(QStringLiteral(", "));
    if (e.exudate >= 0) l << tr("Exsudat %1").arg(joinNonEmpty({ exudateAmounts().value(e.exudate), e.exudateKind }, QStringLiteral(", ")));
    if (!e.infection.isEmpty()) l << tr("Infektionszeichen: %1").arg(e.infection.join(QStringLiteral(", ")));
    if (e.pain) l << tr("Schmerz %1/10").arg(*e.pain);
    if (!e.description.isEmpty()) l << e.description;
    if (e.trend >= 0) l << tr("Tendenz: %1").arg(trends().value(e.trend));
    if (!e.dressing.isEmpty()) l << tr("Versorgung: %1").arg(e.dressing);
    return l.join(QStringLiteral(" · "));
}

std::optional<Entry> lastAssessment(const QList<Entry> &list)
{
    for (const Entry &e : list) if (e.kind == Assessment) return e;    // Liste: neueste zuerst
    return std::nullopt;
}

QDate nextAssessmentDue(const Wound &w, const QList<Entry> &list)
{
    if (w.status != Open) return {};
    if (const auto a = lastAssessment(list)) return a->at.date().addDays(kAssessmentDays);
    return w.created.isValid() ? w.created.date() : QDate::currentDate();   // noch keine Beurteilung: sofort
}

QList<Notice> notices(const Wound &w, const QList<Entry> &list, const QDate &today)
{
    QList<Notice> n;
    if (w.status != Open) return n;
    const QString name = w.label.isEmpty() ? kindName(w.kind) : w.label;
    const auto last = lastAssessment(list);
    if (!last)
        n << Notice{ true, tr("%1: noch keine Wundbeurteilung erfasst.").arg(name) };
    else if (last->at.date().daysTo(today) > kAssessmentDays)
        n << Notice{ true, tr("%1: letzte vollständige Wundbeurteilung am %2 – spätestens alle 4 Wochen neu beurteilen.")
                               .arg(name, dmy(last->at.date())) };
    if (!list.isEmpty()) {
        const Entry &e = list.first();
        const bool pus = e.exudateKind.contains(tr("eitrig")) || e.infection.contains(tr("eitriges Exsudat"));
        if (e.infection.size() >= 2 || pus)
            n << Notice{ true, tr("%1: Infektionszeichen (%2) – bitte ärztlich abklären.")
                                   .arg(name, (e.infection + (pus && !e.infection.contains(tr("eitriges Exsudat")) ? QStringList{ tr("eitriges Exsudat") } : QStringList{}))
                                                  .join(QStringLiteral(", "))) };
        if (e.trend == 2) n << Notice{ true, tr("%1: Verschlechterung beim letzten Eintrag – Versorgung und Ursachen überprüfen.").arg(name) };
    }
    // Fläche gegenüber der vorigen Beurteilung um mehr als 20 % größer
    QList<Entry> assessed;
    for (const Entry &e : list) if (e.kind == Assessment && e.area()) assessed << e;
    if (assessed.size() >= 2 && *assessed[0].area() > *assessed[1].area() * 1.2)
        n << Notice{ true, tr("%1: Wundfläche von %2 auf %3 cm² gewachsen.").arg(name, num(*assessed[1].area()), num(*assessed[0].area())) };
    // nächster Verbandwechsel
    for (const Entry &e : list) {
        if (!e.nextChange.isValid()) continue;
        if (e.nextChange < today) n << Notice{ true, tr("%1: Verbandwechsel war am %2 fällig.").arg(name, dmy(e.nextChange)) };
        else if (e.nextChange == today) n << Notice{ false, tr("%1: Verbandwechsel heute fällig.").arg(name) };
        break;      // nur der jüngste Eintrag mit Termin zählt
    }
    return n;
}

QList<Notice> patientNotices(qint64 patientId, const QDate &today)
{
    QList<Notice> n;
    for (const Wound &w : wounds(patientId, false)) n << notices(w, entries(w.id, false), today);
    return n;
}

QByteArray preparePhoto(const QByteArray &imageData, QString *error)
{
    QBuffer buf;
    buf.setData(imageData);
    buf.open(QIODevice::ReadOnly);
    QImageReader r(&buf);
    r.setAutoTransform(true);           // Ausrichtung aus den EXIF-Angaben der Kamera
    QImage img = r.read();
    if (img.isNull()) { if (error) *error = tr("Das Bild lässt sich nicht lesen."); return {}; }
    if (img.width() > 1024 || img.height() > 1024) img = img.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray out;
    QBuffer ob(&out);
    ob.open(QIODevice::WriteOnly);
    img.convertToFormat(QImage::Format_RGB32).save(&ob, "JPG", 80);
    return out;
}

// ====================================================================== Ausgabe

// Verlauf der Wundfläche als kleines Diagramm (Bericht, Chronik am Computer)
QImage areaChart(const QList<Entry> &list)
{
    QList<QPair<QDateTime, double>> pts;
    for (const Entry &e : list) if (e.area()) pts.prepend({ e.at, *e.area() });   // chronologisch
    if (pts.size() < 2) return {};
    const int W = 1400, H = 420, L = 110, R = 30, T = 30, B = 70;
    QImage img(W, H, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    double maxA = 0;
    for (const auto &pt : pts) maxA = std::max(maxA, pt.second);
    maxA = std::ceil(maxA * 1.15);
    const qint64 t0 = pts.first().first.toSecsSinceEpoch(), t1 = std::max(t0 + 1, pts.last().first.toSecsSinceEpoch());
    auto X = [&](const QDateTime &t) { return L + (W - L - R) * double(t.toSecsSinceEpoch() - t0) / double(t1 - t0); };
    auto Y = [&](double a) { return T + (H - T - B) * (1.0 - a / maxA); };
    QFont f(QStringLiteral("Sans")); f.setPixelSize(22); p.setFont(f);
    p.setPen(QColor(0xdd, 0xe3, 0xe8));
    for (int i = 0; i <= 4; ++i) {
        const double a = maxA * i / 4.0;
        p.drawLine(QPointF(L, Y(a)), QPointF(W - R, Y(a)));
        p.setPen(QColor(0x55, 0x60, 0x6b));
        p.drawText(QRectF(0, Y(a) - 14, L - 12, 28), Qt::AlignRight | Qt::AlignVCenter, num(a, a < 10 ? 1 : 0));
        p.setPen(QColor(0xdd, 0xe3, 0xe8));
    }
    p.setPen(QColor(0x55, 0x60, 0x6b));
    p.drawText(QRectF(L, H - B + 10, 300, 30), Qt::AlignLeft, QLocale().toString(pts.first().first.date(), QStringLiteral("dd.MM.yyyy")));
    p.drawText(QRectF(W - R - 300, H - B + 10, 300, 30), Qt::AlignRight, QLocale().toString(pts.last().first.date(), QStringLiteral("dd.MM.yyyy")));
    p.drawText(QRectF(L, H - 34, W - L - R, 30), Qt::AlignCenter, tr("Wundfläche (Länge × Breite) in cm²"));
    QPainterPath path;
    for (int i = 0; i < pts.size(); ++i) {
        const QPointF pt(X(pts[i].first), Y(pts[i].second));
        if (i == 0) path.moveTo(pt); else path.lineTo(pt);
    }
    p.setPen(QPen(QColor(0x1f, 0x78, 0xb9), 4));
    p.drawPath(path);
    p.setBrush(QColor(0x1f, 0x78, 0xb9));
    for (const auto &pt : pts) p.drawEllipse(QPointF(X(pt.first), Y(pt.second)), 7, 7);
    p.end();
    return img;
}

namespace {
QString imgTag(const QImage &img, int widthPx)
{
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\">").arg(QString::fromLatin1(png.toBase64())).arg(widthPx);
}

QString esc(const QString &s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); }
QString row(const QString &k, const QString &v)
{
    return v.trimmed().isEmpty() ? QString() : QStringLiteral("<tr><td width=\"28%\" valign=\"top\"><b>%1</b></td><td>%2</td></tr>").arg(esc(k), esc(v));
}

QStringList entryLines(const Entry &e)
{
    QStringList l;
    if (const QString s = sizeText(e); !s.isEmpty()) l << tr("Größe (L × B × T): %1").arg(s);
    if (!e.pockets.isEmpty()) l << tr("Taschen/Unterminierung: %1").arg(e.pockets);
    QStringList ground;
    if (e.granulation) ground << tr("Granulation %1 %").arg(e.granulation);
    if (e.epithel) ground << tr("Epithel %1 %").arg(e.epithel);
    if (e.fibrin) ground << tr("Fibrin/Belag %1 %").arg(e.fibrin);
    if (e.necrosis) ground << tr("Nekrose %1 %").arg(e.necrosis);
    if (!e.exposed.isEmpty()) ground << tr("freiliegend: %1").arg(e.exposed.join(QStringLiteral(", ")));
    if (!ground.isEmpty()) l << tr("Wundgrund: %1").arg(ground.join(QStringLiteral(", ")));
    if (e.exudate >= 0 || !e.exudateKind.isEmpty())
        l << tr("Exsudat: %1").arg(joinNonEmpty({ exudateAmounts().value(e.exudate), e.exudateKind }));
    if (e.odor >= 0) l << tr("Geruch: %1").arg(odors().value(e.odor));
    if (!e.edge.isEmpty()) l << tr("Wundrand: %1").arg(e.edge.join(QStringLiteral(", ")));
    if (!e.surroundings.isEmpty()) l << tr("Wundumgebung: %1").arg(e.surroundings.join(QStringLiteral(", ")));
    if (!e.infection.isEmpty()) l << tr("Infektionszeichen: %1").arg(e.infection.join(QStringLiteral(", ")));
    QStringList pain;
    if (e.pain) pain << tr("in Ruhe %1").arg(*e.pain);
    if (e.painChange) pain << tr("beim Verbandwechsel %1").arg(*e.painChange);
    if (!pain.isEmpty()) l << tr("Schmerz (0–10): %1").arg(pain.join(QStringLiteral(", ")));
    if (e.phase >= 0) l << tr("Heilungsphase: %1").arg(phases().value(e.phase));
    if (e.trend >= 0) l << tr("Tendenz: %1").arg(trends().value(e.trend));
    if (!e.description.isEmpty()) l << e.description;
    QStringList care;
    if (!e.cleansing.isEmpty()) care << tr("Reinigung: %1").arg(e.cleansing);
    if (!e.dressing.isEmpty()) care << tr("Wundauflage: %1").arg(e.dressing);
    if (!e.fixation.isEmpty()) care << tr("Fixierung: %1").arg(e.fixation);
    if (!e.compression.isEmpty()) care << tr("Kompression/Druckentlastung: %1").arg(e.compression);
    if (e.intervalDays > 0) care << tr("Wechsel alle %1 Tage").arg(e.intervalDays);
    if (e.nextChange.isValid()) care << tr("nächster Wechsel %1").arg(dmy(e.nextChange));
    if (!care.isEmpty()) l << tr("Versorgung: %1").arg(care.join(QStringLiteral("; ")));
    if (!e.notes.isEmpty()) l << tr("Bemerkung: %1").arg(e.notes);
    return l;
}
} // namespace

QString reportHtml(const Patient &, const Wound &w, const QList<Entry> &list, bool withPhotos)
{
    QString h = QStringLiteral("<h3>%1</h3>").arg(esc(w.title()));
    h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"3\">");
    h += row(tr("Wundart"), kindName(w.kind));
    if (w.grade >= 0) h += row(gradeSystem(w.kind), gradeName(w.kind, w.grade));
    h += row(tr("Medizinische Diagnose"), w.diagnosis);
    h += row(tr("Lokalisation"), joinNonEmpty({ joinNonEmpty({ w.site, w.side }, QStringLiteral(" ")), w.siteDetail }));
    QString since = dmy(w.since);
    if (w.since.isValid()) {
        const qint64 days = w.since.daysTo(w.status == Open ? QDate::currentDate() : w.closedAt);
        since += QStringLiteral(" – ") + (days >= 60 ? tr("Wunddauer %1 Monate").arg(days / 30) : tr("Wunddauer %1 Tage").arg(days));
    }
    h += row(tr("Entstanden"), joinNonEmpty({ since, w.origin }));
    h += row(tr("Bei Aufnahme"), w.onAdmission ? tr("vorhanden (Aufnahme am %1)").arg(dmy(w.admissionDate)) : QString());
    h += row(tr("Rezidive"), w.recurrences > 0 ? joinNonEmpty({ QString::number(w.recurrences), w.recurrenceFree }) : QString());
    h += row(tr("Bisherige Maßnahmen"), w.previousCare);
    h += row(tr("Behandelnd"), w.physician);
    h += row(tr("Therapieziel"), goals().value(w.goal));
    h += row(tr("Einschränkungen"), w.limitations.join(QStringLiteral(", ")));
    h += row(tr("Selbstmanagement"), w.selfManagement);
    h += row(tr("Fotodokumentation"), w.photoConsent ? tr("Einwilligung liegt vor") : tr("keine Einwilligung"));
    h += row(tr("Status"), joinNonEmpty({ statusNames().value(w.status), dmy(w.closedAt) }, QStringLiteral(" ")));
    h += row(tr("Bemerkung"), w.notes);
    h += QStringLiteral("</table>");
    const QImage chart = areaChart(list);
    if (!chart.isNull()) h += QStringLiteral("<p>%1</p>").arg(imgTag(chart, 640));
    h += QStringLiteral("<h4>%1</h4>").arg(tr("Chronik (neueste zuerst)"));
    if (list.isEmpty()) h += QStringLiteral("<p>%1</p>").arg(tr("Noch keine Einträge."));
    h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" border=\"1\" style=\"border-collapse: collapse; border-color: #8899aa;\">");
    for (const Entry &e : list) {
        QString cell = QStringLiteral("<b>%1</b><br>%2").arg(esc(QLocale().toString(e.at, QStringLiteral("dd.MM.yyyy HH:mm"))),
                                                              esc(entryKindName(e.kind)));
        if (!e.by.isEmpty()) cell += QStringLiteral("<br><small>%1</small>").arg(esc(tr("Hz. %1").arg(e.by)));
        QString body = esc(entryLines(e).join(QLatin1Char('\n')));
        if (withPhotos && !e.photo.isEmpty() && e.photo.size() > 8) {
            QImage img;
            if (img.loadFromData(e.photo)) body += QStringLiteral("<br>") + imgTag(img.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation), 220);
        }
        h += QStringLiteral("<tr><td valign=\"top\" width=\"18%\" bgcolor=\"#f1f4f7\">%1</td><td valign=\"top\">%2</td></tr>").arg(cell, body);
    }
    h += QStringLiteral("</table>");
    h += QStringLiteral("<p><small>%1</small></p>").arg(esc(tr("Dokumentation nach dem Expertenstandard „Pflege von Menschen mit chronischen Wunden“: "
                                                                "vollständige Wundbeurteilung spätestens alle 4 Wochen und bei Veränderung, jeder "
                                                                "Verbandwechsel mit Datum, Handzeichen, Beschreibung und Tendenz. Ein Foto ersetzt die "
                                                                "schriftliche Beschreibung nicht.")));
    return h;
}

bool toPdf(const Patient &p, const Wound &w, const QString &path)
{
    return ReportPrinter::htmlReportToPdf(path, p, tr("Wunddokumentation"), reportHtml(p, w, entries(w.id, true)), false);
}

bool toXlsx(const Patient &p, const Wound &w, const QString &path, QString *error)
{
    XlsxWriter x;
    x.setTitle(QStringLiteral("VitaParms – %1 %2").arg(tr("Wunddokumentation"), p.displayName()));
    const int sh = x.addSheet(tr("Wunddokumentation").left(31));
    x.setLandscape(sh, true);
    x.setCell(sh, 0, 0, QStringLiteral("VitaParms – %1").arg(tr("Wunddokumentation")), XlsxWriter::Title);
    x.setCell(sh, 1, 0, p.displayName() + QStringLiteral(" – ") + w.title());
    x.setCell(sh, 2, 0, woundSummary(w));
    const QStringList heads = { tr("Zeitpunkt"), tr("Art"), tr("Hz."), tr("Länge cm"), tr("Breite cm"), tr("Tiefe cm"), tr("Fläche cm²"),
                                tr("Granulation %"), tr("Epithel %"), tr("Fibrin %"), tr("Nekrose %"), tr("Exsudat"), tr("Geruch"),
                                tr("Wundrand"), tr("Wundumgebung"), tr("Infektionszeichen"), tr("Schmerz"), tr("Tendenz"),
                                tr("Versorgung"), tr("nächster Wechsel"), tr("Bemerkung") };
    const int hr = 4;
    for (int c = 0; c < heads.size(); ++c) { x.setCell(sh, hr, c, heads[c], XlsxWriter::Header); x.setColumnWidth(sh, c, c >= 13 ? 24 : 11); }
    x.freezeRows(sh, hr + 1);
    int r = hr + 1;
    for (const Entry &e : entries(w.id, false)) {
        int c = 0;
        x.setCell(sh, r, c++, e.at, XlsxWriter::DateTime);
        x.setCell(sh, r, c++, entryKindName(e.kind));
        x.setCell(sh, r, c++, e.by);
        for (const auto &v : { e.length, e.width, e.depth, e.area() }) x.setCell(sh, r, c++, v ? QVariant(*v) : QVariant(), XlsxWriter::Decimal1);
        for (int v : { e.granulation, e.epithel, e.fibrin, e.necrosis }) x.setCell(sh, r, c++, v ? QVariant(v) : QVariant(), XlsxWriter::Integer);
        x.setCell(sh, r, c++, joinNonEmpty({ exudateAmounts().value(e.exudate), e.exudateKind }));
        x.setCell(sh, r, c++, e.odor >= 0 ? odors().value(e.odor) : QString());
        x.setCell(sh, r, c++, e.edge.join(QStringLiteral(", ")), XlsxWriter::Wrap);
        x.setCell(sh, r, c++, e.surroundings.join(QStringLiteral(", ")), XlsxWriter::Wrap);
        x.setCell(sh, r, c++, e.infection.join(QStringLiteral(", ")), XlsxWriter::Wrap);
        x.setCell(sh, r, c++, e.pain ? QVariant(*e.pain) : QVariant(), XlsxWriter::Integer);
        x.setCell(sh, r, c++, e.trend >= 0 ? trends().value(e.trend) : QString());
        x.setCell(sh, r, c++, joinNonEmpty({ e.cleansing, e.dressing, e.fixation, e.compression }, QStringLiteral("; ")), XlsxWriter::Wrap);
        x.setCell(sh, r, c++, e.nextChange.isValid() ? QVariant(e.nextChange) : QVariant(), XlsxWriter::Date);
        x.setCell(sh, r, c++, joinNonEmpty({ e.description, e.pockets, e.notes }, QStringLiteral("; ")), XlsxWriter::Wrap);
        ++r;
    }
    return x.save(path, error);
}

QString admissionHtml(qint64 patientId)
{
    QStringList l;
    for (const Wound &w : wounds(patientId))
        if (w.onAdmission) {
            QString line = esc(woundSummary(w));
            const QList<Entry> list = entries(w.id, false);
            for (auto it = list.crbegin(); it != list.crend(); ++it)   // erste Beurteilung = Aufnahmebefund
                if (it->kind == Assessment) {
                    if (const QString s = sizeText(*it); !s.isEmpty()) line += QStringLiteral(" – ") + esc(s);
                    break;
                }
            l << QStringLiteral("<li>%1</li>").arg(line);
        }
    if (l.isEmpty()) return {};
    return QStringLiteral("<h3>%1</h3><ul>%2</ul>").arg(esc(tr("Wunden bei Aufnahme")), l.join(QString()));
}

} // namespace Wounds
