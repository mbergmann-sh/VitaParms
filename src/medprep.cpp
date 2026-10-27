// SPDX-License-Identifier: MIT
#include "medprep.h"
#include "database.h"
#include "medschedule.h"
#include "medstock.h"
#include "speech.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>

#include <algorithm>

namespace MedPrep {

static QString tr(const char *s) { return QCoreApplication::translate("MedPrep", s); }

QString slotName(int s)
{
    switch (s) {
    case 0: return tr("morgens");
    case 1: return tr("mittags");
    case 2: return tr("abends");
    default: return tr("zur Nacht");
    }
}

QString doseOf(const Medication &m, int s)
{
    const QString d = (s == 0 ? m.morning : s == 1 ? m.noon : s == 2 ? m.evening : m.night).trimmed();
    return (d.isEmpty() || d == QLatin1String("0") || d == QLatin1String("-")) ? QString() : d;
}

QString unitSuffix(const Medication &m)
{
    return m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit;
}

QList<Medication> currentMedications(const QList<Medication> &all, const QDate &day)
{
    QList<Medication> l;
    for (const Medication &m : all) {
        if (m.archived) continue;
        if (m.startDate.isValid() && m.startDate > day) continue;
        if (m.endDate.isValid() && m.endDate < day) continue;
        l << m;
    }
    return l;
}

bool givenOn(const Medication &m, const QDate &day)
{
    return !currentMedications({ m }, day).isEmpty() && MedSchedule::isDueOn(m, day);
}

bool dueInRange(const Medication &m, const QDate &start, int days, int slot)
{
    for (int i = 0; i < days; ++i) {
        if (!givenOn(m, start.addDays(i))) continue;
        for (int s = 0; s < 4; ++s)
            if ((slot < 0 || s == slot) && !doseOf(m, s).isEmpty()) return true;
    }
    return false;
}

Selection select(const QList<Medication> &source, const QDate &day, int nd, int slot)
{
    Selection r;
    nd = std::max(1, nd);
    // aktiv ist ein Medikament, wenn es an mindestens einem Tag des Zeitraums gegeben wird
    QList<Medication> all;
    for (int i = 0; i < nd; ++i)
        for (const Medication &m : currentMedications(source, day.addDays(i)))
            if (std::none_of(all.begin(), all.end(), [&](const Medication &x) { return x.id == m.id && x.name == m.name; }))
                all << m;
    const QLocale loc;
    for (const Medication &m : std::as_const(all)) {
        if (m.category != Medication::Regular) {
            const QString t = m.name + (m.rhythmNote.isEmpty() ? QString() : QStringLiteral(" (") + m.rhythmNote + QLatin1Char(')'));
            (m.category == Medication::Emergency ? r.emergency : m.category == Medication::Other ? r.other : r.asNeeded) << t;
            continue;
        }
        if (nd > 1) {   // Wochendosierer
            if (dueInRange(m, day, nd, slot)) r.list << m;
            else r.notDue << tr("%1 (%2)").arg(m.name, MedSchedule::describe(m));
            continue;
        }
        if (!MedSchedule::isDueOn(m, day)) {
            const QDate n = MedSchedule::nextDue(m, day.addDays(1));
            r.notDue << (n.isValid() ? tr("%1 (%2, nächste Gabe %3)").arg(m.name, MedSchedule::describe(m),
                                                                        loc.toString(n, QStringLiteral("ddd dd.MM.")))
                                     : tr("%1 (%2)").arg(m.name, MedSchedule::describe(m)));
            continue;
        }
        if (slot < 0 || !doseOf(m, slot).isEmpty()) r.list << m;
    }
    // In der Reihenfolge des Tages vorlesen: zuerst, was morgens genommen wird
    auto firstSlot = [](const Medication &m) {
        for (int s = 0; s < 4; ++s) if (!doseOf(m, s).isEmpty()) return s;
        return 4;
    };
    std::stable_sort(r.list.begin(), r.list.end(), [&](const Medication &a, const Medication &b) {
        const int fa = firstSlot(a), fb = firstSlot(b);
        return fa != fb ? fa < fb : a.name.localeAwareCompare(b.name) < 0;
    });
    return r;
}

QString doseSummary(const Medication &m, int slot)
{
    QStringList l;
    for (int s = 0; s < 4; ++s) {
        if (slot >= 0 && s != slot) continue;
        const QString d = doseOf(m, s);
        if (d.isEmpty()) continue;
        l << slotName(s) + QStringLiteral(": ") + d + unitSuffix(m);
    }
    return l.join(QStringLiteral(", "));
}

QString speechText(const Medication &m, int slot)
{
    QStringList parts;
    QString head = m.name;
    if (!m.strength.isEmpty()) head += QStringLiteral(", ") + m.strength;
    if (!m.form.isEmpty()) head += QStringLiteral(", ") + m.form;
    parts << head + QLatin1Char('.');
    QStringList doses;
    for (int s = 0; s < 4; ++s) {
        if (slot >= 0 && s != slot) continue;
        const QString d = doseOf(m, s);
        if (d.isEmpty()) continue;
        doses << slotName(s) + QStringLiteral(": ") + d + unitSuffix(m);
    }
    if (doses.isEmpty())
        parts << tr("Keine feste Dosis angegeben.");
    else
        parts << doses.join(QStringLiteral(". ")) + QLatin1Char('.');
    if (MedSchedule::isInterval(m))
        parts << tr("Einnahme: %1.").arg(MedSchedule::describe(m));
    if (!m.rhythmNote.isEmpty())
        parts << tr("Bemerkung: %1.").arg(m.rhythmNote);
    if (!m.notes.isEmpty()) parts << tr("Hinweis: %1.").arg(m.notes);
    QString t = parts.join(QLatin1Char(' '));
    t.replace(QStringLiteral(".."), QStringLiteral("."));
    return Speech::expand(t);
}

QString weekSpeechText(const Medication &m, const QDate &start, int days, int slot)
{
    QStringList parts;
    QString head = m.name;
    if (!m.strength.isEmpty()) head += QStringLiteral(", ") + m.strength;
    if (!m.form.isEmpty()) head += QStringLiteral(", ") + m.form;
    parts << head + QLatin1Char('.');
    const QLocale loc;
    // Dosis je Tag als Text; gleiche aufeinanderfolgende Tage zusammenfassen
    auto dayText = [&](const QDate &d) {
        if (!givenOn(m, d)) return QString();
        QStringList l;
        for (int s = 0; s < 4; ++s) {
            if (slot >= 0 && s != slot) continue;
            const QString x = doseOf(m, s);
            if (!x.isEmpty()) l << slotName(s) + QLatin1Char(' ') + x + unitSuffix(m);
        }
        return l.join(QStringLiteral(", "));
    };
    QStringList groups;
    int i = 0;
    while (i < days) {
        const QString t = dayText(start.addDays(i));
        int j = i;
        while (j + 1 < days && dayText(start.addDays(j + 1)) == t) ++j;
        if (!t.isEmpty()) {
            const QString from = loc.dayName(start.addDays(i).dayOfWeek());
            const QString to = loc.dayName(start.addDays(j).dayOfWeek());
            groups << (i == j ? from : j - i + 1 == days ? tr("Alle %1 Tage").arg(days) : tr("%1 bis %2").arg(from, to))
                          + QStringLiteral(": ") + t;
        }
        i = j + 1;
    }
    if (groups.isEmpty())
        parts << tr("In diesen Tagen nicht zu geben.");
    else
        parts << groups.join(QStringLiteral(". ")) + QLatin1Char('.');
    double total = 0;
    for (int k = 0; k < days; ++k) total += MedStock::doseOn(m, start.addDays(k), slot);
    if (total > 0)
        parts << tr("Insgesamt %1%2.").arg(MedStock::qtyText(total), unitSuffix(m));
    if (!m.rhythmNote.isEmpty())
        parts << tr("Bemerkung: %1.").arg(m.rhythmNote);
    if (!m.notes.isEmpty()) parts << tr("Hinweis: %1.").arg(m.notes);
    QString t = parts.join(QLatin1Char(' '));
    t.replace(QStringLiteral(".."), QStringLiteral("."));
    return Speech::expand(t);
}

QString textFor(const Medication &m, const QDate &day, int days, int slot)
{
    return days > 1 ? weekSpeechText(m, day, days, slot) : speechText(m, slot);
}

QString introText(const Patient &p, const QDate &day, int days, int slot, int count)
{
    const QLocale loc;
    const QString when = day == QDate::currentDate() ? QString() : QStringLiteral(", ") + loc.toString(day, tr("dddd, d. MMMM"));
    const QString who = p.firstName.isEmpty() ? p.lastName : p.firstName + QLatin1Char(' ') + p.lastName;
    return days > 1
        ? tr("Wochendosierer für %1: %2 bis %3%4. %5 Medikamente.")
              .arg(who, loc.toString(day, tr("dddd, d. MMMM")), loc.toString(day.addDays(days - 1), tr("dddd, d. MMMM")),
                   slot < 0 ? QString() : QStringLiteral(", ") + slotName(slot))
              .arg(count)
        : tr("Medikamente stellen für %1%2. %3 Medikamente.")
              .arg(who, when + (slot < 0 ? QString() : QStringLiteral(", ") + slotName(slot)))
              .arg(count);
}

QString nothingText(int days)
{
    return days > 1 ? tr("Für diese Tage und diesen Zeitpunkt ist nichts zu stellen.")
                    : tr("Für diesen Tag und Zeitpunkt ist nichts zu stellen.");
}

QString doneText()
{
    return tr("Medikamente sind gerichtet.");
}

LogResult log(const Patient &pat, const QString &by, const QList<Medication> &list, const QSet<int> &confirmed,
              bool complete, int slot, const QDate &day, int days, const QString &note)
{
    LogResult r;
    MedPreparation p;
    p.patientId = pat.id;
    p.preparedAt = QDateTime::currentDateTime();
    p.preparedBy = by;
    p.slot = slot;
    p.forDate = day;
    p.days = std::max(1, days);
    p.total = int(list.size());
    p.confirmed = complete ? p.total : int(confirmed.size());
    p.complete = complete;
    for (int i = 0; i < list.size(); ++i) {
        const Medication &m = list[i];
        QJsonObject o;
        o[QStringLiteral("name")] = m.name;
        if (!m.strength.isEmpty()) o[QStringLiteral("strength")] = m.strength;
        if (!m.form.isEmpty()) o[QStringLiteral("form")] = m.form;
        o[QStringLiteral("dose")] = p.days > 1 ? doseSummary(m, p.slot) + QStringLiteral(" · ") + tr("%1 Tage").arg(p.days)
                                               : doseSummary(m, p.slot);
        o[QStringLiteral("confirmed")] = complete || confirmed.contains(i);
        p.items.append(o);
    }
    p.note = note.trimmed();
    if (!Database::instance().addPreparation(p, &r.error)) return r;
    r.ok = true;
    r.id = p.id;
    // Bestandsführung: gestellte Menge abbuchen (nur bestätigte Medikamente)
    QList<Medication> booked;
    for (int i = 0; i < list.size(); ++i)
        if (complete || confirmed.contains(i)) booked << list[i];
    for (int d = 0; d < p.days; ++d)
        for (const QString &n : MedStock::bookPreparation(pat.id, booked, p.forDate.addDays(d), p.slot))
            if (!r.shortage.contains(n)) r.shortage << n;
    if (complete)   // Stell-Rhythmus merken (Bestandsführung rechnet damit)
        MedStock::setPrepPlan(pat.id, MedStock::PrepPlan{ p.days, p.forDate.dayOfWeek() });
    return r;
}

QString statusText(const MedPreparation &e)
{
    return e.complete ? QCoreApplication::translate("MedLogDialog", "✔ vollständig")
                      : QCoreApplication::translate("MedLogDialog", "abgebrochen (%1 von %2)").arg(e.confirmed).arg(e.total);
}

} // namespace MedPrep
