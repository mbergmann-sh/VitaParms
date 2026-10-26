// VitaParms – BZ-Tagesprofil (siehe glucoseprofile.h)
// SPDX-License-Identifier: MIT
#include "glucoseprofile.h"

#include "database.h"
#include "session.h"

#include <QCoreApplication>
#include <QSettings>

namespace GlucoseProfile {

namespace {
const char *kKey = "bzProfile";          // extra: Messzeitpunkt
const char *kLabelKey = "bzLabel";       // extra: Bezeichnung des Zeitpunkts
}

QList<Point> points()
{
    auto t = [](const char *ctx, const char *s) { return QCoreApplication::translate(ctx, s); };
    QList<Point> l{
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Nüchtern vor dem Frühstück")), t("AppSettings", "nüchtern"), QTime(7, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Frühstück")), t("AppSettings", "2 h nach dem Frühstück"), QTime(9, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Mittagessen")), t("AppSettings", "vor dem Mittagessen"), QTime(12, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Mittagessen")), t("AppSettings", "2 h nach dem Mittagessen"), QTime(14, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Abendessen")), t("AppSettings", "vor dem Abendessen"), QTime(18, 0) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "2 Stunden nach dem Abendessen")), t("AppSettings", "2 h nach dem Abendessen"), QTime(20, 30) },
        { t("GlucoseProfile", QT_TRANSLATE_NOOP("GlucoseProfile", "Vor dem Zubettgehen")), t("AppSettings", "vor dem Schlafengehen"), QTime(22, 0) },
    };
    QSettings s;
    const QStringList times = s.value(QStringLiteral("bzprofile/times")).toStringList();
    const QStringList off = s.value(QStringLiteral("bzprofile/off")).toStringList();
    for (int i = 0; i < l.size(); ++i) {
        const QTime tm = QTime::fromString(times.value(i), QStringLiteral("HH:mm"));
        if (tm.isValid()) l[i].time = tm;
        l[i].active = !off.contains(QString::number(i));
    }
    return l;
}

void savePoints(const QList<Point> &l)
{
    QStringList times, off;
    for (int i = 0; i < l.size(); ++i) {
        times << l[i].time.toString(QStringLiteral("HH:mm"));
        if (!l[i].active) off << QString::number(i);
    }
    QSettings s;
    s.setValue(QStringLiteral("bzprofile/times"), times);
    s.setValue(QStringLiteral("bzprofile/off"), off);
}

QString subject()
{
    return QCoreApplication::translate("GlucoseProfile", "BZ-Tagesprofil");
}

QString title(const Point &p)
{
    return QStringLiteral("%1 %2 – %3").arg(subject(), p.time.toString(QStringLiteral("HH:mm")), p.label);
}

bool isProfileAppointment(const Appointment &a)
{
    return a.extra.contains(QLatin1String(kKey));
}

QString contextOf(const Appointment &a)
{
    return a.extra.value(QLatin1String(kKey)).toString();
}

QString labelOf(const Appointment &a)
{
    return a.extra.value(QLatin1String(kLabelKey)).toString();
}

QList<Appointment> existing(qint64 patientId, const QDate &day)
{
    QList<Appointment> l;
    const QDateTime from(day, QTime(0, 0));
    for (const Appointment &a : Database::instance().appointments(from, from.addDays(1), {}, patientId, true))
        if (isProfileAppointment(a) && a.start.date() == day) l << a;
    return l;
}

int cancel(qint64 patientId, const QDate &day)
{
    int n = 0;
    for (const Appointment &a : existing(patientId, day))
        if (!a.done && Database::instance().deleteAppointment(a.id)) ++n;
    return n;
}

int create(qint64 patientId, const QDate &day, const QList<Point> &pts, bool sound, QString *error)
{
    cancel(patientId, day);
    const QDateTime now = QDateTime::currentDateTime();
    int n = 0;
    for (const Point &p : pts) {
        if (!p.active || !p.time.isValid()) continue;
        Appointment a;
        a.patientId = patientId;
        a.owner = Session::user();
        a.createdBy = Session::user();
        a.title = title(p);
        a.start = QDateTime(day, p.time);
        if (a.start < now) continue;                 // vergangene Zeitpunkte nicht anlegen
        a.end = a.start.addSecs(15 * 60);
        a.priority = Appointment::High;
        a.remindMinutes = 0;
        a.remindVisual = true;
        a.remindSound = sound;
        a.notes = QCoreApplication::translate("GlucoseProfile", "Blutzucker messen: %1").arg(p.label);
        a.extra.insert(QLatin1String(kKey), p.context);
        a.extra.insert(QLatin1String(kLabelKey), p.label);
        if (!Database::instance().saveAppointment(a, error)) return -1;
        ++n;
    }
    return n;
}

} // namespace GlucoseProfile
