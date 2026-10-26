// VitaParms – Erinnerung an den Quartalswechsel (siehe quarterreminder.h)
// SPDX-License-Identifier: MIT
#include "quarterreminder.h"

#include "database.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>

namespace {
constexpr int kDays = 2;
}

namespace QuarterReminder {

QDate quarterStart(const QDate &d)
{
    return QDate(d.year(), ((d.month() - 1) / 3) * 3 + 1, 1);
}

QDate relevantChange(const QDate &today)
{
    // der nächste Quartalsbeginn höchstens zwei Tage voraus …
    const QDate ahead = quarterStart(today.addDays(kDays));
    if (ahead > today) return ahead;
    // … oder der aktuelle, wenn er höchstens zwei Tage zurückliegt
    const QDate cur = quarterStart(today);
    if (cur.daysTo(today) <= kDays) return cur;
    return {};
}

QString key(const QDate &q)
{
    return QStringLiteral("%1-Q%2").arg(q.year()).arg((q.month() - 1) / 3 + 1);
}

QString title(const QDate &q)
{
    return QCoreApplication::translate("QuarterReminder", "Quartalswechsel am %1 (%2. Quartal %3)")
        .arg(QLocale().toString(q, QStringLiteral("dd.MM.yyyy"))).arg((q.month() - 1) / 3 + 1).arg(q.year());
}

QString message(const QDate &q, const QDate &today)
{
    if (today < q)
        return QCoreApplication::translate("QuarterReminder", "Quartalswechsel steht bevor. Bitte denken Sie daran, Ihre KV-Karte in von Ihnen genutzten "
                  "Praxen einlesen zu lassen.");
    return QCoreApplication::translate("QuarterReminder", "Das neue Quartal hat begonnen. Bitte denken Sie daran, Ihre KV-Karte in von Ihnen genutzten "
              "Praxen einlesen zu lassen.");
}

bool enabled()
{
    return QSettings().value(QStringLiteral("reminders/quarter"), true).toBool();
}

void setEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("reminders/quarter"), on);
}

bool confirmed(const QDate &q)
{
    return Database::instance().setting(QStringLiteral("quarter/confirmed")) == key(q);
}

void confirm(const QDate &q)
{
    Database::instance().setSetting(QStringLiteral("quarter/confirmed"), key(q));
}

void snoozeUntil(const QDate &day)
{
    Database::instance().setSetting(QStringLiteral("quarter/snooze"), day.toString(Qt::ISODate));
}

bool due(const QDate &today, QDate *quarterBegin)
{
    if (!enabled() || !Database::instance().isOpen()) return false;
    const QDate q = relevantChange(today);
    if (!q.isValid() || confirmed(q)) return false;
    const QDate snooze = QDate::fromString(Database::instance().setting(QStringLiteral("quarter/snooze")), Qt::ISODate);
    if (snooze.isValid() && today < snooze) return false;
    if (quarterBegin) *quarterBegin = q;
    return true;
}

} // namespace QuarterReminder
