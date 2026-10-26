// VitaParms – Pflegegrad und Schwerbehindertenausweis: Merkzeichen und Anzeigetexte
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QString>

namespace CareInfo {

struct Mark {
    QString code;      // wie im Ausweis: "G", "aG", "B", "H", "Bl", "Gl", "TBl", "RF", "1. Kl."
    QString meaning;   // Erläuterung
};
const QList<Mark> &marks();

QString careLevelName(int level);                 // "Pflegegrad 3" bzw. "kein Pflegegrad"
QString careLevelText(const Patient &p);          // "Pflegegrad 3, nächste Überprüfung 12.05.2027"
QString disabilityText(const Patient &p);         // "GdB 80, Merkzeichen G, B – gültig bis 31.12.2028"
// Überprüfung bzw. Ablauf in weniger als 'days' Tagen (oder schon vorbei)
bool careReviewSoon(const Patient &p, int days = 60);
bool disabilityExpiresSoon(const Patient &p, int days = 90);

// --- Vorsorgedokumente: Patientenverfügung, Vorsorgevollmacht, Betreuungsvollmacht
enum AdvanceKind { PatientDirective = 0, PowerOfAttorney, CareProxy, AdvanceCount };
struct AdvanceDoc {
    bool    present = false;
    QString person;          // Bevollmächtigte(r) bzw. Vertrauensperson
    QString address;
    QString phone;
    QDate   validUntil;      // ungültig = unbefristet
    qint64  docId = 0;       // PDF in der Krankenakte (Kategorie „Vorsorge“)
};
QString advanceKey(int kind);                     // "pv", "vv", "bv"
QString advanceName(int kind);                    // "Patientenverfügung", …
QString advancePersonLabel(int kind);             // "Vertrauensperson", "Bevollmächtigte(r)", …
QString advanceDocIdKey(int kind);                // Schlüssel in Patient::extra ("pvDocId" …), für den Abgleich
AdvanceDoc advanceDoc(const Patient &p, int kind);
void setAdvanceDoc(Patient &p, int kind, const AdvanceDoc &d);
QString advanceLine(const Patient &p, int kind);  // "Vorsorgevollmacht: Max Muster, Gartenweg 3, Tel. … – gültig bis …" ("" = keine)
QString advanceDetails(const Patient &p, int kind);   // ohne Dokumentname
QString advanceShort(const Patient &p);           // "Patientenverfügung, Vorsorgevollmacht (PDF)" für die Kopfzeile
bool advanceExpiresSoon(const Patient &p, int days = 90);

// Interne Schlüssel in Patient::extra (keine Formularfelder: Vorsorge, Stell-Rhythmus …)
bool isInternalExtraKey(const QString &key);

} // namespace CareInfo
