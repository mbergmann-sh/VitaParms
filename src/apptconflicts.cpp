// SPDX-License-Identifier: MIT
#include "apptconflicts.h"
#include "database.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>

#include <algorithm>

namespace ApptConflicts {

static QString tr(const char *s) { return QCoreApplication::translate("ApptConflicts", s); }

namespace {

qint64 minutesBetween(const QDateTime &a, const QDateTime &b) { return a.secsTo(b) / 60; }

QDateTime endOf(const Appointment &a)
{
    return a.end.isValid() && a.end > a.start ? a.end : a.start.addSecs(30 * 60);   // ohne Ende: 30 min
}

// belegte Zeiten (mit Mindestabstand erweitert) der gewählten Personen
struct Busy { QDateTime from, to; qint64 id; };

bool isFree(const QList<Busy> &busy, const QDateTime &s, const QDateTime &e, qint64 ignoreId)
{
    for (const Busy &b : busy) {
        if (b.id == ignoreId) continue;
        if (s < b.to && b.from < e) return false;
    }
    return true;
}

} // namespace

Result check(const Options &o)
{
    Result r;
    Database &db = Database::instance();
    QList<qint64> pids;
    for (qint64 id : o.patients) if (id && !pids.contains(id)) pids << id;
    if (pids.size() < 2) {
        r.error = tr("Bitte mindestens zwei Personen auswählen.");
        return r;
    }
    if (!o.from.isValid() || !o.to.isValid() || o.to < o.from) {
        r.error = tr("Bitte einen gültigen Zeitraum wählen.");
        return r;
    }
    QHash<qint64, QString> names;
    for (qint64 id : pids)
        if (const auto p = db.patient(id)) { names.insert(id, p->displayName()); r.persons << p->displayName(); }

    const QDateTime from(o.from, QTime(0, 0)), to(o.to.addDays(1), QTime(0, 0));
    // für Vorschläge auch die Tage nach dem Zeitraum kennen
    const QDateTime busyTo(o.to.addDays(std::max(1, o.searchDays) + 1), QTime(0, 0));
    QList<Appointment> all, busyList;
    for (qint64 id : pids) {
        for (const Appointment &a : db.appointments(from, busyTo, o.owner, id, false)) {
            if (a.allDay || a.done || !a.start.isValid()) continue;
            busyList << a;
            if (a.start < to && endOf(a) > from) all << a;
        }
    }
    r.checked = int(all.size());
    std::sort(all.begin(), all.end(), [](const Appointment &x, const Appointment &y) { return x.start < y.start; });
    const qint64 buf = std::max(0, o.bufferMinutes);
    QList<Busy> busy;
    for (const Appointment &a : std::as_const(busyList))
        busy << Busy{ a.start.addSecs(-buf * 60), endOf(a).addSecs(buf * 60), a.id };

    for (int i = 0; i < all.size(); ++i) {
        for (int j = i + 1; j < all.size(); ++j) {
            const Appointment &a = all[i], &b = all[j];
            if (b.start >= endOf(a).addSecs(buf * 60)) {
                // sortiert nach Beginn: spätere Termine können a nur noch treffen, wenn a länger dauert
                if (b.start >= endOf(a).addSecs(buf * 60 + 24 * 3600)) break;
                continue;
            }
            if (a.patientId == b.patientId) continue;          // nur zwischen verschiedenen Personen
            Conflict c;
            c.a = a; c.b = b;
            c.personA = names.value(a.patientId);
            c.personB = names.value(b.patientId);
            c.overlapMinutes = int(minutesBetween(b.start, std::min(endOf(a), endOf(b))));
            // verschoben wird der weniger wichtige, bei gleicher Priorität der spätere Termin
            c.moveB = b.priority <= a.priority;
            const Appointment &mv = c.moveB ? b : a;
            const qint64 dur = std::max<qint64>(15, mv.start.secsTo(endOf(mv)) / 60);
            // Suche in 15-Minuten-Schritten ab dem Tag des Termins
            for (int d = 0; d <= std::max(1, o.searchDays) && c.alternatives.size() < o.suggestions; ++d) {
                const QDate day = mv.start.date().addDays(d);
                if (o.weekdaysOnly && day.dayOfWeek() > 5) continue;
                if (day < QDate::currentDate()) continue;
                for (QDateTime s(day, o.dayStart); c.alternatives.size() < o.suggestions; s = s.addSecs(15 * 60)) {
                    const QDateTime e = s.addSecs(dur * 60);
                    if (e > QDateTime(day, o.dayEnd) || s.date() != day) break;
                    if (s <= QDateTime::currentDateTime()) continue;
                    if (s == mv.start) continue;
                    if (isFree(busy, s, e, mv.id)) {
                        c.alternatives << s;
                        s = e.addSecs(-15 * 60);                  // nächster Vorschlag nicht überlappend
                    }
                }
            }
            r.conflicts << c;
        }
    }
    return r;
}

QString when(const Appointment &a)
{
    const QLocale loc;
    return loc.toString(a.start, QStringLiteral("ddd dd.MM.yyyy HH:mm")) + QStringLiteral("–")
           + loc.toString(endOf(a).time(), QStringLiteral("HH:mm"));
}

QString overlapText(const Conflict &c, int bufferMinutes)
{
    if (c.overlapMinutes > 0) return tr("%1 min Überschneidung").arg(c.overlapMinutes);
    return tr("nur %1 min Abstand (gewünscht: %2 min)").arg(-c.overlapMinutes).arg(bufferMinutes);
}

QString alternativesText(const Conflict &c)
{
    if (c.alternatives.isEmpty()) return tr("kein freier Termin gefunden");
    const QLocale loc;
    const Appointment &m = c.moveB ? c.b : c.a;
    const qint64 secs = std::max<qint64>(15 * 60, m.start.secsTo(endOf(m)));
    QStringList l;
    for (const QDateTime &s : c.alternatives)
        l << loc.toString(s, QStringLiteral("ddd dd.MM. HH:mm")) + QStringLiteral("–") + loc.toString(s.addSecs(secs).time(), QStringLiteral("HH:mm"));
    return l.join(QLatin1Char('\n'));
}

QString movedTitle(const Conflict &c)
{
    const Appointment &m = c.moveB ? c.b : c.a;
    return tr("„%1“ (%2)").arg(m.title, c.moveB ? c.personB : c.personA);
}

QString optionsText(const Options &o)
{
    const QLocale loc;
    QString t = tr("Zeitraum %1 bis %2").arg(loc.toString(o.from, QStringLiteral("dd.MM.yyyy")), loc.toString(o.to, QStringLiteral("dd.MM.yyyy")));
    if (o.bufferMinutes > 0) t += QStringLiteral(", ") + tr("Mindestabstand %1 min").arg(o.bufferMinutes);
    t += QStringLiteral(", ") + tr("Vorschläge %1–%2 Uhr%3")
             .arg(o.dayStart.toString(QStringLiteral("HH:mm")), o.dayEnd.toString(QStringLiteral("HH:mm")),
                  o.weekdaysOnly ? tr(", Mo–Fr") : QString());
    return t;
}

} // namespace ApptConflicts
