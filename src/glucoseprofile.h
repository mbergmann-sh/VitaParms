// VitaParms – Tagesprofile: Blutzucker (BZ, sieben Zeitpunkte) bzw. Blutdruck (RR, sechs
// Zeitpunkte) zu festen Zeiten eines Tages messen
//
// Für den gewählten Tag legt VitaParms je Zeitpunkt einen Termin mit Erinnerung an
// („BZ-Tagesprofil 09:30 – 2 Stunden nach dem Frühstück“). Zur Zeit erinnert VitaParms wie bei
// jedem Termin; „Messung erfassen…“ öffnet das Erfassungsfenster mit dem passenden
// Messzeitpunkt. Weil es normale Termine sind, gehen sie mit dem Abgleich auch aufs Tablet.
// Die Uhrzeiten sind einstellbar und werden gespeichert.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QTime>

namespace GlucoseProfile {

// Art des Tagesprofils (BZ ist der Standard – so bleiben die bisherigen Aufrufe gültig)
enum class Kind { Glucose, BloodPressure };

struct Point {
    QString label;       // „2 Stunden nach dem Frühstück“
    QString context;     // Messzeitpunkt im Journal, z. B. „2 h nach dem Frühstück“
    QTime   time;        // gespeicherte bzw. vorgegebene Uhrzeit
    bool    active = true;
};

QList<Point> points(Kind k = Kind::Glucose);     // Zeitpunkte mit gespeicherten Uhrzeiten
QList<Point> defaultPoints(Kind k = Kind::Glucose);   // Vorgaben (Standardzeiten)
void savePoints(const QList<Point> &l, Kind k = Kind::Glucose);   // Uhrzeiten und Auswahl merken
QString subject(Kind k = Kind::Glucose);         // „BZ-Tagesprofil“ bzw. „RR-Tagesprofil“
QString title(const Point &p, Kind k = Kind::Glucose);   // „BZ-Tagesprofil 09:30 – Vor dem Mittagessen“
QString settingsKey(Kind k);                     // "bzprofile" bzw. "rrprofile"

bool isProfileAppointment(const Appointment &a);   // BZ- oder RR-Tagesprofil
Kind kindOf(const Appointment &a);
QString contextOf(const Appointment &a);         // Messzeitpunkt für die Erfassung
QString labelOf(const Appointment &a);
QString subjectOf(const Appointment &a);         // „RR-Tagesprofil 13:00“
QString focusField(Kind k);                      // erstes Eingabefeld: "glucoseSpin" bzw. "systolicSpin"

// Termine eines Tages anlegen; vorhandene, nicht erledigte Profil-Termine derselben Art, dieses Tages und
// dieser Person werden ersetzt. Liefert die Zahl der angelegten Termine (vergangene Zeitpunkte entfallen).
int create(qint64 patientId, const QDate &day, const QList<Point> &points, bool sound, QString *error,
           Kind k = Kind::Glucose);
QList<Appointment> existing(qint64 patientId, const QDate &day, Kind k = Kind::Glucose);
int cancel(qint64 patientId, const QDate &day, Kind k = Kind::Glucose);   // offene Profil-Termine des Tages löschen

} // namespace GlucoseProfile
