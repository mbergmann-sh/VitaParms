// SPDX-License-Identifier: MIT
#include "fluid.h"

#include <QCoreApplication>
#include <QLocale>
#include <algorithm>
#include <cmath>

namespace Fluid {

static QString tr(const char *s) { return QCoreApplication::translate("Fluid", s); }
static QString ml(int v) { return QLocale().toString(v) + QStringLiteral(" ml"); }

int minIntake(const Patient &p) { return p.extra.value(QLatin1String(kMinKey)).toInt(); }
int maxIntake(const Patient &p) { return p.extra.value(QLatin1String(kMaxKey)).toInt(); }
double weightWatch(const Patient &p) { return p.extra.value(QLatin1String(kWeightKey)).toDouble(); }

void setLimits(Patient &p, int minMl, int maxMl, double weightGainKg)
{
    auto put = [&](const char *k, const QJsonValue &v, bool on) {
        if (on) p.extra.insert(QLatin1String(k), v); else p.extra.remove(QLatin1String(k));
    };
    put(kMinKey, minMl, minMl > 0);
    put(kMaxKey, maxMl, maxMl > 0);
    put(kWeightKey, std::round(weightGainKg * 10) / 10.0, weightGainKg > 0);
}

QStringList inKinds()
{
    return { tr("Getränke"), tr("Suppe/flüssige Speisen"), tr("Sondenkost"), tr("Infusion"), tr("Sonstiges") };
}

QStringList outKinds()
{
    return { tr("Urin"), tr("Erbrechen"), tr("Stuhl (flüssig)"), tr("Drainage/Sonde"), tr("Sonstiges") };
}

QStringList quickAmounts()
{
    return { QStringLiteral("100"), QStringLiteral("150"), QStringLiteral("200"), QStringLiteral("250"),
             QStringLiteral("330"), QStringLiteral("500") };
}

QMap<QDate, Day> perDay(const QList<VitalRecord> &records, const QDate &from, const QDate &to)
{
    QMap<QDate, Day> out;
    for (const VitalRecord &r : records) {
        if (!r.fluidIn && !r.fluidOut) continue;
        const QDate d = r.measuredAt.date();
        if ((from.isValid() && d < from) || (to.isValid() && d > to)) continue;
        Day &day = out[d];
        if (r.fluidIn) { day.in += *r.fluidIn; ++day.nIn; }
        if (r.fluidOut) { day.out += *r.fluidOut; ++day.nOut; }
    }
    return out;
}

QString signedMl(int v)
{
    if (v > 0) return QStringLiteral("+") + ml(v);
    if (v < 0) return QStringLiteral("−") + ml(-v);
    return ml(0);
}

QString limitsText(const Patient &p)
{
    const int lo = minIntake(p), hi = maxIntake(p);
    if (lo && hi) return tr("Trinkmenge mindestens %1, höchstens %2 am Tag").arg(ml(lo), ml(hi));
    if (lo) return tr("Trinkmenge mindestens %1 am Tag").arg(ml(lo));
    if (hi) return tr("Trinkmenge höchstens %1 am Tag").arg(ml(hi));
    return {};
}

Notice intakeNotice(const Patient &p, const QList<VitalRecord> &records, const QDate &today)
{
    Notice n;
    const int lo = minIntake(p), hi = maxIntake(p);
    if (!lo && !hi) return n;
    const auto days = perDay(records, today.addDays(-1), today);
    // Begrenzung: heute schon überschritten
    if (hi && days.value(today).in > hi) {
        n.warn = true;
        n.text = tr("Trinkmenge heute %1 – mehr als die verordneten %2.").arg(ml(days.value(today).in), ml(hi));
        return n;
    }
    // Mindestmenge: gestern (vollständiger Tag) zu wenig – nur wenn gestern überhaupt Einfuhr erfasst wurde
    const Day y = days.value(today.addDays(-1));
    if (lo && y.nIn > 0 && y.in < lo) {
        n.warn = true;
        n.text = tr("Trinkmenge gestern nur %1 (mindestens %2 vorgesehen). Bitte ausreichend trinken.").arg(ml(y.in), ml(lo));
    }
    return n;
}

Notice weightNotice(const Patient &p, const QList<VitalRecord> &records, const QDate &today)
{
    Notice n;
    const double limit = weightWatch(p);
    if (limit <= 0) return n;
    // letztes Gewicht der letzten Tage vs. niedrigstes Gewicht in den kWeightDays Tagen davor
    const VitalRecord *last = nullptr;
    for (const VitalRecord &r : records)
        if (r.weight && r.measuredAt.date() <= today && r.measuredAt.date() >= today.addDays(-1)
            && (!last || r.measuredAt > last->measuredAt)) last = &r;
    if (!last) return n;
    const QDate d = last->measuredAt.date();
    double lowest = 0;
    QDate lowDay;
    for (const VitalRecord &r : records) {
        if (!r.weight) continue;
        const QDate rd = r.measuredAt.date();
        if (rd >= d.addDays(-kWeightDays) && rd < d && (lowest == 0 || *r.weight < lowest)) { lowest = *r.weight; lowDay = rd; }
    }
    if (lowest <= 0) return n;
    const double gain = *last->weight - lowest;
    if (gain + 1e-9 >= limit) {
        const QLocale loc;
        n.warn = true;
        n.text = tr("Gewicht in %1 Tagen um %2 kg gestiegen (%3 → %4 kg). Das kann auf Wassereinlagerung hindeuten – "
                    "bitte die behandelnde Praxis informieren.")
                     .arg(int(lowDay.daysTo(d))).arg(loc.toString(gain, 'f', 1), loc.toString(lowest, 'f', 1),
                                                     loc.toString(*last->weight, 'f', 1));
    }
    return n;
}

// ---------------------------------------------------------------- Ödeme

QString edemaName(int g)
{
    switch (g) {
    case 0: return tr("keine Ödeme");
    case 1: return tr("Grad 1 – leicht");
    case 2: return tr("Grad 2 – mäßig");
    case 3: return tr("Grad 3 – deutlich");
    case 4: return tr("Grad 4 – schwer");
    default: return {};
    }
}

QString edemaDescription(int g)
{
    switch (g) {
    case 0: return tr("keine Delle nach Fingerdruck");
    case 1: return tr("Delle bis 2 mm, verschwindet sofort");
    case 2: return tr("Delle bis 4 mm, verschwindet nach 10–15 Sekunden");
    case 3: return tr("Delle bis 6 mm, bleibt bis zu einer Minute");
    case 4: return tr("Delle 8 mm und tiefer, bleibt 2–5 Minuten");
    default: return {};
    }
}

QString edemaShort(int g)
{
    return g <= 0 ? tr("keine") : tr("Grad %1").arg(g);
}

QStringList edemaSites()
{
    return { tr("Knöchel/Füße"), tr("Unterschenkel"), tr("ganzes Bein"), tr("Hände/Arme"),
             tr("Gesicht/Augenlider"), tr("Kreuzbein/Rücken"), tr("Bauch") };
}

QStringList edemaSides()
{
    return { tr("beidseits"), tr("links"), tr("rechts") };
}

QString edemaText(const VitalRecord &r)
{
    if (!r.edema) return {};
    QStringList t{ edemaShort(*r.edema) };
    if (*r.edema > 0) {
        if (!r.edemaSite.isEmpty()) t << r.edemaSite;
        if (!r.edemaSide.isEmpty()) t << r.edemaSide;
    }
    return t.join(QStringLiteral(", "));
}

QString edemaAdvice(const VitalRecord &r, const std::optional<int> &previous)
{
    if (!r.edema || *r.edema <= 0) return {};
    QStringList a;
    const QStringList sites = edemaSites(), sides = edemaSides();
    const bool leg = r.edemaSite == sites.value(1) || r.edemaSite == sites.value(2) || r.edemaSite == sites.value(0);
    const bool oneSide = r.edemaSide == sides.value(1) || r.edemaSide == sides.value(2);
    if (leg && oneSide)
        a << tr("Eine einseitige, neu aufgetretene Beinschwellung kann auf eine Thrombose hindeuten – "
                "bitte rasch ärztlich abklären, bei Atemnot oder Brustschmerz Notruf 112.");
    if (r.edemaSite == sites.value(4))
        a << tr("Schwellungen im Gesicht oder an den Augenlidern bitte ärztlich abklären – bei Atemnot oder "
                "Schwellung von Zunge oder Hals sofort Notruf 112.");
    if (*r.edema >= 3)
        a << tr("Deutliche Ödeme bitte ärztlich abklären, besonders bei gleichzeitiger Gewichtszunahme oder Atemnot.");
    else if (previous && *r.edema - *previous >= 2)
        a << tr("Die Ödeme haben deutlich zugenommen – bitte ärztlich abklären.");
    return a.join(QLatin1Char('\n'));
}

} // namespace Fluid
