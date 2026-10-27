// VitaParms – Flüssigkeitshaushalt: Ein- und Ausfuhr (Bilanz), Trinkmenge, Gewichtszunahme, Ödeme
//
// Jede Messung kann eine Einfuhr (ml, Art) und eine Ausfuhr (ml, Art) enthalten; die Bilanz wird je
// Kalendertag summiert. Im Stammblatt lassen sich eine Mindest-Trinkmenge, eine ärztlich verordnete
// Trinkmengenbegrenzung und die Gewichtskontrolle (z. B. bei Herzschwäche: Warnung bei mehr als
// 2 kg Zunahme in 3 Tagen) einstellen. Ödeme werden nach der üblichen Dellentiefe in Grad 0–4 erfasst.
// VitaParms ist kein Medizinprodukt – die Hinweise ersetzen keine ärztliche Beurteilung.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QColor>
#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Fluid {

// Patient::extra
constexpr const char *kMinKey = "fluidMin";          // Mindest-Trinkmenge ml/Tag (0 = keine)
constexpr const char *kMaxKey = "fluidMax";          // Trinkmengenbegrenzung ml/Tag (0 = keine)
constexpr const char *kWeightKey = "weightWatch";    // Gewichtszunahme in kg (in kWeightDays), 0 = aus
constexpr int kWeightDays = 3;
constexpr double kDefaultWeightGain = 2.0;

int    minIntake(const Patient &p);
int    maxIntake(const Patient &p);
double weightWatch(const Patient &p);
void   setLimits(Patient &p, int minMl, int maxMl, double weightGainKg);

QStringList inKinds();        // Getränke, Suppe/flüssige Speisen, Sondenkost, Infusion, Sonstiges
QStringList outKinds();       // Urin, Erbrechen, Stuhl (flüssig), Drainage/Sonde, Sonstiges
QStringList quickAmounts();   // Tablet: 100, 150, 200, 250, 330, 500 ml

struct Day { int in = 0, out = 0, nIn = 0, nOut = 0; int balance() const { return in - out; } };
QMap<QDate, Day> perDay(const QList<VitalRecord> &records, const QDate &from = {}, const QDate &to = {});
QString signedMl(int ml);     // "+350 ml", "−120 ml"

// Hinweise für den Kopf des Hauptfensters / die Übersicht (jeweils leer, wenn nichts auffällt)
struct Notice { bool warn = false; QString text; };
Notice intakeNotice(const Patient &p, const QList<VitalRecord> &records, const QDate &today = QDate::currentDate());
Notice weightNotice(const Patient &p, const QList<VitalRecord> &records, const QDate &today = QDate::currentDate());
QString limitsText(const Patient &p);   // "Trinkmenge mind. 1.500 ml, höchstens 2.000 ml am Tag" ("" ohne Angaben)

// Ödeme (Dellentiefe nach Fingerdruck)
constexpr int kEdemaMax = 4;
QString edemaName(int grade);         // "Grad 2 – mäßig"
QString edemaDescription(int grade);  // "Delle bis 4 mm, verschwindet nach 10–15 s"
QString edemaShort(int grade);        // Journal: "Grad 2" bzw. "keine"
QStringList edemaSites();             // Knöchel/Füße, Unterschenkel, ganzes Bein …
QStringList edemaSides();             // beidseits, links, rechts
QString edemaText(const VitalRecord &r);   // "Grad 2, Unterschenkel, beidseits"
// Hinweise: Grad 3–4, Zunahme um 2 Grade, einseitige Beinschwellung (Thrombose?)
QString edemaAdvice(const VitalRecord &r, const std::optional<int> &previousGrade = std::nullopt);

} // namespace Fluid
