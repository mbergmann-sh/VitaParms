// SPDX-License-Identifier: MIT
#include "bmi.h"

#include <QCoreApplication>
#include <QLocale>
#include <QStringList>

namespace Bmi {

double armPercent(ArmLevel l)
{
    switch (l) {
    case ArmLevel::Hand:            return 0.7;
    case ArmLevel::ForearmWithHand: return 2.3;
    case ArmLevel::WholeArm:        return 5.0;
    case ArmLevel::None:            break;
    }
    return 0.0;
}

double legPercent(LegLevel l)
{
    switch (l) {
    case LegLevel::Foot:             return 1.5;
    case LegLevel::LowerLegWithFoot: return 5.9;
    case LegLevel::WholeLeg:         return 16.0;
    case LegLevel::None:             break;
    }
    return 0.0;
}

double missingPercent(const Amputations &a)
{
    return armPercent(a.armLeft) + armPercent(a.armRight)
         + legPercent(a.legLeft) + legPercent(a.legRight);
}

double correctedWeight(double weightKg, const Amputations &a)
{
    const double p = missingPercent(a);
    if (p <= 0.0 || p >= 100.0)
        return weightKg;
    return weightKg / (1.0 - p / 100.0);
}

std::optional<double> compute(std::optional<double> weightKg,
                              std::optional<double> heightCm,
                              const Amputations &a)
{
    if (!weightKg || !heightCm || *weightKg <= 0.0 || *heightCm <= 0.0)
        return std::nullopt;
    const double m = *heightCm / 100.0;
    return correctedWeight(*weightKg, a) / (m * m);
}

QString whoCategory(double bmi)
{
    if (bmi < 16.0)  return QCoreApplication::translate("Bmi", "starkes Untergewicht");
    if (bmi < 17.0)  return QCoreApplication::translate("Bmi", "mäßiges Untergewicht");
    if (bmi < 18.5)  return QCoreApplication::translate("Bmi", "leichtes Untergewicht");
    if (bmi < 25.0)  return QCoreApplication::translate("Bmi", "Normalgewicht");
    if (bmi < 30.0)  return QCoreApplication::translate("Bmi", "Präadipositas");
    if (bmi < 35.0)  return QCoreApplication::translate("Bmi", "Adipositas Grad I");
    if (bmi < 40.0)  return QCoreApplication::translate("Bmi", "Adipositas Grad II");
    return QCoreApplication::translate("Bmi", "Adipositas Grad III");
}

QString armLevelText(ArmLevel l)
{
    switch (l) {
    case ArmLevel::Hand:            return QCoreApplication::translate("Bmi", "Hand");
    case ArmLevel::ForearmWithHand: return QCoreApplication::translate("Bmi", "Unterarm mit Hand");
    case ArmLevel::WholeArm:        return QCoreApplication::translate("Bmi", "ganzer Arm");
    case ArmLevel::None:            break;
    }
    return QCoreApplication::translate("Bmi", "vollständig");
}

QString legLevelText(LegLevel l)
{
    switch (l) {
    case LegLevel::Foot:             return QCoreApplication::translate("Bmi", "Fuß");
    case LegLevel::LowerLegWithFoot: return QCoreApplication::translate("Bmi", "Unterschenkel mit Fuß");
    case LegLevel::WholeLeg:         return QCoreApplication::translate("Bmi", "ganzes Bein");
    case LegLevel::None:             break;
    }
    return QCoreApplication::translate("Bmi", "vollständig");
}

QString encode(const Amputations &a)
{
    return QStringLiteral("AL%1;AR%2;LL%3;LR%4")
        .arg(int(a.armLeft)).arg(int(a.armRight))
        .arg(int(a.legLeft)).arg(int(a.legRight));
}

Amputations decode(const QString &s)
{
    Amputations a;
    const QStringList parts = s.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        if (p.size() < 3) continue;
        const QString key = p.left(2);
        const int v = qBound(0, p.mid(2).toInt(), 3);
        if (key == QLatin1String("AL")) a.armLeft  = ArmLevel(v);
        else if (key == QLatin1String("AR")) a.armRight = ArmLevel(v);
        else if (key == QLatin1String("LL")) a.legLeft  = LegLevel(v);
        else if (key == QLatin1String("LR")) a.legRight = LegLevel(v);
    }
    return a;
}

QString describe(const Amputations &a)
{
    if (!a.any())
        return QCoreApplication::translate("Bmi", "keine");
    QStringList l;
    if (a.armLeft  != ArmLevel::None) l << QCoreApplication::translate("Bmi", "links: %1").arg(armLevelText(a.armLeft));
    if (a.armRight != ArmLevel::None) l << QCoreApplication::translate("Bmi", "rechts: %1").arg(armLevelText(a.armRight));
    if (a.legLeft  != LegLevel::None) l << QCoreApplication::translate("Bmi", "links: %1").arg(legLevelText(a.legLeft));
    if (a.legRight != LegLevel::None) l << QCoreApplication::translate("Bmi", "rechts: %1").arg(legLevelText(a.legRight));
    return l.join(QStringLiteral("; "))
         + QCoreApplication::translate("Bmi", " (Korrektur %1 %)")
               .arg(QLocale().toString(missingPercent(a), 'f', 1));
}

} // namespace Bmi
