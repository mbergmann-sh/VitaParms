// VitaParms – Zuordnung von Medikamenten zu Gruppen (BtM, Schmerzmittel, Blutverdünner)
// anhand von Wirkstoff- und Handelsnamen; je Medikament manuell korrigierbar.
// Grundlage BtM: Anlage III BtMG einschließlich der „ausgenommenen Zubereitungen“
// (z. B. Codein ≤ 100 mg je Einheit, Tilidin retard mit Naloxon). Medizinal-Cannabis und
// Dronabinol sind seit dem MedCanG (04/2024) keine Betäubungsmittel mehr. Ohne Gewähr.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <optional>

namespace DrugClass {

enum Group { Btm = 0, Pain, Anticoag, Antihypertensive, Corticosteroid, GroupCount };
QString groupName(Group g);            // "Betäubungsmittel (BtM)", …
QString groupKey(Group g);             // "btm", "pain", "anticoag", "bp", "cortisone" (für Einstellungen/Overrides)

struct Hit {
    bool match = false;
    bool uncertain = false;            // hängt von Zubereitung/Dosis ab ("ggf.")
    bool manual = false;               // vom Anwender festgelegt
    QString reason;                    // "Oxycodon – Opioid (BtM, Anlage III BtMG)"
    QString subclass;                  // "Opioid", "NSAR", "DOAK", …
};

Hit classifyAuto(const Medication &m, Group g);
Hit classify(const Medication &m, Group g);                 // mit manueller Festlegung
std::optional<bool> override(const Medication &m, Group g);
void setOverride(Medication &m, Group g, std::optional<bool> value);

// Mengen
std::optional<double> parseDose(const QString &s);          // "1", "½", "1/2", "1,5", "0.5"
struct Strength { double value = 0; QString unit; bool perTime = false; bool ok = false; };
Strength parseStrength(const QString &s);                    // "7,5 mg", "20 000 IE", "12 µg/h"
struct DailyAmount {
    std::optional<double> units;       // Einheiten (Stück, Hub …) pro Einnahmetag
    QString unitsText;                 // "3 Stück"
    QString activeText;                // "30 mg Wirkstoff" (wenn berechenbar)
    QString perText;                   // "pro Tag", "pro Einnahmetag (1× wöchentlich)", "je Gabe bei Bedarf"
};
DailyAmount dailyAmount(const Medication &m);
QString formatNumber(double v);

} // namespace DrugClass
