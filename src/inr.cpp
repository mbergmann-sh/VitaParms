// SPDX-License-Identifier: MIT
#include "inr.h"

#include <QCoreApplication>
#include <QLocale>

namespace Inr {

static QString tr(const char *s) { return QCoreApplication::translate("Inr", s); }
static QString num(double v) { return QLocale().toString(v, 'f', 1); }

Range range(const Patient &p)
{
    Range r;
    const double lo = p.extra.value(QLatin1String(kLowKey)).toDouble();
    const double hi = p.extra.value(QLatin1String(kHighKey)).toDouble();
    if (lo > 0 && hi > lo) { r.low = lo; r.high = hi; r.personal = true; }
    return r;
}

void setRange(Patient &p, double low, double high)
{
    if (low > 0 && high > low) {
        p.extra.insert(QLatin1String(kLowKey), qRound(low * 10) / 10.0);
        p.extra.insert(QLatin1String(kHighKey), qRound(high * 10) / 10.0);
    } else {
        p.extra.remove(QLatin1String(kLowKey));
        p.extra.remove(QLatin1String(kHighKey));
    }
}

QString rangeText(const Range &r)
{
    return r.personal ? tr("Zielbereich %1–%2").arg(num(r.low), num(r.high))
                      : tr("Zielbereich %1–%2 (Standard, im Stammblatt änderbar)").arg(num(r.low), num(r.high));
}

Assessment assess(const Patient &p, double value)
{
    Assessment a;
    a.range = range(p);
    if (value <= 0) return a;
    const QString v = num(value);
    const QString rg = QStringLiteral("%1–%2").arg(num(a.range.low), num(a.range.high));
    if (value >= kVeryHigh && value > a.range.high) {
        a.zone = Zone::VeryHigh;
        a.text = tr("INR %1 – deutlich über dem Zielbereich %2.").arg(v, rg);
        a.advice = tr("Stark erhöhte Blutungsneigung: Bitte umgehend die behandelnde Praxis anrufen. Bei Blutungen, "
                      "Blut im Urin oder Stuhl, starken Kopfschmerzen: Notruf 112.");
    } else if (value > a.range.high) {
        a.zone = Zone::High;
        a.text = tr("INR %1 – über dem Zielbereich %2.").arg(v, rg);
        a.advice = tr("Erhöhte Blutungsneigung. Die Dosis nur nach ärztlicher Anweisung ändern; auf Blutungszeichen achten.");
    } else if (value < a.range.low) {
        a.zone = Zone::Low;
        a.text = tr("INR %1 – unter dem Zielbereich %2.").arg(v, rg);
        a.advice = tr("Die Gerinnung ist weniger gehemmt als vorgesehen (Thrombosegefahr). Die Dosis nur nach "
                      "ärztlicher Anweisung ändern.");
    } else {
        a.zone = Zone::InRange;
        a.text = tr("INR %1 – im Zielbereich %2.").arg(v, rg);
    }
    return a;
}

QString zoneName(Zone z)
{
    switch (z) {
    case Zone::InRange: return tr("im Zielbereich");
    case Zone::Low: return tr("unter dem Zielbereich");
    case Zone::High: return tr("über dem Zielbereich");
    case Zone::VeryHigh: return tr("deutlich über dem Zielbereich");
    case Zone::None: break;
    }
    return {};
}

QColor zoneColor(Zone z, bool dark)
{
    switch (z) {
    case Zone::InRange: return dark ? QColor(0x4C, 0xC3, 0x8A) : QColor(0x1B, 0x7F, 0x4B);
    case Zone::Low:
    case Zone::High: return dark ? QColor(0xF0, 0xB3, 0x5E) : QColor(0xB3, 0x5C, 0x00);
    case Zone::VeryHigh: return dark ? QColor(0xF2, 0x86, 0x7B) : QColor(0xD5, 0x2B, 0x1E);
    case Zone::None: break;
    }
    return {};
}

QString zoneKey(Zone z)
{
    switch (z) {
    case Zone::InRange: return QStringLiteral("in");
    case Zone::Low: return QStringLiteral("low");
    case Zone::High: return QStringLiteral("high");
    case Zone::VeryHigh: return QStringLiteral("veryHigh");
    case Zone::None: break;
    }
    return {};
}

} // namespace Inr
