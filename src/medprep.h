// VitaParms – Medikamente stellen: gemeinsame Fachlogik für den Assistenten am Computer
// (MedicationAssistant) und in der Tablet-App (Seite „Medikamente stellen“).
//
//  * Auswahl: welche Medikamente an einem Tag bzw. in einer Woche (Wochendosierer) und zu
//    einem Zeitpunkt zu stellen sind; Bedarfs-, Notfallmedikation und „Sonstiges“ werden nur
//    genannt, nicht gestellt.
//  * Vorlesetexte (Tagesdosierer, Wochendosierer mit zusammengefassten Tagen und Wochenmenge).
//  * Protokoll und Abbuchung aus dem Bestand (MedStock) nach dem Stellen.
// Übersetzungskontext „MedPrep“.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace MedPrep {

QString slotName(int slot);                                 // morgens, mittags, abends, zur Nacht
QString doseOf(const Medication &m, int slot);              // "" = keine Gabe
QString unitSuffix(const Medication &m);                    // " Stück" bzw. ""

// Im Plan aktiv (nicht archiviert, Beginn/Ende) an diesem Tag
QList<Medication> currentMedications(const QList<Medication> &all, const QDate &day);
// An diesem Tag zu geben (aktiv und laut Einnahme-Rhythmus fällig)
bool givenOn(const Medication &m, const QDate &day);
// Wochendosierer: an mindestens einem Tag des Zeitraums zum Zeitpunkt zu geben
bool dueInRange(const Medication &m, const QDate &start, int days, int slot);

struct Selection {
    QList<Medication> list;       // zu stellen, in der Reihenfolge des Tages (zuerst morgens)
    QStringList notDue;           // an diesem Tag / in diesen Tagen nicht fällig (Klartext)
    QStringList asNeeded, other, emergency;
};
Selection select(const QList<Medication> &source, const QDate &day, int days, int slot);

// Vorlesetexte
QString speechText(const Medication &m, int slot);
QString weekSpeechText(const Medication &m, const QDate &start, int days, int slot);
QString textFor(const Medication &m, const QDate &day, int days, int slot);
QString doseSummary(const Medication &m, int slot);         // "morgens: 1 Stück, abends: 1 Stück"
QString introText(const Patient &p, const QDate &day, int days, int slot, int count);
QString nothingText(int days);                              // "Für diesen Tag und Zeitpunkt ist nichts zu stellen."
QString doneText();                                         // "Medikamente sind gerichtet."

// Protokoll schreiben und die bestätigten Medikamente aus dem Bestand abbuchen
struct LogResult {
    bool ok = false;
    qint64 id = 0;
    QStringList shortage;         // Bestand reichte nicht
    QString error;
};
LogResult log(const Patient &p, const QString &by, const QList<Medication> &list, const QSet<int> &confirmed,
              bool complete, int slot, const QDate &day, int days, const QString &note);
QString statusText(const MedPreparation &e);                // "✔ vollständig" / "abgebrochen (2 von 5)"

} // namespace MedPrep
