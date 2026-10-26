// VitaParms – minimaler SMTP-Client für Termin-Erinnerungen
// SMTP mit STARTTLS oder SSL/TLS (Port 465), Anmeldung per AUTH PLAIN/LOGIN.
// Der Versand blockiert und wird deshalb in einem Hintergrund-Thread ausgeführt
// (sendAsync).
// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace Smtp {

enum class Security { None = 0, StartTls = 1, SslTls = 2 };

struct Config {
    QString  host;
    int      port = 587;
    Security security = Security::StartTls;
    QString  user;
    QString  password;
    QString  from;
    bool     includeDetails = false;   // Termindetails in die E-Mail übernehmen
    bool isValid() const { return !host.isEmpty() && !from.isEmpty(); }
};

Config loadConfig();                     // aus der verschlüsselten Datenbank
void   saveConfig(const Config &c);

bool send(const Config &c, const QString &to, const QString &subject, const QString &body,
          QString *error = nullptr, int timeoutMs = 20000);

// Versand im Hintergrund; 'done' wird im GUI-Thread aufgerufen (ok, Fehlertext)
void sendAsync(const Config &c, const QString &to, const QString &subject, const QString &body,
               QObject *context, const std::function<void(bool, const QString &)> &done);

bool isValidAddress(const QString &address);

} // namespace Smtp
