// VitaParms – BMI-Berechnung mit Amputationskorrektur
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include <optional>

namespace Bmi {

// Anteil des fehlenden Körpersegments am Gesamtkörpergewicht in Prozent.
// Werte nach Osterkamp LK, "Current perspective on assessment of human body
// proportions of relevance to amputees", J Am Diet Assoc 1995;95:215-218.
//   Hand 0,7 % · Unterarm mit Hand 2,3 % · ganzer Arm 5,0 %
//   Fuß 1,5 %  · Unterschenkel mit Fuß 5,9 % · ganzes Bein 16,0 %
double armPercent(ArmLevel l);
double legPercent(LegLevel l);
double missingPercent(const Amputations &a);

// Korrigiertes (geschätztes) Gewicht bei vollständigem Körper:
//   G_korr = G_gemessen / (1 - p/100)
double correctedWeight(double weightKg, const Amputations &a);

// BMI = G_korr / (Größe in m)²   – liefert nullopt bei fehlenden Werten
std::optional<double> compute(std::optional<double> weightKg,
                              std::optional<double> heightCm,
                              const Amputations &a);

// Grobe Einordnung nach WHO-Klassifikation (Erwachsene).
QString whoCategory(double bmi);

QString armLevelText(ArmLevel l);
QString legLevelText(LegLevel l);

// Kompakte Speicherform, z. B. "AL2;AR0;LL3;LR0"
QString encode(const Amputations &a);
Amputations decode(const QString &s);
QString describe(const Amputations &a); // menschenlesbar

} // namespace Bmi
