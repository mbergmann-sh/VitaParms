// VitaParms – Datenbank von VitaParms light in VitaParms übernehmen
//
// VitaParms light verwendet dasselbe Datenbankformat. Beim Import wird die Light-Datenbank (eigener
// Schlüssel; entsperrt mit ihrem Anmeldekonto oder dem Wiederherstellungsschlüssel) Zeile für Zeile
// in die geöffnete Datenbank eingeführt – wie beim Abgleich: Die Person der Light-Version kommt mit
// Stammblatt, Messungen, Medikamenten, Terminen, Kontakten und Dokumenten als weitere Person hinzu,
// die vorhandenen Personen bleiben unverändert. Ein erneuter Import derselben Datenbank aktualisiert
// die schon übernommenen Einträge (gleiche Kennungen), statt sie zu verdoppeln.
// Vorher legt VitaParms eine Sicherung der geöffneten Datenbank an.
// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace LightImport {

// Datenordner von VitaParms light auf diesem Rechner (Vorschlag im Dateidialog)
QString lightDataDir();
// Pfad der Standard-Datenbank von VitaParms light (existiert evtl. nicht)
QString lightDatabasePath();

struct Result {
    int inserted = 0, updated = 0;
    QStringList persons;             // übernommene Personen („Name, Vorname“)
    QList<qint64> personIds;         // ihre IDs in dieser Datenbank
    QString backup;                  // Sicherung vor dem Import
};

// 'path': Datenbankdatei (*.sqlite) der Light-Version, 'key': ihr Datenbankschlüssel.
bool importFrom(const QString &path, const QByteArray &key, Result *result, QString *error);

} // namespace LightImport
