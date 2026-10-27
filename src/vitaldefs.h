// VitaParms – zentrale Beschreibung der Vitalparameter
// (Beschriftung, Einheit, Nachkommastellen, Farbe/Markerform für Diagramme)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QColor>
#include <QList>
#include <QString>
#include <optional>

enum class VitalKey {
    Pulse, Systolic, Diastolic, Temperature, SpO2, Glucose, Weight, Height, Bmi, PeakFlow, Stool
};

enum class MarkerShape { Circle, Square, Triangle, Diamond, Cross, TriangleDown };

namespace VitalDefs {

QList<VitalKey> all();
QString      key(VitalKey k);          // stabiler Schlüssel, z. B. "pulse"
QString      label(VitalKey k);        // z. B. "Puls"
QString      shortLabel(VitalKey k);   // z. B. "RR sys."
QString      unit(VitalKey k);         // berücksichtigt Blutzucker-Einheit
int          decimals(VitalKey k);
QColor       color(VitalKey k);        // farbfehlsichtigkeits-taugliche Palette
MarkerShape  marker(VitalKey k);

// Wert in Anzeigeeinheit (Blutzucker ggf. in mmol/l umgerechnet)
std::optional<double> value(const VitalRecord &r, VitalKey k);
QString formatted(const VitalRecord &r, VitalKey k, bool withUnit = false);

// Umrechnung Blutzucker
constexpr double kMgDlPerMmolL = 18.016;
double glucoseToDisplay(double mgdl);
double glucoseFromDisplay(double v);

// Diagramm-Bereiche (jeweils eigene y-Achse)
struct Panel {
    QString         id;
    QString         title;
    QList<VitalKey> keys;
};
QList<Panel> panels();

} // namespace VitalDefs
