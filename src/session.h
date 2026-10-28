// VitaParms – angemeldetes Konto (Sitzung) und Entsperren der Datenbank
// SPDX-License-Identifier: MIT
#pragma once

#include "keystore.h"

#include <QString>

class QWidget;

namespace Session {

QString user();
Role    role();
bool    isAdmin();
bool    isLoggedIn();
KeyStore &keyStore();          // Schlüsselbund der geöffneten Datenbank
QByteArray dbKey();            // nur für Verwaltungsfunktionen (Konten, Sicherung)

void set(const QString &user, Role role, const KeyStore &ks, const QByteArray &dbKey);
void clear();

// Öffnet 'dbPath' interaktiv: Anmeldung, Ersteinrichtung (erstes Admin-Konto,
// Verschlüsselung einer bisher unverschlüsselten Datenbank, Wiederherstellungs-
// schlüssel) bzw. Wiederherstellung. Liefert false bei Abbruch.
bool openInteractive(QWidget *parent, const QString &dbPath, bool createNew = false);

// Für Tests/Automatisierung: ohne Dialoge
bool createDatabase(const QString &dbPath, const QString &adminName, const QString &password,
                    QString *recoveryCode = nullptr, QString *error = nullptr);
bool login(const QString &dbPath, const QString &name, const QString &password, QString *error = nullptr);
// Wiederherstellung ohne Dialoge (Tablet): Datenbank mit dem Wiederherstellungsschlüssel öffnen und
// für 'name' ein Administrator-Konto mit neuem Passwort setzen bzw. anlegen (auch ohne .keys-Datei)
bool recover(const QString &dbPath, const QString &recoveryCode, const QString &name, const QString &password,
             QString *error = nullptr);

} // namespace Session
