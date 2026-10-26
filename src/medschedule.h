// VitaParms – Einnahme-Rhythmus von Medikamenten (täglich, alle N Tage, an Wochentagen,
// monatlich, nach Bedarf) und Erinnerung an die Einnahme über die Terminliste
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

namespace MedSchedule {

enum Choice { Daily = 0, EveryNDays, Weekdays, Monthly, AsNeeded };
Choice choiceOf(const Medication &m);
QString categoryName(int category);                      // "Regelmedikation", …
QString categoryHint(int category);                      // Erläuterung
// Medikamente nach Art gruppiert (Regel, Bedarf, Notfall), Reihenfolge innerhalb erhalten
QList<QPair<int, QList<Medication>>> grouped(const QList<Medication> &meds);

bool isDaily(const Medication &m);                       // täglich (Standard)
bool isInterval(const Medication &m);                    // fester Rhythmus, nicht täglich
bool isDueOn(const Medication &m, const QDate &day);     // ohne Beginn/Ende-Prüfung
QDate nextDue(const Medication &m, const QDate &from, int maxDays = 400);
QDate anchor(const Medication &m);                       // Bezugsdatum des Rhythmus
QString describe(const Medication &m);                   // "täglich", "alle 3 Tage", "Mo, Do", …
QString fullText(const Medication &m);                   // Rhythmus + Bemerkung (für Pläne)

// Datenbank-Spalte "rhythm" (JSON)
QString toJsonString(const Medication &m);
void fromJsonString(Medication &m, const QString &json);

// --- Absetzen ------------------------------------------------------------------------
constexpr const char *kStopReasonKey = "stopReason";     // Grund des Absetzens
constexpr const char *kStopOnKey = "stopOn";             // Absetzdatum (ab diesem Tag nicht mehr gegeben)
QString stopReason(const Medication &m);
QDate stopDate(const Medication &m);
bool stopPlanned(const Medication &m);                   // Absetzen für ein künftiges Datum vorgemerkt
// Absetzen zum Datum 'day': heute oder früher → sofort in die Historie; in der Zukunft →
// bis zum Vortag weiter im Plan (Enddatum = Vortag), danach automatisch in die Historie.
void discontinue(Medication &m, const QDate &day, const QString &reason);
void cancelPlannedStop(Medication &m);                   // vorgemerktes Absetzen zurücknehmen
void reactivate(Medication &m);
QString statusText(const Medication &m);                 // "aktiv", "bis 31.10.2026", "abgesetzt am …"

// --- Terminliste / Erinnerung an die Einnahme -----------------------------------------
constexpr const char *kSeriesKey = "apptSeriesId";       // ältere Versionen: eine Serie
constexpr const char *kSeriesListKey = "apptSeriesIds";  // je Einnahmezeit eine Serie
constexpr const char *kSigKey = "apptSig";
constexpr const char *kToCalendarKey = "toCalendar";     // ältere Versionen: false = nicht in die Terminliste
constexpr const char *kRemindKey = "remind";             // An Einnahme erinnern (ja/nein)
constexpr const char *kRemindDaysKey = "remindDays";     // Wochentage (Bit 0 = Montag), nur bei täglicher Einnahme
constexpr const char *kRemindTimesKey = "remindTimes";   // Einnahmezeiten ["08:00", …]

enum Slot { Morning = 0, Noon, Evening, Night, SlotCount };
QString slotName(int slot);                              // "morgens", …
QTime slotDefaultTime(int slot);                         // 08:00, 12:00, 18:00, 21:00
int slotOf(const QTime &t);                              // Tageszeit einer Uhrzeit
QString slotDose(const Medication &m, int slot);         // Dosis dieser Tageszeit ("" = keine)

struct Reminder {
    bool on = false;
    int days = 0x7f;                                     // nur bei täglicher Einnahme
    QList<QTime> times;                                  // aufsteigend
};
bool canRemind(const Medication &m);                     // Regelmedikation und Sonstiges
bool remindByWeekdays(const Medication &m);              // Tage frei wählbar (sonst nach Rhythmus)
QList<QTime> defaultTimes(const Medication &m);          // aus dem Dosierschema
Reminder reminder(const Medication &m);
void setReminder(Medication &m, const Reminder &r);
// Uhrzeiten der Erinnerung den vier Tageszeiten zugeordnet (belegte Tageszeit → nächste freie);
// ohne gespeicherte Auswahl nach dem Dosierschema
struct SlotTime { bool on = false; QTime time; };
QList<SlotTime> reminderSlots(const Medication &m);
QString reminderText(const Medication &m);               // "Mo–Fr um 08:00 und 18:00 Uhr" ("" = keine)
QList<qint64> seriesIds(const Medication &m);

bool wantsAppointments(const Medication &m);
QTime intakeTime(const Medication &m);                   // erster Einnahmezeitpunkt (08/12/18/21 Uhr)

// Gleicht die Terminserien der Medikamente einer Person ab (anlegen, ändern, beenden):
// je Einnahmezeit eine Serie. Vergangene Termine bleiben erhalten. Aktualisiert die
// Verknüpfung in m.extra und speichert sie. Liefert die Zahl geänderter Serien.
int syncAppointments(qint64 patientId, QList<Medication> &meds, const QList<Medication> &removed = {});

} // namespace MedSchedule
