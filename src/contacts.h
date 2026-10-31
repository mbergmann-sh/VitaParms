// VitaParms – Kontakte einer Person (Stammblatt, „Weitere Angaben“)
//
// Krankenkasse, Hausarzt, Fachärzte, Kliniken, Pflegedienst, Apotheke, Notfallkontakt …
// jeweils mit Anschrift, Telefon, Mobil, E-Mail und Website; die Krankenkasse zusätzlich mit
// Versichertennummer. Gespeichert als JSON-Liste in Patient::extra["contacts"] – damit gehen die
// Kontakte ohne Schemaänderung mit dem Abgleich und auf die Website.
//
// Ältere Versionen kannten nur die Zusatzfelder „Krankenkasse“, „Hausarztpraxis“, „Praxis-Telefon“
// und „Notfallkontakt“. list() übernimmt sie beim ersten Lesen; store() schreibt sie weiter mit,
// damit ältere Programmstände (z. B. ein noch nicht aktualisiertes Tablet) sie anzeigen.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Contacts {

// Neue Arten nur hinten anfügen – die Nummer wird gespeichert (Relative ab 0.28)
enum Kind { Insurance = 0, FamilyDoctor, Specialist, Clinic, CareService, Pharmacy, Emergency, Other, Relative, KindCount };
QList<int> kindOrder();                // Reihenfolge für Auswahllisten (Angehörige vor Notfallkontakt)

QString kindName(int kind);             // „Krankenkasse“, „Hausarztpraxis“, …

struct Contact {
    QString uid;            // stabile Kennung (Bearbeiten, Abgleich)
    int     kind = Other;
    QString name;           // Praxis, Klinik, Kasse oder Person
    QString specialty;      // Fachrichtung bzw. Abteilung (z. B. „Kardiologie“)
    QString person;         // Ansprechpartner bzw. Beziehung (z. B. „Ehemann“)
    QString insuranceNo;    // nur Krankenkasse: Versichertennummer
    QString street, zip, city;
    QString phone, mobile, email, website;
    QString note;

    bool isEmpty() const;
    QString kindName() const { return Contacts::kindName(kind); }
    QString address() const;            // „Straße, PLZ Ort“
    QString title() const;              // „Name – Fachrichtung“
    QString searchText() const;         // alles in einer Zeile (für die Suche)
    QString summary() const;            // kurze Zeile für Listen und Ausdrucke
};

QString contactsKey();                  // "contacts" (interner Zusatzfeld-Schlüssel)
bool isLegacyKey(const QString &key);   // „Krankenkasse“, „Hausarztpraxis“, „Praxis-Telefon“, „Notfallkontakt“

// Kontakte der Person, sortiert nach Art und Name; übernimmt bei Bedarf die alten Zusatzfelder
QList<Contact> list(const Patient &p);
// Kontakte speichern (in p.extra) und die alten Zusatzfelder passend mitschreiben
void store(Patient &p, QList<Contact> contacts);
void sort(QList<Contact> &contacts);

// erster Kontakt dieser Art (z. B. Krankenkasse, Hausarzt) – leer, wenn keiner da ist
Contact first(const Patient &p, int kind);
// „AOK Bayern, Vers.-Nr. A123456789“ bzw. „Dr. Sommer, 01234 98765“ – für Ausdrucke
QString insuranceLine(const Patient &p);
QString familyDoctorLine(const Patient &p);

QString newUid();

} // namespace Contacts
