// VitaParms – automatische Sicherung der Datenbank
//
// Schützt vor Datenverlust durch Bedienfehler, Absturz oder defekte Festplatte, ohne dass man
// daran denken muss:
//   * beim Beenden, wenn sich seit der letzten Sicherung etwas geändert hat
//   * beim Start, wenn die letzte Sicherung älter als einen Tag ist (falls das Programm nicht
//     ordentlich beendet wurde)
//   * die letzten n Stände bleiben erhalten (Standard 7), ältere werden gelöscht
// Jede Sicherung besteht wie die manuelle aus der verschlüsselten Datenbank (*.sqlite) und der
// Schlüsseldatei der Anmeldekonten (*.sqlite.keys) und lässt sich über
// „Datei → Sicherung wiederherstellen“ einspielen. Ordner, Anzahl und Ein/Aus sind
// Geräte-Einstellungen; für Schutz vor Festplattendefekten einen USB-Stick oder Cloud-Ordner wählen.
// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace AutoBackup {

struct Config {
    bool    enabled = true;
    QString folder;           // leer = Standardordner
    int     keep = 7;         // Anzahl aufbewahrter Stände
};
Config  config();
void    saveConfig(const Config &c);
QString defaultFolder();      // <Programmdaten>/Sicherungen
QString folder();             // gewählter bzw. Standardordner

struct Entry {
    QString   path;           // *.sqlite
    QDateTime when;
    qint64    size = 0;       // Datenbank + Schlüsseldatei
};
// Automatische Sicherungen der geöffneten Datenbank, neueste zuerst
QList<Entry> list();
QDateTime    lastBackup();

bool dueAtStart();            // eingeschaltet und letzte Sicherung älter als 24 Stunden
bool dueAtExit();             // eingeschaltet und seit der letzten Sicherung geändert
// Sicherung erstellen und alte Stände entfernen. path: neue Datei
bool run(QString *path = nullptr, QString *error = nullptr);
// Hinweistext, wenn die letzte erfolgreiche Sicherung zu lange her ist (sonst leer)
QString staleWarning(int days = 7);

} // namespace AutoBackup
