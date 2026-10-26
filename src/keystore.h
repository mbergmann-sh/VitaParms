// VitaParms – Anmeldekonten und Schlüsselbund einer Datenbank
//
// Jede Datenbank wird mit einem zufälligen 256-Bit-Datenbankschlüssel (K)
// verschlüsselt. Die Datei "<datenbank>.keys" neben der Datenbank enthält je
// Anmeldekonto eine Kopie von K, verschlüsselt mit einem aus dem Kontopasswort
// abgeleiteten Schlüssel ("Key Wrapping"):
//
//   KEK = PBKDF2-HMAC-SHA256(Passwort, Salz, 200 000 Runden, 64 Byte)
//   wrapped = AES-256-CTR(KEK[0..31], Nonce, K),  MAC = HMAC-SHA256(KEK[32..63], Nonce|wrapped)
//
// Wer sich mit Name + Passwort anmeldet, erhält so K, ohne ihn je zu sehen.
// Der Wiederherstellungsschlüssel ist K selbst in lesbarer Form (Crockford-
// Base32 mit Prüfsumme). Er funktioniert daher auch, wenn die .keys-Datei
// verloren ging oder alle Administratoren ihr Passwort vergessen haben.
// Die Kontenliste (Namen, Rollen) ist mit einem aus K abgeleiteten HMAC gegen
// Manipulation geschützt.
// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

enum class Role { Admin, User };

QString roleName(Role r);

struct Account {
    QString    name;
    Role       role = Role::User;
    QDateTime  created;
    QByteArray salt, nonce, wrapped, mac;
    int        iterations = 0;
};

class KeyStore
{
public:
    static QString pathFor(const QString &dbPath) { return dbPath + QStringLiteral(".keys"); }
    static QByteArray newDatabaseKey();            // 32 Zufallsbytes, als Hex (64 Zeichen)

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QByteArray &dbKey, QString *error = nullptr) const;   // schreibt mit Integritäts-HMAC
    bool exists() const { return m_loaded; }
    QString path() const { return m_path; }
    void setPath(const QString &p) { m_path = p; }

    const QList<Account> &accounts() const { return m_accounts; }
    int adminCount() const;
    const Account *find(const QString &name) const;

    // Anmeldung: liefert K und die Rolle, wenn Name und Passwort stimmen
    bool unlock(const QString &name, const QString &password, QByteArray *dbKey, Role *role) const;

    // Integrität der Kontenliste prüfen (nach dem Entsperren)
    bool verify(const QByteArray &dbKey) const;

    // Verwaltung (benötigt K, also eine gültige Anmeldung)
    bool addAccount(const QString &name, const QString &password, Role role, const QByteArray &dbKey, QString *error = nullptr);
    bool setPassword(const QString &name, const QString &password, const QByteArray &dbKey);
    bool setRole(const QString &name, Role role, QString *error = nullptr);
    bool removeAccount(const QString &name, QString *error = nullptr);

    // Wiederherstellungsschlüssel <-> Datenbankschlüssel
    static QString    recoveryCode(const QByteArray &dbKey);
    static QByteArray keyFromRecoveryCode(const QString &code);   // leer bei Tippfehler

    static QString passwordProblem(const QString &pw);    // leer = in Ordnung
    static constexpr int kIterations = 200000;

private:
    QString        m_path;
    bool           m_loaded = false;
    QList<Account> m_accounts;
    QByteArray     m_fileMac;
};
