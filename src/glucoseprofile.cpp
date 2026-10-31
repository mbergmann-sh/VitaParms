// VitaParms – BZ- und RR-Tagesprofil (siehe glucoseprofile.h)
// SPDX-License-Identifier: MIT
#include "glucoseprofile.h"

#include "database.h"
#include "session.h"

#include <QCoreApplication>
#include <QSettings>

namespace GlucoseProfile {

namespace {
// extra des Termins: Messzeitpunkt bzw. Bezeichnung des Zeitpunkts
const char *kKey = "bzProfile", *kLabelKey = "bzLabel";
const char *kRrKey = "rrProfile", *kRrLabelKey = "rrLabel";

QString t(const char *ctx, const char *s) { return QCoreApplication::translate(ctx, s); }
const char *key(Kind k) { return k == Kind::BloodPressure ? kRrKey : kKey; }
const char *labelKey(Kind k) { return k == Kind::BloodPressure ? kRrLabelKey : kLabelKey; }
} // namespace

QList<Point> defaultPoints(Kind k)
{
    if (k == Kind::BloodPressure)
        return {
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Morgens nach dem Aufstehen (vor den Tabletten)")),
              t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "morgens")), QTime(7, 0) },
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vormittags")),
              t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "vormittags")), QTime(10, 0) },
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Mittags")),
              t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "mittags")), QTime(13, 0) },
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Nachmittags")),
              t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "nachmittags")), QTime(16, 0) },
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Abends")),
              t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "abends")), QTime(19, 0) },
            { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Zubettgehen")), t("AppSettings", "vor dem Schlafengehen"), QTime(22, 0) },
        };
    return {
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Nüchtern vor dem Frühstück")), t("AppSettings", "nüchtern"), QTime(7, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Frühstück")), t("AppSettings", "2 h nach dem Frühstück"), QTime(9, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Mittagessen")), t("AppSettings", "vor dem Mittagessen"), QTime(12, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Mittagessen")), t("AppSettings", "2 h nach dem Mittagessen"), QTime(14, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Abendessen")), t("AppSettings", "vor dem Abendessen"), QTime(18, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Abendessen")), t("AppSettings", "2 h nach dem Abendessen"), QTime(20, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Zubettgehen")), t("AppSettings", "vor dem Schlafengehen"), QTime(22, 0) },
    };
}

QString settingsKey(Kind k)
{
    return k == Kind::BloodPressure ? QStringLiteral("rrprofile") : QStringLiteral("bzprofile");
}

QList<Point> points(Kind k)
{
    QList<Point> l = defaultPoints(k);
    QSettings s;
    const QStringList times = s.value(settingsKey(k) + QStringLiteral("/times")).toStringList();
    const QStringList off = s.value(settingsKey(k) + QStringLiteral("/off")).toStringList();
    for (int i = 0; i < l.size(); ++i) {
        const QTime tm = QTime::fromString(times.value(i), QStringLiteral("HH:mm"));
        if (tm.isValid()) l[i].time = tm;
        l[i].active = !off.contains(QString::number(i));
    }
    return l;
}

void savePoints(const QList<Point> &l, Kind k)
{
    QStringList times, off;
    for (int i = 0; i < l.size(); ++i) {
        times << l[i].time.toString(QStringLiteral("HH:mm"));
        if (!l[i].active) off << QString::number(i);
    }
    QSettings s;
    s.setValue(settingsKey(k) + QStringLiteral("/times"), times);
    s.setValue(settingsKey(k) + QStringLiteral("/off"), off);
}

QString subject(Kind k)
{
    return k == Kind::BloodPressure ? QCoreApplication::translate("GlucoseProfile", "RR-Tagesprofil")
                                    : QCoreApplication::translate("GlucoseProfile", "BZ-Tagesprofil");
}

QString title(const Point &p, Kind k)
{
    return QStringLiteral("%1 %2 – %3").arg(subject(k), p.time.toString(QStringLiteral("HH:mm")), p.label);
}

bool isProfileAppointment(const Appointment &a)
{
    return a.extra.contains(QLatin1String(kKey)) || a.extra.contains(QLatin1String(kRrKey));
}

Kind kindOf(const Appointment &a)
{
    return a.extra.contains(QLatin1String(kRrKey)) ? Kind::BloodPressure : Kind::Glucose;
}

QString contextOf(const Appointment &a)
{
    return a.extra.value(QLatin1String(key(kindOf(a)))).toString();
}

QString labelOf(const Appointment &a)
{
    return a.extra.value(QLatin1String(labelKey(kindOf(a)))).toString();
}

QString subjectOf(const Appointment &a)
{
    return QStringLiteral("%1 %2").arg(subject(kindOf(a)), a.start.time().toString(QStringLiteral("HH:mm")));
}

QString focusField(Kind k)
{
    return k == Kind::BloodPressure ? QStringLiteral("systolicSpin") : QStringLiteral("glucoseSpin");
}

QList<Appointment> existing(qint64 patientId, const QDate &day, Kind k)
{
    QList<Appointment> l;
    const QDateTime from(day, QTime(0, 0));
    for (const Appointment &a : Database::instance().appointments(from, from.addDays(1), {}, patientId, true))
        if (isProfileAppointment(a) && kindOf(a) == k && a.start.date() == day) l << a;
    return l;
}

int cancel(qint64 patientId, const QDate &day, Kind k)
{
    int n = 0;
    for (const Appointment &a : existing(patientId, day, k))
        if (!a.done && Database::instance().deleteAppointment(a.id)) ++n;
    return n;
}

int create(qint64 patientId, const QDate &day, const QList<Point> &pts, bool sound, QString *error, Kind k)
{
    cancel(patientId, day, k);
    const QDateTime now = QDateTime::currentDateTime();
    int n = 0;
    for (const Point &p : pts) {
        if (!p.active || !p.time.isValid()) continue;
        Appointment a;
        a.patientId = patientId;
        a.owner = Session::user();
        a.createdBy = Session::user();
        a.title = title(p, k);
        a.start = QDateTime(day, p.time);
        if (a.start < now) continue;                 // vergangene Zeitpunkte nicht anlegen
        a.end = a.start.addSecs(15 * 60);
        a.priority = Appointment::High;
        a.remindMinutes = 0;
        a.remindVisual = true;
        a.remindSound = sound;
        a.notes = k == Kind::BloodPressure
            ? QCoreApplication::translate("GlucoseProfile", "Blutdruck und Puls messen: %1 – nach 5 Minuten Ruhe im Sitzen, "
                                                            "immer am selben Arm").arg(p.label)
            : QCoreApplication::translate("GlucoseProfile", "Blutzucker messen: %1").arg(p.label);
        a.extra.insert(QLatin1String(key(k)), p.context);
        a.extra.insert(QLatin1String(labelKey(k)), p.label);
        if (!Database::instance().saveAppointment(a, error)) return -1;
        ++n;
    }
    return n;
}

} // namespace GlucoseProfile
