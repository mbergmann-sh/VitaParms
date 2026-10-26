// VitaParms – Datenbank lokal übertragen (ohne Website)
//
//  * Export: eine einzige Datei (*.vpdb) mit der verschlüsselten Datenbank und der
//    Schlüsseldatei (Anmeldekonten) – bequem per USB-Kabel oder Cloud-Speicher auf ein
//    anderes Gerät zu bringen. Der Inhalt bleibt verschlüsselt; ohne Anmeldekonto (Name und
//    Passwort) oder Wiederherstellungsschlüssel lässt er sich nicht öffnen.
//  * Import: *.vpdb oder eine Sicherung (*.sqlite, die .keys-Datei daneben bzw. separat
//    gewählt). Die bisherige Datenbank des Geräts wird vorher als Kopie aufbewahrt.
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace DbTransfer {

QString bundleSuffix();                                      // "vpdb"
bool isBundle(const QString &path);
// Dateinamen bzw. Android-content://-Adressen lesen
bool readFile(const QString &pathOrUrl, QByteArray *data, QString *error = nullptr);

// Laufende Datenbank (Database::instance()) mit ihrer Schlüsseldatei als *.vpdb speichern
bool exportBundle(const QString &target, QString *error = nullptr);

// Importieren. keysSource: leer = neben der Datenbank suchen (bei *.vpdb enthalten).
// safetyCopy erhält den Pfad der aufbewahrten bisherigen Datenbank (leer, wenn es keine gab).
// hasKeys: false = es gibt keine Schlüsseldatei, Anmeldung nur mit Wiederherstellungsschlüssel.
bool importDatabase(const QString &source, const QString &keysSource, const QString &target,
                    QString *safetyCopy, bool *hasKeys, QString *error = nullptr);

} // namespace DbTransfer
