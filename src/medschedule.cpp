// SPDX-License-Identifier: MIT
#include "medschedule.h"
#include "qtcompat.h"
#include "database.h"
#include "session.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace {
QString tr(const char *s) { return QCoreApplication::translate("MedSchedule", s); }
}

namespace MedSchedule {

QString categoryName(int c)
{
    switch (c) {
    case Medication::AsNeeded:  return tr("Bedarfsmedikation");
    case Medication::Emergency: return tr("Notfallmedikation");
    case Medication::Other:     return tr("Sonstiges");
    default:                    return tr("Regelmedikation");
    }
}

QString categoryHint(int c)
{
    switch (c) {
    case Medication::AsNeeded:  return tr("nur bei Bedarf – Anlass und Höchstmenge in die Bemerkung");
    case Medication::Emergency: return tr("für den Notfall – Anlass (z. B. Atemnot, Brustschmerz, Krampfanfall) in die Bemerkung");
    case Medication::Other:     return tr("z. B. Salben, Augentropfen, Nahrungsergänzung, Impfungen, Hilfsmittel – Anwendung in die Bemerkung");
    default:                    return tr("feste Einnahme nach Plan");
    }
}

QList<QPair<int, QList<Medication>>> grouped(const QList<Medication> &meds)
{
    QList<QPair<int, QList<Medication>>> g;
    for (int c = 0; c < Medication::CategoryCount; ++c) {
        QList<Medication> l;
        for (const Medication &m : meds)
            if (std::clamp(m.category, 0, Medication::CategoryCount - 1) == c) l << m;
        g << qMakePair(c, l);
    }
    return g;
}

Choice choiceOf(const Medication &m)
{
    if (m.category != Medication::Regular) return AsNeeded;
    switch (m.rhythm.freq) {
    case Recurrence::Daily:   return m.rhythm.interval > 1 ? EveryNDays : Daily;
    case Recurrence::Weekly:  return Weekdays;
    case Recurrence::Monthly: return Monthly;
    default:                  return Daily;
    }
}

bool isDaily(const Medication &m)
{
    return choiceOf(m) == Daily;
}

bool isInterval(const Medication &m)
{
    const Choice c = choiceOf(m);
    return c != Daily && c != AsNeeded;
}

QDate anchor(const Medication &m)
{
    if (m.rhythmStart.isValid()) return m.rhythmStart;
    if (m.startDate.isValid()) return m.startDate;
    return QDate::currentDate();
}

bool isDueOn(const Medication &m, const QDate &day)
{
    if (m.category != Medication::Regular) return false;
    const QDate a = anchor(m);
    const int n = std::max(1, m.rhythm.interval);
    switch (m.rhythm.freq) {
    case Recurrence::None:
        return true;
    case Recurrence::Daily:
        return day >= a && a.daysTo(day) % n == 0;
    case Recurrence::Weekly: {
        if (day < a || !(m.rhythm.weekdays & (1 << (day.dayOfWeek() - 1)))) return false;
        const QDate wa = a.addDays(1 - a.dayOfWeek()), wd = day.addDays(1 - day.dayOfWeek());
        return (wa.daysTo(wd) / 7) % n == 0;
    }
    case Recurrence::Monthly: {
        if (day < a || day.day() != a.day()) return false;   // wie RFC 5545: Monate ohne diesen Tag entfallen
        const int months = (day.year() - a.year()) * 12 + (day.month() - a.month());
        return months % n == 0;
    }
    case Recurrence::Yearly:
        return day >= a && day.month() == a.month() && day.day() == a.day() && (day.year() - a.year()) % n == 0;
    }
    return true;
}

QDate nextDue(const Medication &m, const QDate &from, int maxDays)
{
    for (int i = 0; i <= maxDays; ++i) {
        const QDate d = from.addDays(i);
        if (m.endDate.isValid() && d > m.endDate) return {};
        if (isDueOn(m, d) && (!m.startDate.isValid() || d >= m.startDate)) return d;
    }
    return {};
}

QString describe(const Medication &m)
{
    const QLocale loc;
    switch (choiceOf(m)) {
    case Daily:      return tr("täglich");
    case AsNeeded:   return m.category == Medication::Emergency ? tr("im Notfall")
                          : m.category == Medication::Other   ? tr("siehe Bemerkung") : tr("bei Bedarf");
    case EveryNDays: return tr("alle %1 Tage").arg(m.rhythm.interval);
    case Weekdays: {
        QStringList d;
        for (int i = 0; i < 7; ++i)
            if (m.rhythm.weekdays & (1 << i)) d << loc.dayName(i + 1, QLocale::ShortFormat);
        const QString days = d.join(QStringLiteral(", "));
        if (m.rhythm.interval > 1) return tr("alle %1 Wochen: %2").arg(m.rhythm.interval).arg(days);
        if (d.size() == 1) return tr("1× wöchentlich (%1)").arg(loc.dayName(qCountTrailingZeroBits(quint32(m.rhythm.weekdays)) + 1));
        return tr("wöchentlich: %1").arg(days);
    }
    case Monthly:
        return m.rhythm.interval > 1 ? tr("alle %1 Monate am %2.").arg(m.rhythm.interval).arg(anchor(m).day())
                                     : tr("monatlich am %1.").arg(anchor(m).day());
    }
    return {};
}

QString fullText(const Medication &m)
{
    QString s = isDaily(m) ? QString() : describe(m);
    if (!m.rhythmNote.trimmed().isEmpty())
        s += (s.isEmpty() ? QString() : QStringLiteral(" – ")) + m.rhythmNote.trimmed();
    return s;
}

QString toJsonString(const Medication &m)
{
    QJsonObject o = m.rhythm.toJson();
    if (m.rhythmStart.isValid()) o[QStringLiteral("start")] = m.rhythmStart.toString(Qt::ISODate);
    if (m.rhythm.freq == Recurrence::None) return {};
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void fromJsonString(Medication &m, const QString &json)
{
    m.rhythm = {};
    m.rhythmStart = {};
    if (json.trimmed().isEmpty()) return;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    m.rhythm = Recurrence::fromJson(o);
    m.rhythmStart = QDate::fromString(o.value(QStringLiteral("start")).toString(), Qt::ISODate);
    if (o.value(QStringLiteral("asNeeded")).toBool() && m.category == Medication::Regular)
        m.category = Medication::AsNeeded;   // Daten einer Vorabversion
}

// ----------------------------------------------------------------- Absetzen

QString stopReason(const Medication &m)
{
    return m.extra.value(QLatin1String(kStopReasonKey)).toString().trimmed();
}

QDate stopDate(const Medication &m)
{
    return QDate::fromString(m.extra.value(QLatin1String(kStopOnKey)).toString(), Qt::ISODate);
}

bool stopPlanned(const Medication &m)
{
    return !m.archived && stopDate(m).isValid();
}

void discontinue(Medication &m, const QDate &day, const QString &reason)
{
    const QDate today = QDate::currentDate();
    const QDate on = day.isValid() ? day : today;
    m.extra.remove(QStringLiteral("autoArchived"));
    if (reason.trimmed().isEmpty()) m.extra.remove(QLatin1String(kStopReasonKey));
    else m.extra[QLatin1String(kStopReasonKey)] = reason.trimmed();
    m.extra[QLatin1String(kStopOnKey)] = on.toString(Qt::ISODate);
    if (on > today) {
        // geplant: bis zum Vortag weiter im Plan, am Absetzdatum automatisch in die Historie
        m.archived = false;
        m.archivedAt = {};
        m.endDate = on.addDays(-1);
        if (m.startDate.isValid() && m.endDate < m.startDate) m.endDate = m.startDate;
        return;
    }
    if (!m.archived) m.archivedAt = QDateTime::currentDateTime();
    m.archived = true;
    m.endDate = on;
    if (m.startDate.isValid() && m.endDate < m.startDate) m.endDate = m.startDate;
}

void cancelPlannedStop(Medication &m)
{
    if (!stopPlanned(m)) return;
    m.endDate = {};
    m.extra.remove(QLatin1String(kStopOnKey));
    m.extra.remove(QLatin1String(kStopReasonKey));
}

void reactivate(Medication &m)
{
    m.archived = false;
    m.archivedAt = {};
    m.endDate = {};
    m.extra.remove(QLatin1String(kStopReasonKey));
    m.extra.remove(QLatin1String(kStopOnKey));
    m.extra.remove(QStringLiteral("autoArchived"));
}

QString statusText(const Medication &m)
{
    const QLocale loc;
    auto d = [&](const QDate &x) { return loc.toString(x, QStringLiteral("dd.MM.yyyy")); };
    if (m.archived && m.extra.value(QStringLiteral("autoArchived")).toBool() && m.endDate.isValid())
        return tr("beendet am %1").arg(d(m.endDate));
    if (stopPlanned(m))
        return tr("wird am %1 abgesetzt").arg(d(stopDate(m)));
    if (m.archived) {
        const QDate at = stopDate(m).isValid() ? stopDate(m) : m.endDate.isValid() ? m.endDate : m.archivedAt.date();
        return at.isValid() ? tr("abgesetzt am %1").arg(d(at)) : tr("abgesetzt");
    }
    if (m.endDate.isValid())
        return m.endDate < QDate::currentDate() ? tr("beendet am %1").arg(d(m.endDate)) : tr("bis %1").arg(d(m.endDate));
    return tr("aktiv");
}

// ----------------------------------------------------------------- Terminliste

QString slotName(int slot)
{
    switch (slot) {
    case Morning: return QCoreApplication::translate("MedicationAssistant", "morgens");
    case Noon:    return QCoreApplication::translate("MedicationAssistant", "mittags");
    case Evening: return QCoreApplication::translate("MedicationAssistant", "abends");
    default:      return QCoreApplication::translate("MedicationAssistant", "zur Nacht");
    }
}

QTime slotDefaultTime(int slot)
{
    static const int h[] = { 8, 12, 18, 21 };
    return QTime(h[std::clamp(slot, 0, SlotCount - 1)], 0);
}

int slotOf(const QTime &t)
{
    const int m = t.hour() * 60 + t.minute();
    if (m < 10 * 60 + 30) return Morning;
    if (m < 15 * 60) return Noon;
    if (m < 20 * 60) return Evening;
    return Night;
}

static bool hasDose(const QString &d)
{
    const QString t = d.trimmed();
    return !t.isEmpty() && t != QLatin1String("0") && t != QLatin1String("-");
}

QString slotDose(const Medication &m, int slot)
{
    const QString d[] = { m.morning, m.noon, m.evening, m.night };
    const QString t = d[std::clamp(slot, 0, SlotCount - 1)].trimmed();
    if (!hasDose(t)) return {};
    return t + (m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit);
}

bool canRemind(const Medication &m)
{
    return m.category == Medication::Regular || m.category == Medication::Other;
}

bool remindByWeekdays(const Medication &m)
{
    return m.category == Medication::Other || choiceOf(m) == Daily;
}

QList<QTime> defaultTimes(const Medication &m)
{
    QList<QTime> l;
    for (int s = 0; s < SlotCount; ++s)
        if (!slotDose(m, s).isEmpty()) l << slotDefaultTime(s);
    if (l.isEmpty()) l << slotDefaultTime(Morning);
    return l;
}

QTime intakeTime(const Medication &m)
{
    return defaultTimes(m).value(0, QTime(8, 0));
}

Reminder reminder(const Medication &m)
{
    Reminder r;
    const bool legacy = !m.extra.contains(QLatin1String(kRemindKey));
    // Ältere Versionen: Intervall-Medikamente kamen automatisch in die Terminliste (erste Einnahmezeit)
    r.on = legacy ? isInterval(m) && m.extra.value(QLatin1String(kToCalendarKey)).toBool(true)
                  : m.extra.value(QLatin1String(kRemindKey)).toBool();
    r.days = m.extra.value(QLatin1String(kRemindDaysKey)).toInt(0x7f) & 0x7f;
    for (const QJsonValue &v : m.extra.value(QLatin1String(kRemindTimesKey)).toArray()) {
        const QTime t = QTime::fromString(v.toString(), QStringLiteral("HH:mm"));
        if (t.isValid() && !r.times.contains(t)) r.times << t;
    }
    if (r.times.isEmpty() && !m.extra.contains(QLatin1String(kRemindTimesKey)))
        r.times = legacy && r.on ? QList<QTime>{ intakeTime(m) } : defaultTimes(m);
    std::sort(r.times.begin(), r.times.end());
    return r;
}

void setReminder(Medication &m, const Reminder &r)
{
    m.extra.remove(QLatin1String(kToCalendarKey));
    m.extra[QLatin1String(kRemindKey)] = r.on;
    QList<QTime> times = r.times;
    std::sort(times.begin(), times.end());
    QJsonArray a;
    for (const QTime &t : times)
        if (t.isValid() && !a.contains(t.toString(QStringLiteral("HH:mm")))) a << t.toString(QStringLiteral("HH:mm"));
    m.extra[QLatin1String(kRemindTimesKey)] = a;
    if ((r.days & 0x7f) == 0x7f) m.extra.remove(QLatin1String(kRemindDaysKey));
    else m.extra[QLatin1String(kRemindDaysKey)] = r.days & 0x7f;
}

QList<SlotTime> reminderSlots(const Medication &m)
{
    QList<SlotTime> l = vpFilledList<SlotTime>(SlotCount);
    for (int s = 0; s < SlotCount; ++s) l[s].time = slotDefaultTime(s);
    const QList<QTime> times = m.extra.contains(QLatin1String(kRemindTimesKey)) ? reminder(m).times : defaultTimes(m);
    for (const QTime &t : times) {
        int s = slotOf(t);
        for (int k = 0; k < SlotCount && l[s].on; ++k) s = (s + 1) % SlotCount;
        if (l[s].on) continue;
        l[s] = SlotTime{ true, t };
    }
    return l;
}

static QString daysText(int days)
{
    const QLocale loc;
    days &= 0x7f;
    if (days == 0x7f) return tr("täglich");
    if (days == 0x1f) return tr("Mo–Fr");
    if (days == 0x60) return tr("Sa und So");
    QStringList d;
    for (int i = 0; i < 7; ++i)
        if (days & (1 << i)) d << loc.dayName(i + 1, QLocale::ShortFormat);
    return d.join(QStringLiteral(", "));
}

QString reminderText(const Medication &m)
{
    const Reminder r = reminder(m);
    if (!r.on || !canRemind(m) || r.times.isEmpty()) return {};
    QStringList t;
    for (const QTime &x : r.times) t << x.toString(QStringLiteral("HH:mm"));
    const QString times = t.size() > 1 ? tr("%1 und %2 Uhr").arg(t.mid(0, t.size() - 1).join(QStringLiteral(", ")), t.last())
                                       : tr("%1 Uhr").arg(t.first());
    return tr("%1 um %2").arg(remindByWeekdays(m) ? daysText(r.days) : describe(m), times);
}

QList<qint64> seriesIds(const Medication &m)
{
    QList<qint64> l;
    for (const QJsonValue &v : m.extra.value(QLatin1String(kSeriesListKey)).toArray())
        if (const qint64 id = v.toVariant().toLongLong(); id > 0 && !l.contains(id)) l << id;
    if (const qint64 id = m.extra.value(QLatin1String(kSeriesKey)).toVariant().toLongLong(); id > 0 && !l.contains(id)) l << id;
    return l;
}

bool wantsAppointments(const Medication &m)
{
    if (m.archived || !canRemind(m)) return false;
    const Reminder r = reminder(m);
    if (!r.on || r.times.isEmpty()) return false;
    return !remindByWeekdays(m) || r.days != 0;
}

static QString doseText(const Medication &m)
{
    QStringList l;
    for (int s = 0; s < SlotCount; ++s)
        if (const QString d = slotDose(m, s); !d.isEmpty()) l << slotName(s) + QStringLiteral(": ") + d;
    return l.join(QStringLiteral(", "));
}

// Kennung aller Angaben, die in die Terminserien einfließen – ändert sie sich, werden die Serien neu angelegt
static QString signature(const Medication &m)
{
    const Reminder r = reminder(m);
    QString rem = QString::number(m.category) + QLatin1Char('/') + QString::number(remindByWeekdays(m) ? r.days : 0);
    for (const QTime &t : r.times) rem += QLatin1Char('/') + t.toString(QStringLiteral("HH:mm"));
    const QString s = toJsonString(m) + QLatin1Char('|') + m.name + m.strength + doseText(m) + m.rhythmNote
                    + m.notes + m.startDate.toString(Qt::ISODate) + m.endDate.toString(Qt::ISODate) + QLatin1Char('|') + rem;
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

static Recurrence ruleFor(const Medication &m, const Reminder &r)
{
    Recurrence rule;
    if (remindByWeekdays(m)) {
        if ((r.days & 0x7f) == 0x7f) rule.freq = Recurrence::Daily;
        else { rule.freq = Recurrence::Weekly; rule.weekdays = r.days & 0x7f; }
        rule.interval = 1;
    } else {
        rule = m.rhythm;
    }
    rule.count = 0;
    rule.until = m.endDate;
    return rule;
}

// erster Einnahmetermin zu dieser Uhrzeit, der noch bevorsteht
static QDateTime firstOccurrence(const Medication &m, const Reminder &r, const QTime &t)
{
    const QDateTime now = QDateTime::currentDateTime();
    QDate d = std::max(QDate::currentDate(), m.startDate.isValid() ? m.startDate : QDate::currentDate());
    for (int i = 0; i < 800; ++i, d = d.addDays(1)) {
        if (m.endDate.isValid() && d > m.endDate) return {};
        const bool due = remindByWeekdays(m) ? bool(r.days & (1 << (d.dayOfWeek() - 1))) : isDueOn(m, d);
        if (due && QDateTime(d, t) > now) return QDateTime(d, t);
    }
    return {};
}

int syncAppointments(qint64 patientId, QList<Medication> &meds, const QList<Medication> &removed)
{
    Database &db = Database::instance();
    if (!db.isOpen() || !patientId) return 0;
    const QDateTime cut(QDate::currentDate(), QTime(0, 0));   // Vergangenes bleibt stehen
    int changed = 0;
    auto endSeries = [&](qint64 sid) {
        if (sid && db.series(sid)) { db.truncateSeries(sid, cut); ++changed; }
    };
    for (const Medication &m : removed)
        for (qint64 sid : seriesIds(m)) endSeries(sid);

    for (Medication &m : meds) {
        const QList<qint64> sids = seriesIds(m);
        bool allThere = !sids.isEmpty();
        for (qint64 sid : sids) if (!db.series(sid)) allThere = false;   // Serie in der Terminliste gelöscht
        const bool want = wantsAppointments(m);
        const QString sig = want ? signature(m) : QString();
        if (want && allThere && m.extra.value(QLatin1String(kSigKey)).toString() == sig
            && !m.extra.contains(QLatin1String(kSeriesKey)))
            continue;   // unverändert

        QJsonObject extra = m.extra;
        for (qint64 sid : sids) endSeries(sid);
        extra.remove(QLatin1String(kSeriesKey));
        extra.remove(QLatin1String(kSeriesListKey));
        extra.remove(QLatin1String(kSigKey));
        if (want) {
            const Reminder r = reminder(m);
            QJsonArray created;
            for (const QTime &t : r.times) {
                const QDateTime first = firstOccurrence(m, r, t);
                if (!first.isValid()) continue;
                const int slot = slotOf(t);
                const QString dose = slotDose(m, slot);
                Appointment a;
                a.title = QStringLiteral("💊 ") + m.name + (m.strength.isEmpty() ? QString() : QLatin1Char(' ') + m.strength)
                        + (dose.isEmpty() ? QString() : QStringLiteral(" – ") + dose);
                a.patientId = patientId;
                a.owner = Session::user();
                a.createdBy = Session::user();
                a.start = first;
                a.end = a.start.addSecs(15 * 60);
                a.priority = Appointment::High;
                QStringList notes;
                notes << tr("Medikament: %1").arg(doseText(m));
                notes << tr("Erinnerung: %1").arg(reminderText(m));
                if (!m.rhythmNote.isEmpty()) notes << tr("Bemerkung: %1").arg(m.rhythmNote);
                if (!m.notes.isEmpty()) notes << tr("Hinweise: %1").arg(m.notes);
                a.notes = notes.join(QLatin1Char('\n'));
                a.remindMinutes = 0;
                a.remindVisual = true;
                a.remindSound = true;
                a.extra[QStringLiteral("medicationId")] = m.id;
                // Die Serie beginnt am ersten fälligen Termin – damit bleibt das Raster des Rhythmus erhalten
                if (db.createSeries(a, ruleFor(m, r))) {
                    created << a.seriesId;
                    ++changed;
                }
            }
            if (!created.isEmpty()) {
                extra[QLatin1String(kSeriesListKey)] = created;
                extra[QLatin1String(kSigKey)] = sig;
            }
        }
        if (extra != m.extra) {
            m.extra = extra;
            if (m.id) db.updateMedicationLeaflet(m.id, m.pzn, m.extra);
        }
    }
    return changed;
}

} // namespace MedSchedule
