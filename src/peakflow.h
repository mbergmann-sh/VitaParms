// VitaParms – Peak Flow: Sollwert (Normwerttabelle nach EGKS/ERS 1994) und individuelle Warnhinweise
//
// Bezugswert ist der persönliche Bestwert, wenn er im Stammblatt eingetragen ist (so empfehlen es
// die Asthma-Leitlinien), sonst der Sollwert für Geschlecht, Alter und Körpergröße. Die Bereiche
// folgen dem üblichen Ampelschema: grün ab 80 %, gelb 50–79 %, rot unter 50 % des Bezugswerts.
// VitaParms ist kein Medizinprodukt – die Hinweise ersetzen nicht den ärztlich vereinbarten
// Asthma-Aktionsplan.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QColor>
#include <QString>
#include <optional>

namespace PeakFlow {

constexpr const char *kBestKey = "peakFlowBest";        // Patient::extra – persönlicher Bestwert (l/min)
// Biologisches Geschlecht für Normwerte (Patient::extra "bioSex" = "m"/"w"). Gespeichert wird es nur,
// wenn es vom Feld „Geschlecht“ abweicht oder dieses „divers“/leer ist – sonst folgt es dem Geschlecht.
constexpr const char *kBioSexKey = "bioSex";

// Tabelle: Alter 5–80 Jahre, Körpergröße 135–195 cm; dazwischen linear interpoliert,
// über 80 Jahre gilt die Zeile für 80 Jahre. Außerhalb (jünger als 5, kleiner/größer) kein Sollwert.
constexpr int kMinAge = 5, kMaxAge = 80, kMinHeight = 135, kMaxHeight = 195;
std::optional<int> normValue(QChar sex, double ageYears, double heightCm);

int  personalBest(const Patient &p);                    // 0 = keiner
void setPersonalBest(Patient &p, int litersPerMinute);  // 0 entfernt
QChar sexFromField(const QString &sex);                 // 'm'/'w' aus „Geschlecht“, sonst QChar()
QChar bioSexSetting(const Patient &p);                  // gespeicherte Abweichung oder QChar()
QChar biologicalSex(const Patient &p);                  // wirksam: Abweichung, sonst aus „Geschlecht“
void  setBiologicalSex(Patient &p, QChar sex);          // nach p.sex setzen; speichert nur Abweichungen
bool  bioSexDiffers(const Patient &p);                  // für den Ausdruck: weicht vom Geschlecht ab

enum class Source { None, PersonalBest, Norm };
struct Reference {
    Source  source = Source::None;
    int     value = 0;          // l/min
    bool    capped = false;     // Alter über 80: Zeile für 80 Jahre
    QString description;        // "persönlicher Bestwert" / "Sollwert (ERS 1994 für 52 Jahre, männlich, 178 cm)"
    QString basis;              // nur Sollwert: "ERS 1994 für 52 Jahre, männlich, 178 cm"
    QString missing;            // warum es keinen Sollwert gibt ("" = gibt einen)
};
Reference reference(const Patient &p, const QDate &day, std::optional<double> heightCm);
Reference normReference(const Patient &p, const QDate &day, std::optional<double> heightCm);   // ohne Bestwert

enum class Zone { None, Green, Yellow, Red };
struct Assessment {
    Zone      zone = Zone::None;
    int       percent = 0;      // Messwert in % des Bezugswerts
    Reference ref;
    QString   text;             // ein Satz: Wert, Prozent, Bereich
    QString   advice;           // Handlungshinweis (gelb/rot), sonst leer
};
// "Bezugswert: Sollwert 525 l/min (ERS 1994 …) – gelb unter 420, rot unter 263 l/min." ("" ohne Bezugswert)
QString limitsText(const Reference &r);
Zone zoneFor(int percent);
Assessment assess(const Patient &p, int value, const QDate &day, std::optional<double> heightCm);

QString zoneName(Zone z);       // "grüner Bereich" …
QColor  zoneColor(Zone z, bool dark = false);   // Text auf hellem bzw. dunklem Grund (gelb = Orange, lesbar)
QString zoneKey(Zone z);        // "green", "yellow", "red", ""

} // namespace PeakFlow
