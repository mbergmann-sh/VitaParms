// VitaParms – BZ-Tagesprofil: Blutzucker zu sieben festen Zeitpunkten eines Tages messen
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

struct Point {
    QString label;       // „2 Stunden nach dem Frühstück“
    QString context;     // Messzeitpunkt im Journal, z. B. „2 h nach dem Frühstück“
    QTime   time;        // gespeicherte bzw. vorgegebene Uhrzeit
    bool    active = true;
};

QList<Point> points();                           // die sieben Zeitpunkte mit gespeicherten Uhrzeiten
void savePoints(const QList<Point> &l);          // Uhrzeiten und Auswahl merken
QString subject();                               // „BZ-Tagesprofil“
QString title(const Point &p);                   // „BZ-Tagesprofil 09:30 – Vor dem Mittagessen“

bool isProfileAppointment(const Appointment &a);
QString contextOf(const Appointment &a);         // Messzeitpunkt für die Erfassung
QString labelOf(const Appointment &a);

// Termine eines Tages anlegen; vorhandene, nicht erledigte Profil-Termine dieses Tages und dieser
// Person werden ersetzt. Liefert die Zahl der angelegten Termine (vergangene Zeitpunkte entfallen).
int create(qint64 patientId, const QDate &day, const QList<Point> &points, bool sound, QString *error);
QList<Appointment> existing(qint64 patientId, const QDate &day);
int cancel(qint64 patientId, const QDate &day);  // offene Profil-Termine des Tages löschen

} // namespace GlucoseProfile
