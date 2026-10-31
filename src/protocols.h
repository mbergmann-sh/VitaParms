// VitaParms – Protokolle aus den erfassten Messwerten: Stuhlprotokoll, BZ- und RR-Tagesprofile, Bilanz
//
// Beide Protokolle werden nicht eigens geführt, sondern aus dem Journal zusammengestellt
// (gefiltert, nach Datum und Uhrzeit sortiert, tabellarisch). Ausgabe als Druck/PDF (HTML)
// und als Excel-Tabelle; dieselben Tabellen nutzt die Tablet-App.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace Protocols {

enum class Kind { Stool, Glucose, Fluid, BloodPressure };

struct Table {
    enum RowFlag { Normal = 0, Muted = 1, Alert = 2, Summary = 3 };
    struct Row {
        QDateTime   when;            // Sortierung; bei Tagen ohne Eintrag 00:00
        QStringList cells;
        int         flag = Normal;
        QList<int>  alertCells;      // auffällige Zellen (fett/farbig)
        bool        spanAfterFirst = false;   // Zellen ab Spalte 1 zusammenfassen (Tage ohne Stuhlgang)
    };
    QString        title;            // „Stuhlprotokoll“
    QStringList    heads;
    QList<double>  widths;           // Spaltenbreiten in Prozent
    QList<Row>     rows;             // chronologisch
    QList<Row>     footer;           // Zusammenfassung (z. B. Mittelwerte)
    QStringList    notes;            // Absätze unter der Tabelle
    QString        summary;          // eine Zeile über der Tabelle
    int            entries = 0;      // Anzahl Stuhlgänge bzw. Messwerte
};

struct Options {
    QDate from, to;                  // einschließlich
    bool  showEmptyDays = true;      // Stuhlprotokoll: Tage ohne Stuhlgang aufführen
    int   minValues = 4;             // BZ/RR: nur Tage mit mindestens so vielen Werten (1 = alle Tage mit Werten)
};

QString title(Kind k);
Table stoolProtocol(const QList<VitalRecord> &records, const Options &o);
Table glucoseProfiles(const QList<VitalRecord> &records, const Options &o);
// RR-Tagesprofile: je Tag und Zeitpunkt Blutdruck und Puls, Mittelwerte je Zeitpunkt (Morgen-/Abendwerte)
Table bpProfiles(const QList<VitalRecord> &records, const Options &o);
// Ein- und Ausfuhr: Einträge chronologisch, je Tag eine Summenzeile mit Bilanz; Trinkmengen aus dem Stammblatt
Table fluidBalance(const QList<VitalRecord> &records, const Options &o, const Patient &p = {});
Table build(Kind k, const QList<VitalRecord> &records, const Options &o, const Patient &p = {});

QString periodText(const Options &o);
QString toHtml(const Table &t, const QString &periodText);
bool toXlsx(const Table &t, const Patient &p, const QString &periodText, const QString &path, QString *error = nullptr);
bool toPdf(const Table &t, const Patient &p, const QString &periodText, const QString &path);

} // namespace Protocols
