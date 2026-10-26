// VitaParms – Termine: gemeinsame Fachlogik für Desktop und Tablet (siehe apptcore.h)
// SPDX-License-Identifier: MIT
#include "apptcore.h"

#include "database.h"
#include "session.h"

#include <QCoreApplication>
#include <QLocale>

namespace ApptUi {

QString priorityName(int p)
{
    switch (p) {
    case Appointment::Low:    return QCoreApplication::translate("ApptUi", "niedrig");
    case Appointment::High:   return QCoreApplication::translate("ApptUi", "hoch");
    case Appointment::Urgent: return QCoreApplication::translate("ApptUi", "dringend");
    default:                  return QCoreApplication::translate("ApptUi", "normal");
    }
}

QColor priorityColor(int p)
{
    switch (p) {
    case Appointment::Low:    return QColor(0x8a, 0x8a, 0x8a);
    case Appointment::High:   return QColor(0xE6, 0x9F, 0x00);
    case Appointment::Urgent: return QColor(0xD5, 0x2B, 0x1E);
    default:                  return QColor(0x00, 0x72, 0xB2);
    }
}

QList<QPair<int, QString>> leadTimes()
{
    auto t = [](const char *s) { return QCoreApplication::translate("ApptUi", s); };
    return { { -1, t(QT_TRANSLATE_NOOP("ApptUi", "keine Erinnerung")) }, { 0, t(QT_TRANSLATE_NOOP("ApptUi", "zum Termin")) }, { 5, t(QT_TRANSLATE_NOOP("ApptUi", "5 Minuten vorher")) },
             { 10, t(QT_TRANSLATE_NOOP("ApptUi", "10 Minuten vorher")) }, { 15, t(QT_TRANSLATE_NOOP("ApptUi", "15 Minuten vorher")) }, { 30, t(QT_TRANSLATE_NOOP("ApptUi", "30 Minuten vorher")) },
             { 60, t(QT_TRANSLATE_NOOP("ApptUi", "1 Stunde vorher")) }, { 120, t(QT_TRANSLATE_NOOP("ApptUi", "2 Stunden vorher")) }, { 240, t(QT_TRANSLATE_NOOP("ApptUi", "4 Stunden vorher")) },
             { 1440, t(QT_TRANSLATE_NOOP("ApptUi", "1 Tag vorher")) }, { 2880, t(QT_TRANSLATE_NOOP("ApptUi", "2 Tage vorher")) }, { 10080, t(QT_TRANSLATE_NOOP("ApptUi", "1 Woche vorher")) } };
}

QString leadText(int minutes)
{
    for (const auto &[m, text] : leadTimes())
        if (m == minutes) return text;
    return QCoreApplication::translate("ApptUi", "%1 Minuten vorher").arg(minutes);
}

QStringList accountNames()
{
    QStringList l;
    for (const Account &a : Session::keyStore().accounts()) l << a.name;
    if (l.isEmpty() && !Session::user().isEmpty()) l << Session::user();
    return l;
}

bool canSee(const Appointment &a)
{
    return Session::isAdmin() || a.owner.compare(Session::user(), Qt::CaseInsensitive) == 0;
}

QString seriesText(const Appointment &a)
{
    if (!a.isSeries()) return {};
    const auto s = Database::instance().series(a.seriesId);
    if (!s) return {};
    QString t = s->rule.describe(s->dtstart.date());
    if (a.detached) t += QCoreApplication::translate("ApptUi", " (dieser Termin einzeln geändert)");
    return t;
}

// ---------------------------------------------------------------- Wiederholung

QList<QPair<int, QString>> repeatChoices()
{
    auto t = [](const char *s) { return QCoreApplication::translate("AppointmentDialog", s); };
    return { { RepNone, t(QT_TRANSLATE_NOOP("AppointmentDialog", "keine")) }, { RepDaily, t(QT_TRANSLATE_NOOP("AppointmentDialog", "täglich")) }, { RepEveryNDays, t(QT_TRANSLATE_NOOP("AppointmentDialog", "alle … Tage")) },
             { RepWorkdays, t(QT_TRANSLATE_NOOP("AppointmentDialog", "werktags (Mo–Fr)")) }, { RepWeekly, t(QT_TRANSLATE_NOOP("AppointmentDialog", "wöchentlich")) },
             { RepEveryNWeeks, t(QT_TRANSLATE_NOOP("AppointmentDialog", "alle … Wochen (am Wochentag)")) }, { RepMonthly, t(QT_TRANSLATE_NOOP("AppointmentDialog", "monatlich")) },
             { RepYearly, t(QT_TRANSLATE_NOOP("AppointmentDialog", "jährlich")) } };
}

int repeatChoiceOf(const Recurrence &r)
{
    switch (r.freq) {
    case Recurrence::Daily:   return r.interval > 1 ? RepEveryNDays : RepDaily;
    case Recurrence::Weekly:  return (r.weekdays == 0x1f && r.interval == 1) ? RepWorkdays
                                   : r.interval > 1 ? RepEveryNWeeks : RepWeekly;
    case Recurrence::Monthly: return RepMonthly;
    case Recurrence::Yearly:  return RepYearly;
    default:                  return RepNone;
    }
}

Recurrence makeRecurrence(int choice, int interval, int weekdays, int monthMode, int endChoice,
                          const QDate &until, int count, const QDate &start)
{
    Recurrence r;
    switch (choice) {
    case RepDaily:
    case RepEveryNDays:  r.freq = Recurrence::Daily; break;
    case RepWorkdays:    r.freq = Recurrence::Weekly; r.weekdays = 0x1f; break;
    case RepWeekly:
    case RepEveryNWeeks: r.freq = Recurrence::Weekly; break;
    case RepMonthly:     r.freq = Recurrence::Monthly; break;
    case RepYearly:      r.freq = Recurrence::Yearly; break;
    default:             return r;
    }
    if (choice != RepWorkdays) r.interval = std::max(1, interval);
    if (r.freq == Recurrence::Weekly && r.weekdays == 0) r.weekdays = weekdays & 0x7f;
    r.monthMode = monthMode;
    if (endChoice == EndUntil) r.until = until;
    else if (endChoice == EndCount) r.count = std::max(1, count);
    r.normalize(start);
    return r;
}

QList<QPair<int, QString>> monthModes(const QDate &start)
{
    auto t = [](const char *s) { return QCoreApplication::translate("AppointmentDialog", s); };
    const QString day = QLocale().dayName(start.dayOfWeek());
    QList<QPair<int, QString>> l;
    l << qMakePair(int(Recurrence::ByMonthDay), t(QT_TRANSLATE_NOOP("AppointmentDialog", "am %1 des Monats")).arg(Recurrence::ordinal(start.day())));
    if (Recurrence::nthOfMonth(start) <= 4)
        l << qMakePair(int(Recurrence::ByNthWeekday), t(QT_TRANSLATE_NOOP("AppointmentDialog", "am %1 %2")).arg(Recurrence::ordinal(Recurrence::nthOfMonth(start)), day));
    if (Recurrence::isLastOfMonth(start))
        l << qMakePair(int(Recurrence::ByLastWeekday), t(QT_TRANSLATE_NOOP("AppointmentDialog", "am letzten %1")).arg(day));
    return l;
}

// ---------------------------------------------------------------- Speichern und Löschen

namespace {
struct SeriesInfo {
    std::optional<AppointmentSeries> series;
    Recurrence orig;          // Regel der Serie, bezogen auf ihren Beginn
    QDate start;              // Beginn der Serie
};
SeriesInfo seriesInfo(const Appointment &before)
{
    SeriesInfo i;
    if (!before.isSeries()) return i;
    i.series = Database::instance().series(before.seriesId);
    if (i.series) i.orig = i.series->rule;
    i.start = i.series ? i.series->dtstart.date() : before.seriesOrig.date();
    i.orig.normalize(i.start);
    return i;
}
} // namespace

bool ruleChanged(const Appointment &before, const std::function<Recurrence(const QDate &)> &rule)
{
    if (!before.isSeries() || !rule) return false;
    const SeriesInfo i = seriesInfo(before);
    // Vergleich bezogen auf den Serienbeginn, damit ein verschobener Einzeltermin nicht als neue Regel gilt
    return rule(i.start) != i.orig;
}

bool saveWithScope(Appointment &a, const Appointment &before, const std::function<Recurrence(const QDate &)> &rule,
                   SeriesScope scope, QString *error)
{
    Database &db = Database::instance();
    const Recurrence newRule = rule ? rule(a.start.date()) : Recurrence();
    if (newRule.until.isValid() && newRule.until < a.start.date()) {
        if (error) *error = QCoreApplication::translate("AppointmentDialog", "Das Ende der Serie liegt vor dem ersten Termin.");
        return false;
    }
    if (!a.isSeries()) {
        if (newRule.isNone()) return db.saveAppointment(a, error);
        return db.createSeries(a, newRule, error);
    }
    const SeriesInfo info = seriesInfo(before);
    const bool changed = ruleChanged(before, rule);
    // Gleiche Regel auf einen neuen Starttermin übertragen (einzelner Wochentag wandert mit)
    auto moved = [&](const QDate &newStart) {
        Recurrence r = info.orig;
        if (r.freq == Recurrence::Weekly && qPopulationCount(quint32(r.weekdays)) == 1)
            r.weekdays = 1 << (newStart.dayOfWeek() - 1);
        r.normalize(newStart);
        return r;
    };
    switch (scope) {
    case SeriesScope::Cancel:
        return false;
    case SeriesScope::OnlyThis:
        if (changed) {   // eine geänderte Regel betrifft immer mehrere Termine
            if (error) *error = QCoreApplication::translate("ApptUi", "Eine geänderte Wiederholung gilt immer für mehrere Termine.");
            return false;
        }
        a.detached = true;
        return db.saveAppointment(a, error);
    case SeriesScope::Following:
        return db.splitSeries(a.seriesId, a.seriesOrig, a, changed ? newRule : moved(a.start.date()), error);
    case SeriesScope::All: {
        if (!changed && a.start.date() == a.seriesOrig.date()) {
            const bool done = a.done;
            if (!db.updateSeriesAll(a.seriesId, a, error)) return false;
            if (done != before.done) db.setAppointmentDone(a.id, done);
            return true;
        }
        // Datum oder Regel geändert: Serie ab ihrem ersten Termin neu aufbauen
        const QDateTime first = info.series ? info.series->dtstart : a.seriesOrig;
        const qint64 shift = a.seriesOrig.date().daysTo(a.start.date());
        Appointment f = a;
        const qint64 dur = a.end.isValid() ? a.start.secsTo(a.end) : -1;
        f.start = QDateTime(first.date().addDays(shift), a.start.time());
        f.end = dur >= 0 ? f.start.addSecs(dur) : QDateTime();
        f.done = false;
        f.remindedAt = {};
        f.snoozeUntil = {};
        Recurrence r = changed ? newRule : moved(f.start.date());
        r.normalize(f.start.date());
        if (!db.splitSeries(a.seriesId, first, f, r, error, true)) return false;
        a = f;
        return true;
    }
    }
    return false;
}

bool deleteWithScope(const Appointment &a, SeriesScope scope, QString *error)
{
    Database &db = Database::instance();
    if (!a.isSeries()) return scope != SeriesScope::Cancel && db.deleteAppointment(a.id, error);
    switch (scope) {
    case SeriesScope::OnlyThis:  return db.deleteOccurrence(a.id, error);
    case SeriesScope::Following: return db.truncateSeries(a.seriesId, a.seriesOrig, error);
    case SeriesScope::All:       return db.deleteSeries(a.seriesId, error);
    default:                     return false;
    }
}

} // namespace ApptUi
