// VitaParms – Termine: gemeinsame Fachlogik für Desktop und Tablet
//
// Anzeigetexte (Priorität, Vorwarnung, Serienregel), Sichtbarkeit nach Konto, die Auswahl
// „Wiederholung“ der Eingabemasken und das Speichern/Löschen mit Serien-Umfang
// („nur dieser“, „dieser und alle folgenden“, „alle“).
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "recurrence.h"

#include <QColor>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>

namespace ApptUi {
QString priorityName(int p);
QColor  priorityColor(int p);
QList<QPair<int, QString>> leadTimes();      // Minuten, Anzeigetext (-1 = keine)
QString leadText(int minutes);
QStringList accountNames();                  // Anmeldekonten der geöffneten Datenbank
bool canSee(const Appointment &a);           // Admin: alle, Anwender: eigene
QString seriesText(const Appointment &a);    // Beschreibung der Regel oder leer

// Bei Serien: Für welche Termine gilt das Bearbeiten/Löschen?
enum class SeriesScope { Cancel, OnlyThis, Following, All };

// Auswahl „Wiederholung“ in den Eingabemasken
enum RepeatChoice { RepNone, RepDaily, RepWorkdays, RepWeekly, RepMonthly, RepYearly, RepEveryNWeeks, RepEveryNDays };
enum EndChoice { EndNever, EndUntil, EndCount };
QList<QPair<int, QString>> repeatChoices();  // in der Reihenfolge der Auswahlliste
int repeatChoiceOf(const Recurrence &r);
// Regel aus den Eingaben bilden (an 'start' angepasst); weekdays: Bit 0 = Montag
Recurrence makeRecurrence(int choice, int interval, int weekdays, int monthMode, int endChoice,
                          const QDate &until, int count, const QDate &start);
// Monatsmodi passend zum Startdatum: (Modus, Text)
QList<QPair<int, QString>> monthModes(const QDate &start);

// Speichern unter Berücksichtigung einer Serie.
//  before: Stand vor der Bearbeitung; rule(start): Regel aus der Maske für dieses Startdatum
//  (leer = Maske ohne Wiederholung, Regel bleibt). scope wird nur bei Serien ausgewertet.
bool ruleChanged(const Appointment &before, const std::function<Recurrence(const QDate &)> &rule);
bool saveWithScope(Appointment &a, const Appointment &before, const std::function<Recurrence(const QDate &)> &rule,
                   SeriesScope scope, QString *error);
bool deleteWithScope(const Appointment &a, SeriesScope scope, QString *error);
}
