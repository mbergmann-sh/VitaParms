// VitaParms – Erinnerung an den Quartalswechsel (Versichertenkarte in den Praxen einlesen lassen)
//
// Zwei Tage vor bis zwei Tage nach dem Beginn eines Quartals (01.01., 01.04., 01.07., 01.10.)
// erinnert VitaParms, bis der Benutzer sie bestätigt („Erledigt“). „Morgen wieder erinnern“
// schiebt sie auf den nächsten Tag. Der Stand gehört zur Datenbank (Einstellungstabelle).
// SPDX-License-Identifier: MIT
#pragma once

#include <QDate>
#include <QString>

namespace QuarterReminder {

QDate quarterStart(const QDate &d);              // erster Tag des Quartals von d
// Quartalsbeginn, wenn 'today' im Erinnerungszeitraum liegt (Beginn −2 … +2 Tage), sonst ungültig
QDate relevantChange(const QDate &today);
QString key(const QDate &quarterBegin);          // "2027-Q1"
QString title(const QDate &quarterBegin);        // „Quartalswechsel am 01.01.2027 (1. Quartal 2027)“
QString message(const QDate &quarterBegin, const QDate &today);

bool enabled();                                   // Einstellung (Standard: an)
void setEnabled(bool on);
bool confirmed(const QDate &quarterBegin);
void confirm(const QDate &quarterBegin);
void snoozeUntil(const QDate &day);              // erst an diesem Tag wieder zeigen
// Jetzt anzeigen? (eingeschaltet, im Zeitraum, nicht bestätigt, nicht verschoben)
bool due(const QDate &today, QDate *quarterBegin = nullptr);

} // namespace QuarterReminder
