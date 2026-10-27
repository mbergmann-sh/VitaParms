// VitaParms – Stuhlgang: Bristol-Stuhlformen-Skala, Häufigkeit, Hinweis nach Tagen ohne Stuhlgang
// SPDX-License-Identifier: MIT
//
// Gespeichert wird je Messung (vitals.stool): leer = nicht erfasst, 0 = Stuhlgang ohne
// nähere Angabe, 1–7 = Typ nach der Bristol-Stuhlformen-Skala (Lewis & Heaton 1997).
// Kein Medizinprodukt: Die Texte beschreiben nur die Skala; der Hinweis nach drei Tagen
// ohne Stuhlgang ersetzt keine ärztliche Beurteilung.
#pragma once

#include "datatypes.h"

#include <QColor>
#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

namespace Stool {

constexpr int kTypes = 7;
constexpr int kWarnDays = 3;        // ab so vielen Tagen ohne Stuhlgang ein Hinweis
constexpr int kTrackedDays = 30;    // Hinweis nur, wenn in diesem Zeitraum Stuhlgang erfasst wurde

enum class Group { Unspecified, Hard, Normal, Soft };

QString typeName(int type);         // "Typ 4" (0: "ohne Angabe")
QString description(int type);      // "wurstartig, glatt und weich"
QString choiceText(int type);       // "Typ 4 – wurstartig, glatt und weich"
QString shortText(int type);        // Journal: "Typ 4" bzw. "ja"
Group   group(int type);
QString groupName(Group g);         // "hart (Hinweis auf Verstopfung)" …
QColor  groupColor(Group g, bool dark = false);
QString scaleNote();                // Kurzbeschreibung der Skala (Hilfe, Tooltip)

// Vorschläge für das Stuhlprotokoll (frei ergänzbar)
QStringList amountChoices();        // wenig, mittel, viel
QStringList colorChoices();         // braun, hellbraun … schwarz (Teerstuhl), rot/blutig
QStringList admixtureChoices();     // Schleim, Blut, unverdaute Reste, übelriechend …
QString     symptomsHint();         // Platzhalter: Bauchschmerzen, Blähungen …
// Schwarzer oder blutiger Stuhl: ärztlich abklären lassen
bool        needsAttention(const StoolDetail &d);
QString     attentionText();

// Stuhlgänge je Kalendertag (nur Tage mit mindestens einem Eintrag)
struct Day { int count = 0; QList<int> types; };
QMap<QDate, Day> perDay(const QList<VitalRecord> &records, const QDate &from = {}, const QDate &to = {});

// Zeitspannen ohne Stuhlgang von mindestens minDays Tagen zwischen first und last
// (jeweils einschließlich); Rückgabe: erster und letzter Tag ohne Stuhlgang
struct Gap { QDate first; QDate last; int days() const { return int(first.daysTo(last)) + 1; } };
QList<Gap> gaps(const QMap<QDate, Day> &days, const QDate &from, const QDate &to, int minDays = kWarnDays);

// Stand für den Hinweis
struct Status {
    bool  tracked = false;          // in den letzten kTrackedDays Tagen Stuhlgang erfasst
    QDate last;                     // letzter Tag mit Stuhlgang
    int   daysWithout = 0;          // ganze Tage seit dem letzten Stuhlgang (heute zählt mit)
    bool  warn = false;             // daysWithout >= kWarnDays
};
Status status(const QList<VitalRecord> &records, const QDate &today = QDate::currentDate());
QString warningText(const Status &s);     // leer, wenn kein Hinweis
QString statusText(const Status &s);      // "letzter Stuhlgang heute / vor 2 Tagen …"

} // namespace Stool
