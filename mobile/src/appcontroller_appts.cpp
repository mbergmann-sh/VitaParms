// VitaParms Tablet – Terminplaner (Liste, anlegen, bearbeiten, Serien, erledigt)
// Dieselbe Fachlogik wie am Computer (apptcore.h): Serienregeln, „nur dieser / alle folgenden / alle“.
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "apptcore.h"
#include "glucoseprofile.h"
#include "quarterreminder.h"
#include "database.h"
#include "session.h"

#include <QLocale>

namespace {
QString iso(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QString hhmm(const QTime &t) { return t.isValid() ? t.toString(QStringLiteral("HH:mm")) : QString(); }
QDate fromIso(const QVariant &v) { return QDate::fromString(v.toString(), Qt::ISODate); }
QTime fromHhmm(const QVariant &v)
{
    const QString s = v.toString().trimmed().replace(QLatin1Char('.'), QLatin1Char(':'));
    QTime t = QTime::fromString(s, QStringLiteral("H:mm"));
    if (!t.isValid()) t = QTime::fromString(s, QStringLiteral("HH:mm"));
    return t;
}

QVariantMap ruleToMap(const Recurrence &r, const QDate &start)
{
    const int choice = ApptUi::repeatChoiceOf(r);
    int weekdays = r.freq == Recurrence::Weekly ? r.weekdays : (1 << (start.dayOfWeek() - 1));
    int interval = r.interval;
    if (choice == ApptUi::RepEveryNDays || choice == ApptUi::RepEveryNWeeks) interval = std::max(2, interval);
    return { { QStringLiteral("repeat"), choice }, { QStringLiteral("interval"), interval },
             { QStringLiteral("weekdays"), weekdays }, { QStringLiteral("monthMode"), r.monthMode },
             { QStringLiteral("endMode"), r.count > 0 ? ApptUi::EndCount : r.until.isValid() ? ApptUi::EndUntil : ApptUi::EndNever },
             { QStringLiteral("until"), iso(r.until.isValid() ? r.until : start.addMonths(3)) },
             { QStringLiteral("count"), r.count > 0 ? r.count : 10 } };
}

Recurrence mapToRule(const QVariantMap &m, const QDate &start)
{
    return ApptUi::makeRecurrence(m.value(QStringLiteral("repeat")).toInt(), m.value(QStringLiteral("interval"), 1).toInt(),
                                  m.value(QStringLiteral("weekdays")).toInt(), m.value(QStringLiteral("monthMode")).toInt(),
                                  m.value(QStringLiteral("endMode")).toInt(), fromIso(m.value(QStringLiteral("until"))),
                                  m.value(QStringLiteral("count")).toInt(), start);
}

ApptUi::SeriesScope scopeOf(const QString &s)
{
    if (s == QLatin1String("only")) return ApptUi::SeriesScope::OnlyThis;
    if (s == QLatin1String("following")) return ApptUi::SeriesScope::Following;
    if (s == QLatin1String("all")) return ApptUi::SeriesScope::All;
    return ApptUi::SeriesScope::OnlyThis;      // Einzeltermin
}
} // namespace

// ---------------------------------------------------------------- Liste

QVariantList AppController::appointmentList(qint64 pid, const QString &range, bool showDone)
{
    QVariantList l;
    if (!loggedIn()) return l;
    Database &db = Database::instance();
    const QDate today = QDate::currentDate();
    QDateTime from, to;
    if (range == QLatin1String("today")) {
        from = QDateTime(today, QTime(0, 0));
        to = from.addDays(1);
    } else if (range == QLatin1String("week")) {
        from = QDateTime(today.addDays(1 - today.dayOfWeek()), QTime(0, 0));
        to = from.addDays(7);
    } else if (range == QLatin1String("month")) {
        from = QDateTime(QDate(today.year(), today.month(), 1), QTime(0, 0));
        to = from.addMonths(1);
    } else if (range == QLatin1String("past")) {
        from = QDateTime(today.addDays(-90), QTime(0, 0));
        to = QDateTime::currentDateTime();
    } else {                                     // kommende
        from = QDateTime(today, QTime(0, 0));
    }
    const QString owner = Session::isAdmin() ? QString() : Session::user();
    QList<Appointment> list = db.appointments(from, to, owner, pid, showDone);
    if (range == QLatin1String("past")) std::reverse(list.begin(), list.end());   // jüngste zuerst
    QHash<qint64, QString> names;
    for (const Patient &p : db.patients()) names.insert(p.id, p.displayName());
    const QLocale loc;
    const QDateTime now = QDateTime::currentDateTime();
    for (const Appointment &a : list) {
        if (!ApptUi::canSee(a)) continue;
        const qint64 d = today.daysTo(a.start.date());
        const QString dayText = (d == 0 ? tr("heute") + QStringLiteral(", ") : d == 1 ? tr("morgen") + QStringLiteral(", ") : QString())
                              + loc.toString(a.start.date(), QStringLiteral("dddd, dd.MM.yyyy"));
        QString time = tr("ganztägig");
        if (!a.allDay) {
            time = hhmm(a.start.time());
            if (a.end.isValid() && a.end > a.start)
                time += QStringLiteral("–") + (a.end.date() == a.start.date() ? hhmm(a.end.time())
                                                                             : loc.toString(a.end, QStringLiteral("dd.MM. HH:mm")));
        }
        QString reminder;
        if (a.hasReminder())
            reminder = a.snoozeUntil.isValid() ? tr("später: %1").arg(loc.toString(a.snoozeUntil, QStringLiteral("HH:mm")))
                                               : ApptUi::leadText(a.remindMinutes);
        l << QVariantMap{ { QStringLiteral("id"), a.id }, { QStringLiteral("title"), a.title },
                          { QStringLiteral("location"), a.location }, { QStringLiteral("notes"), a.notes },
                          { QStringLiteral("priority"), a.priority },
                          { QStringLiteral("priorityName"), ApptUi::priorityName(a.priority) },
                          { QStringLiteral("priorityColor"), ApptUi::priorityColor(a.priority).name() },
                          { QStringLiteral("day"), iso(a.start.date()) }, { QStringLiteral("dayText"), dayText },
                          { QStringLiteral("time"), time }, { QStringLiteral("person"), a.patientId ? names.value(a.patientId) : QString() },
                          { QStringLiteral("owner"), a.owner }, { QStringLiteral("series"), a.isSeries() },
                          { QStringLiteral("reminder"), reminder }, { QStringLiteral("done"), a.done },
                          { QStringLiteral("past"), (a.end.isValid() ? a.end : a.start) < now } };
    }
    return l;
}

// ---------------------------------------------------------------- ein Termin

QVariantMap AppController::appointment(qint64 id, qint64 pid)
{
    Appointment a;
    if (id) {
        const auto found = Database::instance().appointment(id);
        if (!found || !ApptUi::canSee(*found)) return {};
        a = *found;
    } else {
        const QDate d = QDate::currentDate();
        a.start = QDateTime(d, QTime(std::min(QTime::currentTime().hour() + 1, 23), 0));
        a.end = a.start.addSecs(3600);
        a.owner = Session::user();
        a.patientId = pid;
        a.remindMinutes = 60;
    }
    Recurrence rule;
    QDate ruleStart = a.start.date();
    if (a.isSeries())
        if (const auto s = Database::instance().series(a.seriesId)) { rule = s->rule; ruleStart = s->dtstart.date(); }
    const QDateTime end = a.end.isValid() ? a.end : a.start.addSecs(3600);
    return { { QStringLiteral("id"), a.id }, { QStringLiteral("title"), a.title },
             { QStringLiteral("location"), a.location }, { QStringLiteral("notes"), a.notes },
             { QStringLiteral("priority"), a.priority }, { QStringLiteral("allDay"), a.allDay },
             { QStringLiteral("startDate"), iso(a.start.date()) }, { QStringLiteral("startTime"), hhmm(a.start.time()) },
             { QStringLiteral("endDate"), iso(end.date()) }, { QStringLiteral("endTime"), hhmm(end.time()) },
             { QStringLiteral("patientId"), a.patientId }, { QStringLiteral("owner"), a.owner.isEmpty() ? Session::user() : a.owner },
             { QStringLiteral("remindMinutes"), a.remindMinutes }, { QStringLiteral("remindVisual"), a.remindVisual },
             { QStringLiteral("remindSound"), a.remindSound }, { QStringLiteral("remindEmail"), a.remindEmail },
             { QStringLiteral("done"), a.done }, { QStringLiteral("isSeries"), a.isSeries() },
             { QStringLiteral("detached"), a.detached }, { QStringLiteral("seriesText"), ApptUi::seriesText(a) },
             { QStringLiteral("rule"), ruleToMap(rule, ruleStart) } };
}

QVariantList AppController::apptPersons()
{
    QVariantList l{ QVariantMap{ { QStringLiteral("id"), 0 }, { QStringLiteral("name"), tr("— keine Person —") } } };
    if (!loggedIn()) return l;
    for (const Patient &p : Database::instance().patients())
        l << QVariantMap{ { QStringLiteral("id"), p.id }, { QStringLiteral("name"), p.displayName() } };
    return l;
}

QStringList AppController::apptOwners() const
{
    return ApptUi::accountNames();
}

QVariantList AppController::apptLeadTimes() const
{
    QVariantList l;
    for (const auto &[m, text] : ApptUi::leadTimes())
        l << QVariantMap{ { QStringLiteral("value"), m }, { QStringLiteral("text"), text } };
    return l;
}

QVariantList AppController::apptPriorities() const
{
    QVariantList l;
    for (int p = Appointment::Low; p <= Appointment::Urgent; ++p)
        l << QVariantMap{ { QStringLiteral("value"), p }, { QStringLiteral("text"), ApptUi::priorityName(p) },
                          { QStringLiteral("color"), ApptUi::priorityColor(p).name() } };
    return l;
}

QVariantList AppController::apptRepeatChoices() const
{
    QVariantList l;
    for (const auto &[v, text] : ApptUi::repeatChoices())
        l << QVariantMap{ { QStringLiteral("value"), v }, { QStringLiteral("text"), text } };
    return l;
}

QVariantList AppController::apptMonthModes(const QString &isoStart) const
{
    QVariantList l;
    const QDate d = QDate::fromString(isoStart, Qt::ISODate);
    if (!d.isValid()) return l;
    for (const auto &[v, text] : ApptUi::monthModes(d))
        l << QVariantMap{ { QStringLiteral("value"), v }, { QStringLiteral("text"), text } };
    return l;
}

QStringList AppController::weekdayNames() const
{
    QStringList l;
    for (int i = 1; i <= 7; ++i) l << QLocale().dayName(i, QLocale::ShortFormat);
    return l;
}

QString AppController::apptRulePreview(const QVariantMap &rule, const QString &isoStart) const
{
    const QDate start = QDate::fromString(isoStart, Qt::ISODate);
    if (!start.isValid()) return {};
    const Recurrence r = mapToRule(rule, start);
    if (r.isNone()) return {};
    const QList<QDate> next = r.dates(start, start.addYears(5), 6);
    QStringList ds;
    for (int i = 0; i < std::min<qsizetype>(5, next.size()); ++i)
        ds << QLocale().toString(next[i], QStringLiteral("ddd dd.MM."));
    return tr("↻ %1 – Termine: %2").arg(r.describe(start), ds.join(QStringLiteral(", ")) + (next.size() > 5 ? QStringLiteral(" …") : QString()));
}

// ---------------------------------------------------------------- Speichern, Löschen, erledigt

static bool mapToAppointment(const QVariantMap &m, Appointment &a, QString *error)
{
    a.title = m.value(QStringLiteral("title")).toString().trimmed();
    if (a.title.isEmpty()) { *error = AppController::tr("Bitte einen Titel angeben."); return false; }
    const QDate sd = fromIso(m.value(QStringLiteral("startDate")));
    const QDate ed = fromIso(m.value(QStringLiteral("endDate")));
    a.allDay = m.value(QStringLiteral("allDay")).toBool();
    const QTime st = a.allDay ? QTime(0, 0) : fromHhmm(m.value(QStringLiteral("startTime")));
    const QTime et = a.allDay ? QTime(23, 59) : fromHhmm(m.value(QStringLiteral("endTime")));
    if (!sd.isValid() || !st.isValid()) { *error = AppController::tr("Bitte Datum (TT.MM.JJJJ) und Uhrzeit (HH:MM) des Beginns prüfen."); return false; }
    a.start = QDateTime(sd, st);
    a.end = (ed.isValid() && et.isValid()) ? QDateTime(ed, et) : (a.allDay ? QDateTime(sd, QTime(23, 59)) : a.start.addSecs(3600));
    if (a.allDay && a.end.date() < a.start.date()) a.end = QDateTime(sd, QTime(23, 59));
    if (a.end < a.start) { *error = AppController::tr("Das Ende liegt vor dem Beginn."); return false; }
    a.location = m.value(QStringLiteral("location")).toString().trimmed();
    a.notes = m.value(QStringLiteral("notes")).toString().trimmed();
    a.priority = std::clamp(m.value(QStringLiteral("priority"), int(Appointment::Normal)).toInt(), int(Appointment::Low), int(Appointment::Urgent));
    a.patientId = m.value(QStringLiteral("patientId")).toLongLong();
    a.owner = Session::isAdmin() ? m.value(QStringLiteral("owner")).toString() : Session::user();
    if (a.owner.isEmpty()) a.owner = Session::user();
    a.remindMinutes = m.value(QStringLiteral("remindMinutes"), -1).toInt();
    a.remindVisual = m.value(QStringLiteral("remindVisual"), true).toBool();
    a.remindSound = m.value(QStringLiteral("remindSound"), true).toBool();
    if (a.remindMinutes >= 0 && !a.remindVisual && !a.remindSound && !a.remindEmail) {
        *error = AppController::tr("Bitte mindestens eine Art der Erinnerung wählen.");
        return false;
    }
    a.done = m.value(QStringLiteral("done")).toBool();
    return true;
}

bool AppController::apptRuleChanged(const QVariantMap &m)
{
    const auto before = Database::instance().appointment(m.value(QStringLiteral("id")).toLongLong());
    if (!before || !before->isSeries()) return false;
    const QVariantMap rule = m.value(QStringLiteral("rule")).toMap();
    return ApptUi::ruleChanged(*before, [&](const QDate &d) { return mapToRule(rule, d); });
}

bool AppController::saveAppointment(const QVariantMap &m, const QString &scope)
{
    Appointment before;
    const qint64 id = m.value(QStringLiteral("id")).toLongLong();
    if (id) {
        const auto found = Database::instance().appointment(id);
        if (!found || !ApptUi::canSee(*found)) { setError(tr("Den Termin gibt es nicht mehr.")); return false; }
        before = *found;
    }
    Appointment a = before;
    QString err;
    if (!mapToAppointment(m, a, &err)) { setError(err); return false; }
    // Zeit oder Vorwarnung geändert → Erinnerung neu scharf schalten
    if (a.id == 0 || before.remindMinutes != a.remindMinutes || before.start != a.start) {
        a.remindedAt = {};
        a.snoozeUntil = {};
    }
    if (a.id == 0) a.createdBy = Session::user();
    const QVariantMap rule = m.value(QStringLiteral("rule")).toMap();
    if (!ApptUi::saveWithScope(a, before, [&](const QDate &d) { return mapToRule(rule, d); }, scopeOf(scope), &err)) {
        setError(err.isEmpty() ? tr("Speichern fehlgeschlagen.") : err);
        return false;
    }
    bump();
    return true;
}

bool AppController::deleteAppointment(qint64 id, const QString &scope)
{
    const auto a = Database::instance().appointment(id);
    if (!a || !ApptUi::canSee(*a)) return false;
    QString err;
    if (!ApptUi::deleteWithScope(*a, scopeOf(scope), &err)) { setError(err); return false; }
    bump();
    return true;
}

bool AppController::setAppointmentDone(qint64 id, bool done)
{
    const auto a = Database::instance().appointment(id);
    if (!a || !ApptUi::canSee(*a)) return false;
    const bool ok = Database::instance().setAppointmentDone(id, done);
    if (ok) bump();
    return ok;
}

// ---------------------------------------------------------------- BZ-Tagesprofil, Quartalswechsel


static GlucoseProfile::Kind profileKind(bool bp) { return bp ? GlucoseProfile::Kind::BloodPressure : GlucoseProfile::Kind::Glucose; }

QVariantList AppController::glucoseProfilePoints(bool bp) const
{
    QVariantList l;
    for (const GlucoseProfile::Point &p : GlucoseProfile::points(profileKind(bp)))
        l << QVariantMap{ { QStringLiteral("label"), p.label }, { QStringLiteral("context"), p.context },
                          { QStringLiteral("time"), hhmm(p.time) }, { QStringLiteral("active"), p.active } };
    return l;
}

int AppController::startGlucoseProfile(qint64 pid, const QString &isoDay, const QVariantList &points, bool sound, bool bp)
{
    const GlucoseProfile::Kind k = profileKind(bp);
    QList<GlucoseProfile::Point> pts = GlucoseProfile::points(k);
    for (int i = 0; i < pts.size() && i < points.size(); ++i) {
        const QVariantMap m = points.at(i).toMap();
        pts[i].active = m.value(QStringLiteral("active")).toBool();
        const QTime t = fromHhmm(m.value(QStringLiteral("time")));
        if (t.isValid()) pts[i].time = t;
    }
    GlucoseProfile::savePoints(pts, k);
    QString err;
    const int n = GlucoseProfile::create(pid, QDate::fromString(isoDay, Qt::ISODate), pts, sound, &err, k);
    if (n < 0) setError(err);
    bump();
    return n;
}

int AppController::stopGlucoseProfile(qint64 pid, const QString &isoDay, bool bp)
{
    const int n = GlucoseProfile::cancel(pid, QDate::fromString(isoDay, Qt::ISODate), profileKind(bp));
    bump();
    return n;
}

int AppController::glucoseProfileOpen(qint64 pid, const QString &isoDay, bool bp)
{
    int n = 0;
    for (const Appointment &a : GlucoseProfile::existing(pid, QDate::fromString(isoDay, Qt::ISODate), profileKind(bp)))
        if (!a.done) ++n;
    return n;
}

QVariantMap AppController::dueProfileMeasurement()
{
    if (!loggedIn()) return {};
    const QDateTime now = QDateTime::currentDateTime();
    for (const Appointment &a : Database::instance().appointments(now.addSecs(-90 * 60), now.addSecs(5 * 60), Session::user(), 0, false)) {
        if (!GlucoseProfile::isProfileAppointment(a) || a.done || a.start > now.addSecs(5 * 60) || a.start < now.addSecs(-90 * 60)) continue;
        const auto p = Database::instance().patient(a.patientId);
        if (!p) continue;
        return { { QStringLiteral("id"), a.id }, { QStringLiteral("patientId"), a.patientId },
                 { QStringLiteral("context"), GlucoseProfile::contextOf(a) }, { QStringLiteral("label"), GlucoseProfile::labelOf(a) },
                 { QStringLiteral("subject"), GlucoseProfile::subjectOf(a) },
                 { QStringLiteral("bp"), GlucoseProfile::kindOf(a) == GlucoseProfile::Kind::BloodPressure },
                 { QStringLiteral("person"), p->displayName() } };
    }
    return {};
}

QVariantMap AppController::quarterDue()
{
    QDate q;
    if (!loggedIn() || !QuarterReminder::due(QDate::currentDate(), &q)) return {};
    return { { QStringLiteral("key"), QuarterReminder::key(q) }, { QStringLiteral("title"), QuarterReminder::title(q) },
             { QStringLiteral("message"), QuarterReminder::message(q, QDate::currentDate()) } };
}

QVariantMap AppController::quarterBanner()
{
    const QDate q = QuarterReminder::relevantChange(QDate::currentDate());
    if (!loggedIn() || !QuarterReminder::enabled() || !q.isValid() || QuarterReminder::confirmed(q)) return {};
    return { { QStringLiteral("title"), QuarterReminder::title(q) },
             { QStringLiteral("message"), QuarterReminder::message(q, QDate::currentDate()) } };
}

void AppController::confirmQuarter()
{
    const QDate q = QuarterReminder::relevantChange(QDate::currentDate());
    if (q.isValid()) QuarterReminder::confirm(q);
    bump();
}

void AppController::snoozeQuarter()
{
    QuarterReminder::snoozeUntil(QDate::currentDate().addDays(1));
}

bool AppController::quarterReminderEnabled() const { return QuarterReminder::enabled(); }
void AppController::setQuarterReminderEnabled(bool on) { QuarterReminder::setEnabled(on); bump(); }
