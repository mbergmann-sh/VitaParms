// SPDX-License-Identifier: MIT
#include "recurrence.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>

namespace {

QString tr(const char *s) { return QCoreApplication::translate("Recurrence", s); }

const char *const kDayCodes[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

// Tag 'wd' (1 = Mo) im Monat von 'first' (1. des Monats): n-ter oder letzter
QDate weekdayInMonth(const QDate &first, int wd, int mode, int nth)
{
    if (mode == Recurrence::ByLastWeekday) {
        const QDate last(first.year(), first.month(), first.daysInMonth());
        return last.addDays(-((last.dayOfWeek() - wd + 7) % 7));
    }
    const QDate d = first.addDays((wd - first.dayOfWeek() + 7) % 7 + 7 * (nth - 1));
    return d.month() == first.month() ? d : QDate();
}

} // namespace

void Recurrence::normalize(const QDate &start)
{
    if (interval < 1) interval = 1;
    if (count < 0) count = 0;
    if (freq == Weekly) {
        weekdays &= 0x7f;
        weekdays |= 1 << (start.dayOfWeek() - 1);
    }
    if (freq == Monthly) {
        if (monthMode == ByNthWeekday && nthOfMonth(start) > 4) monthMode = ByLastWeekday;
        if (monthMode == ByLastWeekday && !isLastOfMonth(start)) monthMode = ByNthWeekday;
    }
    if (freq != Weekly) weekdays = 0;
    if (freq != Monthly) monthMode = ByMonthDay;
    if (until.isValid() && until < start) until = start;
}

QList<QDate> Recurrence::dates(const QDate &start, const QDate &to, int maxCount) const
{
    QList<QDate> out;
    if (!start.isValid()) return out;
    if (freq == None) {
        if (start <= to) out << start;
        return out;
    }
    QDate limit = to;
    if (until.isValid() && until < limit) limit = until;
    int n = 0;                         // gezählte Vorkommen (für COUNT)
    auto take = [&](const QDate &d) -> bool {   // false = Ende erreicht
        if (count > 0 && n >= count) return false;
        if (d > limit) return false;
        ++n;
        out << d;
        return int(out.size()) < maxCount;
    };
    const int step = std::max(1, interval);
    for (int k = 0; k < 100000; ++k) {
        switch (freq) {
        case Daily: {
            if (!take(start.addDays(qint64(k) * step))) return out;
            break;
        }
        case Weekly: {
            const QDate ws = start.addDays(-(start.dayOfWeek() - 1)).addDays(qint64(7) * k * step);
            if (ws > limit) return out;
            for (int i = 0; i < 7; ++i) {
                if (!(weekdays & (1 << i))) continue;
                const QDate d = ws.addDays(i);
                if (d < start) continue;
                if (!take(d)) return out;
            }
            break;
        }
        case Monthly: {
            const QDate first = QDate(start.year(), start.month(), 1).addMonths(k * step);
            if (first > limit) return out;
            QDate d;
            if (monthMode == ByMonthDay) {
                if (start.day() <= first.daysInMonth()) d = QDate(first.year(), first.month(), start.day());
            } else {
                d = weekdayInMonth(first, start.dayOfWeek(), monthMode, nthOfMonth(start));
            }
            if (d.isValid() && d >= start && !take(d)) return out;
            break;
        }
        case Yearly: {
            const int y = start.year() + k * step;
            if (QDate(y, 1, 1) > limit) return out;
            const QDate d(y, start.month(), start.day());   // 29.02. nur in Schaltjahren
            if (d.isValid() && !take(d)) return out;
            break;
        }
        case None:
            return out;
        }
    }
    return out;
}

QString Recurrence::ordinal(int n)
{
    if (QLocale().language() != QLocale::English) return QString::number(n) + QLatin1Char('.');
    const int t = n % 100;
    const char *suf = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return QString::number(n) + QLatin1String(suf);
}

QDate Recurrence::lastDate(const QDate &start) const
{
    if (freq == None) return start;
    if (isEndless()) return {};
    const QDate to = until.isValid() ? until : start.addYears(200);
    const QList<QDate> l = dates(start, to, count > 0 ? count : 100000);
    return l.isEmpty() ? start : l.last();
}

QString Recurrence::rrule(const QDate &start, const QString &untilValue) const
{
    static const char *const kFreq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY" };
    if (freq == None) return {};
    QStringList p;
    p << QStringLiteral("FREQ=") + QLatin1String(kFreq[freq]);
    if (interval > 1) p << QStringLiteral("INTERVAL=%1").arg(interval);
    if (freq == Weekly) {
        QStringList days;
        for (int i = 0; i < 7; ++i)
            if (weekdays & (1 << i)) days << QLatin1String(kDayCodes[i]);
        p << QStringLiteral("BYDAY=") + days.join(QLatin1Char(','));
        p << QStringLiteral("WKST=MO");
    } else if (freq == Monthly) {
        const QString code = QLatin1String(kDayCodes[start.dayOfWeek() - 1]);
        if (monthMode == ByMonthDay) p << QStringLiteral("BYMONTHDAY=%1").arg(start.day());
        else if (monthMode == ByNthWeekday) p << QStringLiteral("BYDAY=%1%2").arg(nthOfMonth(start)).arg(code);
        else p << QStringLiteral("BYDAY=-1") + code;
    }
    if (count > 0) p << QStringLiteral("COUNT=%1").arg(count);
    else if (until.isValid() && !untilValue.isEmpty()) p << QStringLiteral("UNTIL=") + untilValue;
    return p.join(QLatin1Char(';'));
}

QString Recurrence::describe(const QDate &start) const
{
    if (freq == None) return tr("keine Wiederholung");
    const QLocale loc;
    QString s;
    switch (freq) {
    case Daily:
        s = interval == 1 ? tr("täglich") : tr("alle %1 Tage").arg(interval);
        break;
    case Weekly: {
        if (weekdays == 0x1f && interval == 1) { s = tr("werktags (Mo–Fr)"); break; }
        QStringList days;
        for (int i = 0; i < 7; ++i)
            if (weekdays & (1 << i)) days << loc.dayName(i + 1, QLocale::ShortFormat);
        s = (interval == 1 ? tr("wöchentlich") : tr("alle %1 Wochen").arg(interval))
            + tr(" am %1").arg(days.join(QStringLiteral(", ")));
        break;
    }
    case Monthly: {
        const QString base = interval == 1 ? tr("monatlich") : tr("alle %1 Monate").arg(interval);
        const QString day = loc.dayName(start.dayOfWeek());
        if (monthMode == ByMonthDay) s = base + tr(" am %1.").arg(start.day());
        else if (monthMode == ByNthWeekday) s = base + tr(" am %1 %2").arg(ordinal(nthOfMonth(start)), day);
        else s = base + tr(" am letzten %1").arg(day);
        break;
    }
    case Yearly:
        s = (interval == 1 ? tr("jährlich") : tr("alle %1 Jahre").arg(interval))
            + tr(" am %1").arg(loc.toString(start, tr("d. MMMM")));
        break;
    case None:
        break;
    }
    if (count > 0) s += tr(", %1-mal").arg(count);
    else if (until.isValid()) s += tr(", bis %1").arg(loc.toString(until, QStringLiteral("dd.MM.yyyy")));
    return s;
}

QJsonObject Recurrence::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("freq")] = freq;
    o[QStringLiteral("interval")] = interval;
    if (weekdays) o[QStringLiteral("weekdays")] = weekdays;
    if (freq == Monthly) o[QStringLiteral("monthMode")] = monthMode;
    if (until.isValid()) o[QStringLiteral("until")] = until.toString(Qt::ISODate);
    if (count > 0) o[QStringLiteral("count")] = count;
    return o;
}

Recurrence Recurrence::fromJson(const QJsonObject &o)
{
    Recurrence r;
    r.freq = Freq(std::clamp(o.value(QStringLiteral("freq")).toInt(), 0, 4));
    r.interval = std::max(1, o.value(QStringLiteral("interval")).toInt(1));
    r.weekdays = o.value(QStringLiteral("weekdays")).toInt();
    r.monthMode = std::clamp(o.value(QStringLiteral("monthMode")).toInt(), 0, 2);
    r.until = QDate::fromString(o.value(QStringLiteral("until")).toString(), Qt::ISODate);
    r.count = std::max(0, o.value(QStringLiteral("count")).toInt());
    return r;
}

bool Recurrence::operator==(const Recurrence &o) const
{
    if (freq != o.freq) return false;
    if (freq == None) return true;
    return interval == o.interval && weekdays == o.weekdays && monthMode == o.monthMode
        && until == o.until && count == o.count;
}
