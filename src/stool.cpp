// SPDX-License-Identifier: MIT
#include "stool.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace Stool {

static QString tr(const char *s) { return QCoreApplication::translate("Stool", s); }

QString typeName(int type)
{
    if (type >= 1 && type <= kTypes) return tr("Typ %1").arg(type);
    return tr("ohne Angabe");
}

QString description(int type)
{
    switch (type) {
    case 1: return tr("einzelne feste Kügelchen, schwer auszuscheiden");
    case 2: return tr("wurstartig, klumpig");
    case 3: return tr("wurstartig mit rissiger Oberfläche");
    case 4: return tr("wurstartig, glatt und weich");
    case 5: return tr("weiche Klümpchen mit glatten Rändern");
    case 6: return tr("breiig, flockige Stücke mit ausgefransten Rändern");
    case 7: return tr("wässrig, ohne feste Bestandteile");
    default: return tr("Art nicht angegeben");
    }
}

QString choiceText(int type)
{
    if (type >= 1 && type <= kTypes) return typeName(type) + QStringLiteral(" – ") + description(type);
    return tr("ohne nähere Angabe");
}

QString shortText(int type)
{
    return type >= 1 && type <= kTypes ? typeName(type) : tr("ja");
}

Group group(int type)
{
    if (type == 1 || type == 2) return Group::Hard;
    if (type == 3 || type == 4) return Group::Normal;
    if (type >= 5 && type <= 7) return Group::Soft;
    return Group::Unspecified;
}

QString groupName(Group g)
{
    switch (g) {
    case Group::Hard:   return tr("hart (Hinweis auf Verstopfung)");
    case Group::Normal: return tr("normal");
    case Group::Soft:   return tr("weich bis flüssig (Hinweis auf Durchfall)");
    case Group::Unspecified: break;
    }
    return tr("ohne Angabe");
}

QColor groupColor(Group g, bool dark)
{
    // Okabe-Ito: auch bei Rot-Grün-Schwäche unterscheidbar
    switch (g) {
    case Group::Hard:   return dark ? QColor(0xE6, 0x9F, 0x00) : QColor(0xC2, 0x7C, 0x00);   // Orange
    case Group::Normal: return dark ? QColor(0x2E, 0xB8, 0x8F) : QColor(0x00, 0x9E, 0x73);   // Blaugrün
    case Group::Soft:   return dark ? QColor(0x56, 0xB4, 0xE9) : QColor(0x00, 0x72, 0xB2);   // Blau
    case Group::Unspecified: break;
    }
    return dark ? QColor(0xA0, 0xA0, 0xA0) : QColor(0x8A, 0x8A, 0x8A);
}

QString scaleNote()
{
    return tr("Bristol-Stuhlformen-Skala: Typ 1–2 hart (Hinweis auf Verstopfung), Typ 3–4 normal, "
              "Typ 5–7 weich bis flüssig (Hinweis auf Durchfall).");
}

QStringList amountChoices()
{
    return { tr("wenig"), tr("mittel"), tr("viel") };
}

QStringList colorChoices()
{
    return { tr("braun"), tr("hellbraun"), tr("dunkelbraun"), tr("gelblich"), tr("grünlich"),
             tr("lehmfarben (grau-weiß)"), tr("schwarz (Teerstuhl)"), tr("rot / blutig") };
}

QStringList admixtureChoices()
{
    return { tr("keine"), tr("Schleim"), tr("Blut (hellrot)"), tr("unverdaute Nahrungsreste"), tr("Fett (glänzend)"),
             tr("übelriechend"), tr("säuerlich riechend") };
}

QString symptomsHint()
{
    return tr("z. B. Bauchschmerzen, Blähungen, starkes Pressen, Inkontinenz, Abführmittel");
}

bool needsAttention(const StoolDetail &d)
{
    const QStringList colors = colorChoices();
    const QString c = d.color.trimmed().toLower(), m = d.admixture.toLower();
    if (c == colors.value(6).toLower() || c == colors.value(7).toLower()) return true;
    static const QRegularExpression rx(QStringLiteral("\\b(schwarz|teer|blut|blutig|rot|black|tarry|blood|bloody|red)\\b"),
                                       QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return rx.match(c).hasMatch() || rx.match(m).hasMatch();
}

QString attentionText()
{
    return tr("Schwarzer (teerartiger) oder blutiger Stuhl sollte rasch ärztlich abgeklärt werden – "
              "sofern er nicht durch Eisentabletten, Kohle oder Lebensmittel (z. B. Rote Bete) erklärbar ist.");
}

QMap<QDate, Day> perDay(const QList<VitalRecord> &records, const QDate &from, const QDate &to)
{
    QMap<QDate, Day> out;
    for (const VitalRecord &r : records) {
        if (!r.stool) continue;
        const QDate d = r.measuredAt.date();
        if ((from.isValid() && d < from) || (to.isValid() && d > to)) continue;
        Day &day = out[d];
        ++day.count;
        day.types << *r.stool;
    }
    return out;
}

QList<Gap> gaps(const QMap<QDate, Day> &days, const QDate &from, const QDate &to, int minDays)
{
    QList<Gap> out;
    if (days.isEmpty()) return out;
    const QDate today = QDate::currentDate();
    auto add = [&](const QDate &a, const QDate &b) {      // a, b: Tage ohne Stuhlgang (einschließlich)
        const int n = int(a.daysTo(b)) + 1;
        // Sehr lange Lücken bedeuten eher "nicht erfasst" als "kein Stuhlgang"
        if (n < minDays || n > kTrackedDays) return;
        if ((to.isValid() && a > to) || (from.isValid() && b < from)) return;
        out << Gap{ a, b };
    };
    QDate prev;
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        if (prev.isValid() && prev.daysTo(it.key()) > 1)
            add(prev.addDays(1), it.key().addDays(-1));
        prev = it.key();
    }
    // vom letzten Stuhlgang bis heute (heute zählt mit, solange noch nichts eingetragen ist)
    if (prev.isValid() && prev < today) add(prev.addDays(1), today);
    return out;
}

Status status(const QList<VitalRecord> &records, const QDate &today)
{
    Status s;
    for (const VitalRecord &r : records) {
        if (!r.stool) continue;
        const QDate d = r.measuredAt.date();
        if (d > today) continue;
        if (!s.last.isValid() || d > s.last) s.last = d;
    }
    if (!s.last.isValid()) return s;
    s.daysWithout = int(s.last.daysTo(today));
    s.tracked = s.daysWithout <= kTrackedDays;
    s.warn = s.tracked && s.daysWithout >= kWarnDays;
    return s;
}

QString warningText(const Status &s)
{
    if (!s.warn) return {};
    return tr("Seit %1 Tagen kein Stuhlgang eingetragen (zuletzt am %2). Bitte auf Beschwerden achten, "
              "ausreichend trinken und bei anhaltender Verstopfung ärztlichen Rat einholen.")
        .arg(s.daysWithout).arg(s.last.toString(QStringLiteral("dd.MM.yyyy")));
}

QString statusText(const Status &s)
{
    if (!s.last.isValid()) return tr("kein Stuhlgang erfasst");
    if (s.daysWithout == 0) return tr("letzter Stuhlgang heute");
    if (s.daysWithout == 1) return tr("letzter Stuhlgang gestern");
    return tr("letzter Stuhlgang vor %1 Tagen").arg(s.daysWithout);
}

} // namespace Stool
