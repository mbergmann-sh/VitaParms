// VitaParms – Gerinnungswert INR (bei Vitamin-K-Antagonisten wie Phenprocoumon/Marcumar, Warfarin)
//
// Bewertet wird gegen den ärztlich festgelegten Zielbereich aus dem Stammblatt; ist keiner
// eingetragen, gilt der häufigste Standard 2,0–3,0. VitaParms ist kein Medizinprodukt – die
// Dosis wird nur nach ärztlicher Anweisung geändert.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QColor>
#include <QString>

namespace Inr {

constexpr const char *kLowKey = "inrLow";     // Patient::extra – Zielbereich unten
constexpr const char *kHighKey = "inrHigh";   // Patient::extra – Zielbereich oben
constexpr double kDefaultLow = 2.0, kDefaultHigh = 3.0;
constexpr double kVeryHigh = 5.0;             // ab hier deutlich erhöhte Blutungsgefahr
constexpr double kMin = 0.5, kMax = 12.0;     // Eingabebereich

struct Range {
    double low = kDefaultLow, high = kDefaultHigh;
    bool   personal = false;                  // im Stammblatt eingetragen
};
Range range(const Patient &p);
void  setRange(Patient &p, double low, double high);   // low/high 0 → Eintrag entfernen
QString rangeText(const Range &r);            // "Zielbereich 2,0–3,0 (Standard)"

enum class Zone { None, InRange, Low, High, VeryHigh };
struct Assessment {
    Zone    zone = Zone::None;
    Range   range;
    QString text;                             // "INR 3,6 – über dem Zielbereich 2,0–3,0."
    QString advice;                           // Hinweis außerhalb des Zielbereichs
};
Assessment assess(const Patient &p, double value);
QString zoneName(Zone z);
QColor  zoneColor(Zone z, bool dark = false);
QString zoneKey(Zone z);                      // "in", "low", "high", "veryHigh", ""

} // namespace Inr
