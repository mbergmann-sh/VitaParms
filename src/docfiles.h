// VitaParms – Inhalte der Krankenakte beim Abgleich getrennt übertragen
//
// Die Datenbank geht beim Abgleich ohne Dokumentinhalte (PDF) auf die Reise; die Inhalte werden
// einzeln übertragen – nur die, die auf der Gegenseite fehlen. So bleibt jeder Abgleich klein,
// auch wenn die Krankenakte wächst.
//
//   * Kennung eines Inhalts: HMAC-SHA256(Datenbankschlüssel, "vitaparms-doc:" + uid des Dokuments),
//     hexadezimal. Sie verrät dem Server nichts über den Inhalt und ist auf allen Geräten gleich.
//   * Verschlüsselung (Server, USB): "VPF1" · 16 Byte Zufall · AES-256-CTR · 32 Byte HMAC-SHA256
//     (encrypt-then-MAC); beide Schlüssel werden aus dem Datenbankschlüssel abgeleitet.
// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace DocFiles {

QString fileId(const QByteArray &dbKey, const QString &docUid);

QByteArray seal(const QByteArray &plain, const QByteArray &dbKey);
bool open(const QByteArray &sealed, const QByteArray &dbKey, QByteArray *plain);

// Inhalte dieser Datenbank: Kennung → Dokument-ID (nur vorhandene bzw. nur fehlende)
QHash<QString, qint64> present(const QByteArray &dbKey);
QHash<QString, qint64> missing(const QByteArray &dbKey);

// Verschlüsselten Inhalt einer Kennung liefern (leer, wenn nicht vorhanden)
QByteArray sealedData(const QString &id, const QByteArray &dbKey);
// Verschlüsselten Inhalt übernehmen: prüft Kennung, MAC und (falls bekannt) die Prüfsumme.
bool storeSealed(const QString &id, const QByteArray &sealed, const QByteArray &dbKey, QString *error = nullptr);

bool isValidId(const QString &id);

} // namespace DocFiles
