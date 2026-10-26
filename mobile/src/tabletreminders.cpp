// VitaParms Tablet – Termin-Erinnerungen als Android-Benachrichtigung (siehe tabletreminders.h)
// SPDX-License-Identifier: MIT
#include "tabletreminders.h"

#include "apptcore.h"
#include "database.h"
#include "session.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

TabletReminders::TabletReminders(QObject *parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(2000);
    connect(&m_debounce, &QTimer::timeout, this, &TabletReminders::scheduleNow);
}

QList<TabletReminders::Item> TabletReminders::plan(const QDateTime &now, int days, int max)
{
    QList<Item> l;
    if (!Session::isLoggedIn() || !Database::instance().isOpen()) return l;
    Database &db = Database::instance();
    QHash<qint64, QString> names;
    for (const Patient &p : db.patients()) names.insert(p.id, p.displayName());
    const QLocale loc;
    // Termine, deren Erinnerung in den Zeitraum fällt (Vorlauf bis zu einer Woche → etwas weiter suchen)
    const QList<Appointment> list = db.appointments(now, now.addDays(days + 8), Session::user(), 0, false);
    for (const Appointment &a : list) {
        if (a.done || !a.hasReminder() || (!a.remindVisual && !a.remindSound)) continue;
        const QDateTime at = a.reminderTime();
        if (!at.isValid() || at <= now || at > now.addDays(days)) continue;
        Item it;
        it.id = int(a.id & 0x7fffffff);
        it.at = at;
        it.title = tr("Termin: %1").arg(a.title);
        const qint64 d = QDate::currentDate().daysTo(a.start.date());
        QString when = d == 0 ? tr("heute") : d == 1 ? tr("morgen") : loc.toString(a.start.date(), QStringLiteral("ddd dd.MM."));
        if (!a.allDay) when += QLatin1Char(' ') + a.start.time().toString(QStringLiteral("HH:mm"));
        QStringList parts{ when };
        if (!a.location.isEmpty()) parts << a.location;
        if (a.patientId && names.contains(a.patientId)) parts << names.value(a.patientId);
        if (a.priority >= Appointment::High) parts << ApptUi::priorityName(a.priority);
        it.text = parts.join(QStringLiteral(" · "));
        it.sound = a.remindSound;
        l << it;
    }
    std::sort(l.begin(), l.end(), [](const Item &x, const Item &y) { return x.at < y.at; });
    if (l.size() > max) l = l.mid(0, max);
    return l;
}

void TabletReminders::scheduleSoon()
{
    m_debounce.start();
}

void TabletReminders::scheduleNow()
{
    const QList<Item> items = plan(QDateTime::currentDateTime());
    m_count = int(items.size());
#ifdef Q_OS_ANDROID
    QJsonArray arr;
    for (const Item &it : items)
        arr << QJsonObject{ { QStringLiteral("id"), it.id }, { QStringLiteral("at"), double(it.at.toMSecsSinceEpoch()) },
                            { QStringLiteral("title"), it.title }, { QStringLiteral("text"), it.text },
                            { QStringLiteral("sound"), it.sound } };
    const QJniObject ctx(QNativeInterface::QAndroidApplication::context());
    QJniObject::callStaticMethod<void>("de/mbergmann/vitaparms/Reminders", "ensureChannels",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
        ctx.object(), QJniObject::fromString(tr("Termin-Erinnerungen")).object<jstring>(),
        QJniObject::fromString(tr("Termin-Erinnerungen (ohne Ton)")).object<jstring>(),
        QJniObject::fromString(tr("Erinnerungen an Termine und Medikamente aus VitaParms")).object<jstring>());
    QJniObject::callStaticMethod<void>("de/mbergmann/vitaparms/Reminders", "replaceAll",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        ctx.object(), QJniObject::fromString(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))).object<jstring>(),
        QJniObject::fromString(tr("Termin-Erinnerung")).object<jstring>());
#endif
}

void TabletReminders::requestPermission()
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<jboolean>("de/mbergmann/vitaparms/Reminders", "ensurePermission",
        "(Landroid/content/Context;)Z", QJniObject(QNativeInterface::QAndroidApplication::context()).object());
#endif
}

bool TabletReminders::permissionGranted() const
{
#ifdef Q_OS_ANDROID
    return QJniObject::callStaticMethod<jboolean>("de/mbergmann/vitaparms/Reminders", "permissionGranted",
        "(Landroid/content/Context;)Z", QJniObject(QNativeInterface::QAndroidApplication::context()).object());
#else
    return true;
#endif
}
