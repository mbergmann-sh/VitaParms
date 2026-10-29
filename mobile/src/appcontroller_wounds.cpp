// VitaParms Tablet – Wunddokumentation (Teil von AppController). Fachlogik wie am Computer:
// Wounds (Wundanamnese, Chronik aus Wundbeurteilungen und Verbandwechseln, Hinweise, Fotos,
// Flächenverlauf, PDF/Excel). Fotos: Kamera-App oder Datei → verkleinertes JPEG.
// Bilder für QML über image://wound/<Eintrag-ID | draft | chart-<Wunden-ID>>?r=<Revision>
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "database.h"
#include "dbtransfer.h"
#include "wounds.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>

using namespace Wounds;

namespace {
QString iso(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QDate fromIso(const QVariant &v) { return QDate::fromString(v.toString().left(10), Qt::ISODate); }
QString dmy(const QDate &d) { return d.isValid() ? QLocale().toString(d, QStringLiteral("dd.MM.yyyy")) : QString(); }
QString dmyhm(const QDateTime &d) { return d.isValid() ? QLocale().toString(d, QStringLiteral("dd.MM.yyyy HH:mm")) : QString(); }

std::optional<double> num(const QVariant &v)
{
    QString s = v.toString().trimmed();
    if (s.isEmpty()) return std::nullopt;
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double d = s.toDouble(&ok);
    if (!ok || d < 0) return std::nullopt;
    return d;
}
QString numText(const std::optional<double> &d) { return d ? QLocale().toString(*d, 'f', 1) : QString(); }
QStringList strings(const QVariant &v) { return v.toStringList(); }
int intOr(const QVariant &v, int def) { bool ok = false; const int i = v.toInt(&ok); return ok ? i : def; }

QVariantList notesList(const QList<Notice> &n)
{
    QVariantList l;
    for (const Notice &x : n) l << QVariantMap{ { QStringLiteral("text"), x.text }, { QStringLiteral("warn"), x.warn } };
    return l;
}
} // namespace

QVariantMap AppController::woundChoices()
{
    QVariantList grades, systems;
    for (int k = 0; k < KindCount; ++k) {
        grades << QVariant(gradeNames(k));
        systems << gradeSystem(k);
    }
    return {
        { QStringLiteral("kinds"), kindNames() }, { QStringLiteral("grades"), grades }, { QStringLiteral("gradeSystems"), systems },
        { QStringLiteral("sites"), sites() }, { QStringLiteral("sides"), sides() }, { QStringLiteral("origins"), origins() },
        { QStringLiteral("goals"), goals() }, { QStringLiteral("limitations"), limitationChoices() },
        { QStringLiteral("exposed"), exposedChoices() }, { QStringLiteral("exudate"), exudateAmounts() },
        { QStringLiteral("exudateKinds"), exudateKinds() }, { QStringLiteral("odors"), odors() },
        { QStringLiteral("edges"), edgeChoices() }, { QStringLiteral("surroundings"), surroundingChoices() },
        { QStringLiteral("infection"), infectionChoices() }, { QStringLiteral("phases"), phases() },
        { QStringLiteral("trends"), trends() }, { QStringLiteral("statuses"), statusNames() },
        { QStringLiteral("entryKinds"), QStringList{ entryKindName(Assessment), entryKindName(DressingChange) } },
    };
}

QVariantList AppController::woundList(qint64 pid, bool includeClosed)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    for (const Wound &w : wounds(pid, includeClosed)) {
        const QList<Entry> list = entries(w.id, false);
        const QList<Notice> n = w.status == Open ? notices(w, list) : QList<Notice>{};
        bool warn = false;
        for (const Notice &x : n) warn |= x.warn;
        QString last;
        if (!list.isEmpty())
            last = tr("Letzter Eintrag: %1, %2").arg(entryKindName(list.first().kind), dmyhm(list.first().at));
        const auto la = lastAssessment(list);
        l << QVariantMap{
            { QStringLiteral("id"), w.id }, { QStringLiteral("title"), w.title() }, { QStringLiteral("summary"), woundSummary(w) },
            { QStringLiteral("status"), w.status }, { QStringLiteral("statusText"), statusNames().value(w.status) },
            { QStringLiteral("onAdmission"), w.onAdmission }, { QStringLiteral("admissionText"), dmy(w.admissionDate) },
            { QStringLiteral("notices"), notesList(n) }, { QStringLiteral("warn"), warn }, { QStringLiteral("last"), last },
            { QStringLiteral("size"), la ? sizeText(*la) : QString() }, { QStringLiteral("count"), list.size() },
            { QStringLiteral("nextDue"), w.status == Open ? dmy(nextAssessmentDue(w, list)) : QString() },
            { QStringLiteral("photoConsent"), w.photoConsent }, { QStringLiteral("hasChart"), !areaChart(list).isNull() },
        };
    }
    return l;
}

QVariantMap AppController::woundData(qint64 pid, qint64 id, bool onAdmission)
{
    Wound w;
    if (id) {
        if (const auto x = wound(id)) w = *x;
    } else {
        w.patientId = pid;
        w.onAdmission = onAdmission;
        if (onAdmission) {
            const auto adm = Database::instance().admissionVitals(pid);
            w.admissionDate = adm ? adm->measuredAt.date() : QDate::currentDate();
        }
    }
    return {
        { QStringLiteral("id"), w.id }, { QStringLiteral("label"), w.label },
        { QStringLiteral("labelHint"), tr("z. B. Wunde %1").arg(w.id ? 1 : nextNumber(pid)) },
        { QStringLiteral("kind"), w.kind }, { QStringLiteral("grade"), w.grade }, { QStringLiteral("diagnosis"), w.diagnosis },
        { QStringLiteral("site"), w.site }, { QStringLiteral("side"), w.side }, { QStringLiteral("siteDetail"), w.siteDetail },
        { QStringLiteral("since"), iso(w.since) }, { QStringLiteral("origin"), w.origin }, { QStringLiteral("onAdmission"), w.onAdmission },
        { QStringLiteral("admissionDate"), iso(w.admissionDate.isValid() ? w.admissionDate : QDate::currentDate()) },
        { QStringLiteral("recurrences"), w.recurrences }, { QStringLiteral("recurrenceFree"), w.recurrenceFree },
        { QStringLiteral("previousCare"), w.previousCare }, { QStringLiteral("physician"), w.physician },
        { QStringLiteral("goal"), w.goal }, { QStringLiteral("limitations"), w.limitations },
        { QStringLiteral("selfManagement"), w.selfManagement }, { QStringLiteral("photoConsent"), w.photoConsent },
        { QStringLiteral("status"), w.status }, { QStringLiteral("closedAt"), iso(w.closedAt.isValid() ? w.closedAt : QDate::currentDate()) },
        { QStringLiteral("notes"), w.notes }, { QStringLiteral("title"), w.id ? w.title() : QString() },
        { QStringLiteral("createdText"), w.id ? tr("angelegt am %1 von %2").arg(dmyhm(w.created), w.createdBy) : QString() },
    };
}

qint64 AppController::saveWound(qint64 pid, const QVariantMap &m)
{
    if (!loggedIn() || !pid) return 0;
    Wound w;
    if (const qint64 id = m.value(QStringLiteral("id")).toLongLong()) {
        const auto x = wound(id);
        if (!x) { setError(tr("Die Wunde gibt es nicht mehr.")); return 0; }
        w = *x;
    }
    w.patientId = pid;
    w.label = m.value(QStringLiteral("label")).toString().trimmed();
    w.kind = qBound(0, intOr(m.value(QStringLiteral("kind")), 0), int(KindCount) - 1);
    w.grade = gradeNames(w.kind).isEmpty() ? -1 : intOr(m.value(QStringLiteral("grade")), -1);
    w.diagnosis = m.value(QStringLiteral("diagnosis")).toString().trimmed();
    w.site = m.value(QStringLiteral("site")).toString().trimmed();
    w.side = m.value(QStringLiteral("side")).toString().trimmed();
    w.siteDetail = m.value(QStringLiteral("siteDetail")).toString().trimmed();
    w.since = fromIso(m.value(QStringLiteral("since")));
    w.origin = m.value(QStringLiteral("origin")).toString().trimmed();
    w.onAdmission = m.value(QStringLiteral("onAdmission")).toBool();
    w.admissionDate = w.onAdmission ? fromIso(m.value(QStringLiteral("admissionDate"))) : QDate();
    w.recurrences = qMax(0, m.value(QStringLiteral("recurrences")).toInt());
    w.recurrenceFree = m.value(QStringLiteral("recurrenceFree")).toString().trimmed();
    w.previousCare = m.value(QStringLiteral("previousCare")).toString().trimmed();
    w.physician = m.value(QStringLiteral("physician")).toString().trimmed();
    w.goal = qBound(0, m.value(QStringLiteral("goal")).toInt(), int(goals().size()) - 1);
    w.limitations = strings(m.value(QStringLiteral("limitations")));
    w.selfManagement = m.value(QStringLiteral("selfManagement")).toString().trimmed();
    w.photoConsent = m.value(QStringLiteral("photoConsent")).toBool();
    w.status = qBound(0, m.value(QStringLiteral("status")).toInt(), int(statusNames().size()) - 1);
    w.closedAt = w.status != Open ? fromIso(m.value(QStringLiteral("closedAt"))) : QDate();
    w.notes = m.value(QStringLiteral("notes")).toString().trimmed();
    if (w.site.isEmpty()) { setError(tr("Bitte die Lokalisation der Wunde angeben.")); return 0; }
    QString err;
    if (!Wounds::saveWound(w, &err)) { setError(err); return 0; }
    return w.id;
}

bool AppController::deleteWound(qint64 id)
{
    QString err;
    if (!Wounds::deleteWound(id, &err)) { setError(err.isEmpty() ? tr("Die Wunde konnte nicht gelöscht werden.") : err); return false; }
    return true;
}

bool AppController::setWoundHealed(qint64 id)
{
    auto w = wound(id);
    if (!w) return false;
    w->status = Healed;
    w->closedAt = QDate::currentDate();
    QString err;
    if (!Wounds::saveWound(*w, &err)) { setError(err); return false; }
    return true;
}

QVariantList AppController::woundEntries(qint64 woundId)
{
    QVariantList l;
    if (!loggedIn() || !woundId) return l;
    const auto w = wound(woundId);
    for (const Entry &e : entries(woundId, false)) {
        QStringList more;
        if (!e.pockets.isEmpty()) more << tr("Taschen: %1").arg(e.pockets);
        if (!e.exposed.isEmpty()) more << tr("Freiliegend: %1").arg(e.exposed.join(QStringLiteral(", ")));
        if (e.odor >= 0) more << tr("Geruch: %1").arg(odors().value(e.odor));
        if (!e.edge.isEmpty()) more << tr("Wundrand: %1").arg(e.edge.join(QStringLiteral(", ")));
        if (!e.surroundings.isEmpty()) more << tr("Umgebung: %1").arg(e.surroundings.join(QStringLiteral(", ")));
        if (e.phase >= 0) more << tr("Phase: %1").arg(phases().value(e.phase));
        if (e.grade >= 0 && w) more << gradeName(w->kind, e.grade);
        if (!e.cleansing.isEmpty()) more << tr("Reinigung: %1").arg(e.cleansing);
        if (!e.fixation.isEmpty()) more << tr("Fixierung: %1").arg(e.fixation);
        if (!e.compression.isEmpty()) more << tr("Kompression/Entlastung: %1").arg(e.compression);
        if (e.nextChange.isValid()) more << tr("Nächster Verbandwechsel: %1").arg(dmy(e.nextChange));
        if (!e.notes.isEmpty()) more << e.notes;
        l << QVariantMap{
            { QStringLiteral("id"), e.id }, { QStringLiteral("kind"), e.kind }, { QStringLiteral("kindText"), entryKindName(e.kind) },
            { QStringLiteral("at"), dmyhm(e.at) }, { QStringLiteral("by"), e.by }, { QStringLiteral("size"), sizeText(e) },
            { QStringLiteral("summary"), entrySummary(e) }, { QStringLiteral("details"), more },
            { QStringLiteral("hasPhoto"), !e.photo.isEmpty() }, { QStringLiteral("trend"), e.trend },
        };
    }
    return l;
}

QVariantMap AppController::woundEntryData(qint64 woundId, qint64 entryId, int kind)
{
    Entry e;
    const auto w = wound(woundId);
    if (entryId) {
        if (const auto x = entry(entryId)) e = *x;
    } else {
        e.kind = kind == DressingChange ? DressingChange : Assessment;
        e.at = QDateTime::currentDateTime();
        if (w && e.kind == Assessment) e.grade = w->grade;
        // Versorgung vom letzten Eintrag übernehmen (meist unverändert)
        const QList<Entry> list = entries(woundId, false);
        if (!list.isEmpty()) {
            const Entry &last = list.first();
            e.cleansing = last.cleansing;
            e.dressing = last.dressing;
            e.fixation = last.fixation;
            e.compression = last.compression;
            e.intervalDays = last.intervalDays;
        }
        if (e.intervalDays > 0) e.nextChange = e.at.date().addDays(e.intervalDays);
    }
    // Foto im Entwurf: vorhandenes Foto des Eintrags
    m_woundPhoto = e.photo;
    ++m_woundPhotoRevision;
    return {
        { QStringLiteral("id"), e.id }, { QStringLiteral("kind"), e.kind }, { QStringLiteral("date"), iso(e.at.date()) },
        { QStringLiteral("time"), e.at.time().toString(QStringLiteral("HH:mm")) },
        { QStringLiteral("length"), numText(e.length) }, { QStringLiteral("width"), numText(e.width) }, { QStringLiteral("depth"), numText(e.depth) },
        { QStringLiteral("pockets"), e.pockets },
        { QStringLiteral("granulation"), e.granulation }, { QStringLiteral("epithel"), e.epithel },
        { QStringLiteral("fibrin"), e.fibrin }, { QStringLiteral("necrosis"), e.necrosis },
        { QStringLiteral("exposed"), e.exposed }, { QStringLiteral("exudate"), e.exudate }, { QStringLiteral("exudateKind"), e.exudateKind },
        { QStringLiteral("odor"), e.odor }, { QStringLiteral("edge"), e.edge }, { QStringLiteral("surroundings"), e.surroundings },
        { QStringLiteral("infection"), e.infection },
        { QStringLiteral("pain"), e.pain ? QString::number(*e.pain) : QString() },
        { QStringLiteral("painChange"), e.painChange ? QString::number(*e.painChange) : QString() },
        { QStringLiteral("grade"), e.grade }, { QStringLiteral("phase"), e.phase }, { QStringLiteral("trend"), e.trend },
        { QStringLiteral("description"), e.description }, { QStringLiteral("cleansing"), e.cleansing }, { QStringLiteral("dressing"), e.dressing },
        { QStringLiteral("fixation"), e.fixation }, { QStringLiteral("compression"), e.compression },
        { QStringLiteral("intervalDays"), e.intervalDays }, { QStringLiteral("nextChange"), iso(e.nextChange) },
        { QStringLiteral("notes"), e.notes }, { QStringLiteral("by"), e.by },
        { QStringLiteral("woundKind"), w ? w->kind : 0 }, { QStringLiteral("woundTitle"), w ? w->title() : QString() },
        { QStringLiteral("photoConsent"), w && w->photoConsent }, { QStringLiteral("photoRevision"), m_woundPhotoRevision },
        { QStringLiteral("hasPhoto"), !m_woundPhoto.isEmpty() },
    };
}

bool AppController::saveWoundEntry(qint64 woundId, const QVariantMap &m)
{
    if (!loggedIn()) return false;
    const auto w = wound(woundId);
    if (!w) { setError(tr("Die Wunde gibt es nicht mehr.")); return false; }
    Entry e;
    if (const qint64 id = m.value(QStringLiteral("id")).toLongLong()) {
        const auto x = entry(id);
        if (!x) { setError(tr("Der Eintrag ist nicht mehr vorhanden.")); return false; }
        e = *x;
    }
    e.woundId = woundId;
    e.kind = m.value(QStringLiteral("kind")).toInt() == DressingChange ? DressingChange : Assessment;
    const QDate d = fromIso(m.value(QStringLiteral("date")));
    const QTime t = QTime::fromString(m.value(QStringLiteral("time")).toString().trimmed(), QStringLiteral("H:mm"));
    if (!d.isValid() || !t.isValid()) { setError(tr("Bitte Datum (TT.MM.JJJJ) und Uhrzeit (HH:MM) prüfen.")); return false; }
    e.at = QDateTime(d, t);
    e.description = m.value(QStringLiteral("description")).toString().trimmed();
    e.trend = intOr(m.value(QStringLiteral("trend")), -1);
    e.cleansing = m.value(QStringLiteral("cleansing")).toString().trimmed();
    e.dressing = m.value(QStringLiteral("dressing")).toString().trimmed();
    e.fixation = m.value(QStringLiteral("fixation")).toString().trimmed();
    e.compression = m.value(QStringLiteral("compression")).toString().trimmed();
    e.intervalDays = qBound(0, m.value(QStringLiteral("intervalDays")).toInt(), 30);
    e.nextChange = fromIso(m.value(QStringLiteral("nextChange")));
    e.notes = m.value(QStringLiteral("notes")).toString().trimmed();
    e.photo = w->photoConsent ? m_woundPhoto : QByteArray();
    if (e.kind == Assessment) {
        e.length = num(m.value(QStringLiteral("length")));
        e.width = num(m.value(QStringLiteral("width")));
        e.depth = num(m.value(QStringLiteral("depth")));
        e.pockets = m.value(QStringLiteral("pockets")).toString().trimmed();
        e.granulation = qBound(0, m.value(QStringLiteral("granulation")).toInt(), 100);
        e.epithel = qBound(0, m.value(QStringLiteral("epithel")).toInt(), 100);
        e.fibrin = qBound(0, m.value(QStringLiteral("fibrin")).toInt(), 100);
        e.necrosis = qBound(0, m.value(QStringLiteral("necrosis")).toInt(), 100);
        e.exposed = strings(m.value(QStringLiteral("exposed")));
        e.exudate = intOr(m.value(QStringLiteral("exudate")), -1);
        e.exudateKind = m.value(QStringLiteral("exudateKind")).toString().trimmed();
        e.odor = intOr(m.value(QStringLiteral("odor")), -1);
        e.edge = strings(m.value(QStringLiteral("edge")));
        e.surroundings = strings(m.value(QStringLiteral("surroundings")));
        e.infection = strings(m.value(QStringLiteral("infection")));
        const auto pain = num(m.value(QStringLiteral("pain")));
        const auto painChange = num(m.value(QStringLiteral("painChange")));
        e.pain = pain ? std::optional<int>(qBound(0, qRound(*pain), 10)) : std::nullopt;
        e.painChange = painChange ? std::optional<int>(qBound(0, qRound(*painChange), 10)) : std::nullopt;
        e.grade = gradeNames(w->kind).isEmpty() ? -1 : intOr(m.value(QStringLiteral("grade")), -1);
        e.phase = intOr(m.value(QStringLiteral("phase")), -1);
    } else {
        if (e.description.isEmpty() && e.trend < 0 && e.dressing.isEmpty()) {
            setError(tr("Bitte mindestens Beschreibung, Tendenz oder Wundauflage angeben."));
            return false;
        }
        e.length = e.width = e.depth = std::nullopt;
        e.pockets.clear();
        e.granulation = e.epithel = e.fibrin = e.necrosis = 0;
        e.exposed.clear();
        e.exudate = e.odor = e.grade = e.phase = -1;
        e.exudateKind.clear();
        e.edge.clear();
        e.surroundings.clear();
        e.infection.clear();
        e.pain = e.painChange = std::nullopt;
    }
    QString err;
    if (!Wounds::saveEntry(e, &err)) { setError(err); return false; }
    m_woundPhoto.clear();
    return true;
}

bool AppController::deleteWoundEntry(qint64 id)
{
    QString err;
    if (!Wounds::deleteEntry(id, &err)) { setError(err.isEmpty() ? tr("Der Eintrag konnte nicht gelöscht werden.") : err); return false; }
    return true;
}

int AppController::woundPhotoFrom(const QString &pathOrUrl)
{
    QByteArray data;
    QString err;
    if (!DbTransfer::readFile(pathOrUrl, &data, &err)) { setError(err); return 0; }
    const QByteArray jpeg = preparePhoto(data, &err);
    if (jpeg.isEmpty()) { setError(err.isEmpty() ? tr("Das Bild lässt sich nicht lesen.") : err); return 0; }
    m_woundPhoto = jpeg;
    // aufgenommenes Foto der Kamera-App wird nicht mehr gebraucht
    const QUrl u(pathOrUrl);
    const QString local = u.isLocalFile() ? u.toLocalFile() : pathOrUrl;
    const QString capture = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/capture");
    if (local.startsWith(capture)) QFile::remove(local);
    return ++m_woundPhotoRevision;
}

int AppController::woundPhotoClear()
{
    m_woundPhoto.clear();
    return ++m_woundPhotoRevision;
}

bool AppController::setWoundPhotoData(const QByteArray &jpeg)
{
    m_woundPhoto = jpeg;
    ++m_woundPhotoRevision;
    return !jpeg.isEmpty();
}

QImage AppController::woundImage(const QString &id, int width)
{
    QImage img;
    if (!loggedIn()) return img;
    if (id == QLatin1String("draft")) {
        img.loadFromData(m_woundPhoto);
    } else if (id.startsWith(QLatin1String("chart-"))) {
        img = areaChart(entries(id.mid(6).toLongLong(), false));
    } else if (const auto e = entry(id.toLongLong())) {
        img.loadFromData(e->photo);
    }
    if (!img.isNull() && width > 0 && img.width() > width)
        img = img.scaledToWidth(width, Qt::SmoothTransformation);
    return img;
}
