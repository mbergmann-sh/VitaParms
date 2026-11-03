// VitaParms – Impfpass im Stammblatt: erhaltene Impfungen mit Datum, Impfstoff (Präparat) und
// Chargennummer, dazu Arzt/Praxis und Bemerkung. Gespeichert wie die Kontakte als JSON-Liste in
// Patient::extra["vaccinations"] – damit geht der Impfpass ohne Schemaänderung mit dem Abgleich,
// aufs Tablet und auf die Website.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace Vaccinations {

struct Vaccination {
    QString uid;          // stabile Kennung (Bearbeiten, Abgleich)
    QDate   date;         // Tag der Impfung
    QString against;      // Impfung gegen, z. B. „Tetanus, Diphtherie, Pertussis“
    QString product;      // Impfstoff / Handelsname, z. B. „Boostrix“
    QString batch;        // Chargennummer (Ch.-B.)
    QString doctor;       // Arzt / Praxis (Stempel)
    QString note;         // z. B. „2. Dosis“, „Auffrischung“

    bool isEmpty() const;
    QString summary() const;              // „12.03.2024 Tetanus … – Boostrix, Ch.-B. AB123“
};

QString key();                            // "vaccinations" (interner Zusatzfeld-Schlüssel)
QString newUid();

QList<Vaccination> list(const Patient &p);            // neueste Impfung zuerst
void store(Patient &p, QList<Vaccination> list);       // sortiert und schreibt nach p.extra
void sort(QList<Vaccination> &list);                   // nach Datum, neueste zuerst

// Vorschläge für „Impfung gegen“ (übersetzt) – ergänzt um bereits verwendete Einträge
QStringList againstSuggestions(const QList<Vaccination> &existing = {});
QStringList productSuggestions(const QList<Vaccination> &existing);   // bereits verwendete Präparate

// Zuletzt erhaltene Impfung je Erreger-Eintrag (für Übersichten): „Tetanus …: 12.03.2024“
QStringList latestPerAgainst(const QList<Vaccination> &list);

} // namespace Vaccinations
