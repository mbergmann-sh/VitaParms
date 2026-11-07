// VitaParms – Terminkollisions-Check (nur VitaParms und VitaParms mobile, nicht light)
//
// Vergleicht nachträglich die Termine von zwei oder mehr Personen einer Datenbank in einem Zeitraum:
// Termine verschiedener Personen, die sich zeitlich überschneiden (oder näher beieinander liegen als
// der gewünschte Mindestabstand, z. B. für die Fahrt), werden als Kollision gemeldet. Für jede
// Kollision schlägt der Check freie Ausweichtermine vor – gleiche Dauer, innerhalb der Tageszeit
// „von … bis“, an Werktagen, ohne Überschneidung mit einem Termin einer der gewählten Personen.
// Verschoben werden soll der Termin mit der niedrigeren Priorität, bei gleicher der spätere.
// Ganztägige und erledigte Termine bleiben außen vor.
// Übersetzungskontext „ApptConflicts“.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QTime>

namespace ApptConflicts {

struct Options {
    QList<qint64> patients;          // mindestens zwei
    QDate from, to;                  // einschließlich
    int bufferMinutes = 0;           // Mindestabstand zwischen den Terminen
    QTime dayStart{ 8, 0 }, dayEnd{ 18, 0 };   // Rahmen für Vorschläge
    bool weekdaysOnly = true;        // Vorschläge nur Montag bis Freitag
    int suggestions = 3;             // Vorschläge je Kollision
    int searchDays = 14;             // so viele Tage ab dem Termin wird gesucht
    QString owner;                   // leer = alle Konten (Administrator), sonst nur dieses Konto
};

struct Conflict {
    Appointment a, b;                // a beginnt zuerst
    QString personA, personB;
    int overlapMinutes = 0;          // > 0: echte Überschneidung; ≤ 0: Abstand kleiner als der Mindestabstand
    bool moveB = true;               // Vorschläge gelten für b (sonst für a)
    QList<QDateTime> alternatives;   // Beginn der vorgeschlagenen Ausweichtermine (Dauer wie der Termin)
};

struct Result {
    QList<Conflict> conflicts;
    int checked = 0;                 // geprüfte Termine
    QStringList persons;             // Namen der gewählten Personen
    QString error;                   // z. B. „weniger als zwei Personen“
};

Result check(const Options &o);

// Texte für Anzeige und Ausdruck
QString when(const Appointment &a);                  // "Mo 09.11.2026 10:00–11:00"
QString overlapText(const Conflict &c, int bufferMinutes);   // "überschneidet sich 30 min" / "nur 10 min Abstand"
QString alternativesText(const Conflict &c);         // eine Zeile je Vorschlag "Di 10.11. 09:00–09:45" / "kein freier Termin gefunden"
QString movedTitle(const Conflict &c);               // Titel und Person des Termins, der verschoben werden soll
QString optionsText(const Options &o);               // Zeitraum, Abstand, Rahmen für Vorschläge

} // namespace ApptConflicts
