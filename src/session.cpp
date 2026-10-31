// SPDX-License-Identifier: MIT
#include "session.h"
#include "edition.h"
#include <QPushButton>
#include <QStandardPaths>
#include <QFileDialog>
#include "dbtransfer.h"
#ifndef VP_MOBILE
#include "accountdialogs.h"
#endif
#include "database.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

namespace Session {

namespace {
QString    g_user;
Role       g_role = Role::User;
KeyStore   g_ks;
QByteArray g_key;
bool       g_loggedIn = false;

QString tr(const char *s) { return QCoreApplication::translate("Session", s); }

[[maybe_unused]] QString defaultAdminName()
{
    QString n = qEnvironmentVariable("USER");
    if (n.isEmpty()) n = qEnvironmentVariable("USERNAME");
    return n.isEmpty() ? QStringLiteral("admin") : n;
}

[[maybe_unused]] void rememberUser(const QString &name)
{
    QSettings().setValue(QStringLiteral("login/lastUser"), name);
}

#ifndef VP_MOBILE
// Erstes Administrator-Konto anlegen, Schlüsseldatei schreiben, Code anzeigen
bool setupKeys(QWidget *parent, const QString &dbPath, const QByteArray &key, const QString &intro)
{
    AccountEditDialog d(AccountEditDialog::Mode::FirstAdmin, intro, defaultAdminName(), parent);
    if (d.exec() != QDialog::Accepted) return false;
    KeyStore ks;
    ks.setPath(KeyStore::pathFor(dbPath));
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = ks.addAccount(d.name(), d.password(), Role::Admin, key, &err) && ks.save(key, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(parent, tr("Anmeldekonto"), tr("Einrichtung fehlgeschlagen:\n%1").arg(err));
        return false;
    }
    set(d.name(), Role::Admin, ks, key);
    rememberUser(d.name());
    RecoveryCodeDialog rc(KeyStore::recoveryCode(key), dbPath, true, parent);
    rc.exec();
    return true;
}
#endif
} // namespace

QString user() { return g_user; }
Role role() { return g_role; }
bool isAdmin() { return g_loggedIn && g_role == Role::Admin; }
bool isLoggedIn() { return g_loggedIn; }
KeyStore &keyStore() { return g_ks; }
QByteArray dbKey() { return g_key; }

void set(const QString &user, Role role, const KeyStore &ks, const QByteArray &dbKey)
{
    g_user = user;
    g_role = role;
    g_ks = ks;
    g_key = dbKey;
    g_loggedIn = true;
}

void clear()
{
    g_user.clear();
    g_role = Role::User;
    g_ks = KeyStore();
    g_key.fill(0);
    g_key.clear();
    g_loggedIn = false;
}

bool createDatabase(const QString &dbPath, const QString &adminName, const QString &password,
                    QString *recoveryCode, QString *error)
{
    const QByteArray key = KeyStore::newDatabaseKey();
    if (Database::instance().open(dbPath, key, error) != Database::OpenResult::Ok)
        return false;
    KeyStore ks;
    ks.setPath(KeyStore::pathFor(dbPath));
    if (!ks.addAccount(adminName, password, Role::Admin, key, error) || !ks.save(key, error))
        return false;
    set(adminName, Role::Admin, ks, key);
    if (recoveryCode) *recoveryCode = KeyStore::recoveryCode(key);
    return true;
}

bool login(const QString &dbPath, const QString &name, const QString &password, QString *error)
{
    KeyStore ks;
    if (!ks.load(KeyStore::pathFor(dbPath), error)) return false;
    QByteArray key;
    Role role;
    if (!ks.unlock(name, password, &key, &role)) {
        if (error) *error = tr("Konto oder Passwort ist falsch.");
        return false;
    }
    if (Database::instance().open(dbPath, key, error) != Database::OpenResult::Ok)
        return false;
    if (!ks.verify(key)) role = Role::User;
    set(ks.find(name)->name, role, ks, key);
    return true;
}

bool recover(const QString &dbPath, const QString &recoveryCode, const QString &name, const QString &password,
             QString *error)
{
    const QByteArray key = KeyStore::keyFromRecoveryCode(recoveryCode);
    if (key.isEmpty()) {
        if (error) *error = tr("Der Wiederherstellungsschlüssel ist unvollständig oder enthält einen Tippfehler.");
        return false;
    }
    if (const QString p = KeyStore::passwordProblem(password); !p.isEmpty()) { if (error) *error = p; return false; }
    if (name.trimmed().isEmpty()) { if (error) *error = tr("Bitte einen Kontonamen angeben."); return false; }
    Database &db = Database::instance();
    db.close();
    QString err;
    if (db.open(dbPath, key, &err) != Database::OpenResult::Ok) {
        if (error) *error = tr("Der Wiederherstellungsschlüssel passt nicht zu dieser Datenbank.");
        return false;
    }
    KeyStore ks;
    const QString keysPath = KeyStore::pathFor(dbPath);
    if (!QFile::exists(keysPath) || !ks.load(keysPath)) {
        ks = KeyStore();
        ks.setPath(keysPath);            // Schlüsseldatei fehlt oder ist beschädigt: neu anlegen
    }
    bool ok;
    if (const Account *a = ks.find(name.trimmed())) {
        const QString n = a->name;
        ok = ks.setPassword(n, password, key) && ks.setRole(n, Role::Admin, &err);
    } else {
        ok = ks.addAccount(name.trimmed(), password, Role::Admin, key, &err);
    }
    if (ok) ok = ks.save(key, &err);
    if (!ok) {
        db.close();
        if (error) *error = err;
        return false;
    }
    const Account *a = ks.find(name.trimmed());
    set(a ? a->name : name.trimmed(), Role::Admin, ks, key);
    rememberUser(user());
    return true;
}

#ifndef VP_MOBILE
// Datenbank-Datei wählen und an die Stelle der bisherigen setzen (die bisherige bleibt als Kopie)
static bool importInteractive(QWidget *parent, const QString &dbPath)
{
    const QString src = QFileDialog::getOpenFileName(parent, tr("Datenbank importieren"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("VitaParms-Datenbank (*.vpdb *.sqlite *.db);;Alle Dateien (*)"));
    if (src.isEmpty()) return false;
    QString keys;
    if (!DbTransfer::isBundle(src) && !QFile::exists(KeyStore::pathFor(src))) {
        if (QMessageBox::question(parent, tr("Datenbank importieren"),
                tr("Neben der Datenbank liegt keine Schlüsseldatei (.keys) mit den Anmeldekonten.\n\n"
                   "Schlüsseldatei jetzt auswählen? (Ohne sie lässt sich die Datenbank nur mit dem "
                   "Wiederherstellungsschlüssel öffnen.)")) == QMessageBox::Yes)
            keys = QFileDialog::getOpenFileName(parent, tr("Schlüsseldatei wählen"), QFileInfo(src).absolutePath(),
                                                tr("Schlüsseldatei (*.keys);;Alle Dateien (*)"));
    }
    if (Database::fileHasContent(dbPath)
        && QMessageBox::warning(parent, tr("Datenbank importieren"),
               tr("Die Datenbank\n%1\nwird durch die gewählte ersetzt. Der bisherige Stand bleibt als Kopie "
                  "(*.vor-import-…) im selben Ordner erhalten.\n\nAnschließend melden Sie sich mit einem Konto "
                  "der importierten Datenbank an. Fortfahren?").arg(QDir::toNativeSeparators(dbPath)),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return false;
    QString safety, err;
    bool hasKeys = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = DbTransfer::importDatabase(src, keys, dbPath, &safety, &hasKeys, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(parent, tr("Datenbank importieren"), err);
        return false;
    }
    QMessageBox::information(parent, tr("Datenbank importieren"),
        tr("Die Datenbank wurde übernommen.%1")
            .arg(safety.isEmpty() ? QString() : tr("\n\nDer bisherige Stand liegt als Kopie unter\n%1").arg(QDir::toNativeSeparators(safety))));
    return true;
}

bool openInteractive(QWidget *parent, const QString &dbPath, bool createNew)
{
    Database &db = Database::instance();
    const QString keysPath = KeyStore::pathFor(dbPath);
    QString err;

    // ---- 1. Neue Datenbank – beim ersten Start wahlweise eine vorhandene importieren
    if (!createNew && !Database::fileHasContent(dbPath)) {
        FirstStartBox box(parent);
        box.exec();
        if (box.clickedButton() == box.importButton()) {
            importInteractive(parent, dbPath);
            return openInteractive(parent, dbPath);
        }
        if (box.clickedButton() != box.freshButton()) return false;
    }
    if (createNew || !Database::fileHasContent(dbPath)) {
        if (QFile::exists(keysPath))   // verwaiste Schlüsseldatei beiseitelegen
            QFile::rename(keysPath, keysPath + QStringLiteral(".alt-")
                                        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        const QByteArray key = KeyStore::newDatabaseKey();
        if (db.open(dbPath, key, &err) != Database::OpenResult::Ok) {
            QMessageBox::critical(parent, tr("Datenbank"), tr("Die Datenbank kann nicht angelegt werden:\n%1").arg(err));
            return false;
        }
        if (!setupKeys(parent, dbPath, key,
                Edition::light()
                    ? tr("<p><b>Neue, verschlüsselte Datenbank</b></p>"
                         "<p>Ihre Daten werden verschlüsselt gespeichert. Wählen Sie einen Namen und ein Passwort – "
                         "damit melden Sie sich bei jedem Start an.</p>")
                    : tr("<p><b>Neue, verschlüsselte Datenbank</b></p>"
                   "<p>Legen Sie das erste <b>Administrator-Konto</b> an. Weitere Konten "
                   "(Administratoren oder Anwender) können Sie danach unter <i>Extras → "
                   "Anmeldekonten</i> hinzufügen.</p>"))) {
            db.close();
            QFile::remove(dbPath);
            return false;
        }
        return true;
    }

    // ---- 2. Bisher unverschlüsselte Datenbank (ältere VitaParms-Version)
    if (!Database::isEncryptedFile(dbPath)) {
        if (db.open(dbPath, {}, &err) != Database::OpenResult::OpenedPlain) {
            QMessageBox::critical(parent, tr("Datenbank"), tr("Die Datenbank kann nicht geöffnet werden:\n%1").arg(err));
            return false;
        }
        if (QMessageBox::information(parent, tr("Datenbank verschlüsseln"),
                tr("<p>Diese Datenbank ist noch <b>nicht verschlüsselt</b>.</p>"
                   "<p>VitaParms verschlüsselt sie jetzt (ChaCha20-Poly1305, 256 Bit). Danach ist der "
                   "Zugriff nur noch mit einem Anmeldekonto oder dem Wiederherstellungsschlüssel "
                   "möglich.</p><p>Tipp: Unverschlüsselte Sicherungskopien aus früheren Versionen "
                   "sollten Sie anschließend löschen.</p>"),
                QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) {
            db.close();
            return false;
        }
        const QByteArray key = KeyStore::newDatabaseKey();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = db.rekey(key, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::critical(parent, tr("Datenbank verschlüsseln"), tr("Verschlüsselung fehlgeschlagen:\n%1").arg(err));
            db.close();
            return false;
        }
        QFile::remove(dbPath + QStringLiteral("-wal"));
        QFile::remove(dbPath + QStringLiteral("-shm"));
        if (!setupKeys(parent, dbPath, key,
                tr("<p>Die Datenbank ist jetzt verschlüsselt. Legen Sie das erste "
                   "<b>Administrator-Konto</b> an.</p>"))) {
            // Ohne Konto bliebe nur der Schlüssel – zur Sicherheit wieder entschlüsseln
            db.rekey({}, &err);
            db.close();
            return false;
        }
        return true;
    }

    // ---- 3. Verschlüsselt, aber Schlüsseldatei fehlt
    KeyStore ks;
    if (!QFile::exists(keysPath)) {
        const QByteArray key = askRecoveryKey(parent,
            tr("Zur Datenbank\n%1\nfehlt die Schlüsseldatei (%2).\nDie Datenbank kann nur mit dem "
               "Wiederherstellungsschlüssel geöffnet werden.")
                .arg(QDir::toNativeSeparators(dbPath), QFileInfo(keysPath).fileName()));
        if (key.isEmpty()) return false;
        if (db.open(dbPath, key, &err) != Database::OpenResult::Ok) {
            QMessageBox::critical(parent, tr("Wiederherstellung"),
                                  tr("Der Wiederherstellungsschlüssel passt nicht zu dieser Datenbank."));
            return false;
        }
        return setupKeys(parent, dbPath, key, tr("<p>Datenbank entsperrt. Legen Sie ein neues "
                                                 "<b>Administrator-Konto</b> an.</p>"));
    }
    if (!ks.load(keysPath, &err)) {
        QMessageBox::critical(parent, tr("Anmeldung"), err);
        return false;
    }

    // ---- 4. Normale Anmeldung
    const QString preset = QSettings().value(QStringLiteral("login/lastUser")).toString();
    for (;;) {
        LoginDialog login(dbPath, ks, preset, parent);
        if (login.exec() == QDialog::Accepted) {
            if (db.open(dbPath, login.dbKey(), &err) != Database::OpenResult::Ok) {
                QMessageBox::critical(parent, tr("Anmeldung"),
                    tr("Die Anmeldung war korrekt, aber die Datenbank passt nicht zur Schlüsseldatei:\n%1").arg(err));
                return false;
            }
            Role role = login.role();
            if (!ks.verify(login.dbKey())) {
                QMessageBox::warning(parent, tr("Anmeldung"),
                    tr("Die Kontenliste in der Schlüsseldatei wurde außerhalb von VitaParms verändert.\n"
                       "Aus Sicherheitsgründen werden Sie nur mit Anwenderrechten angemeldet. Ein "
                       "Administrator sollte die Konten prüfen."));
                role = Role::User;
            }
            set(login.user(), role, ks, login.dbKey());
            rememberUser(login.user());
            return true;
        }
        if (login.importRequested()) {
            if (importInteractive(parent, dbPath)) return openInteractive(parent, dbPath);
            continue;
        }
        if (!login.recoveryRequested()) return false;

        // Wiederherstellung: entsperren und Passwort eines Admin-Kontos neu setzen/anlegen
        const QByteArray key = askRecoveryKey(parent, tr("Alle Passwörter vergessen?"));
        if (key.isEmpty()) continue;
        if (db.open(dbPath, key, &err) != Database::OpenResult::Ok) {
            QMessageBox::warning(parent, tr("Wiederherstellung"),
                                 tr("Der Wiederherstellungsschlüssel passt nicht zu dieser Datenbank."));
            continue;
        }
        AccountEditDialog d(AccountEditDialog::Mode::FirstAdmin,
            tr("<p>Datenbank entsperrt. Vergeben Sie ein neues Passwort für ein bestehendes "
               "Administrator-Konto oder legen Sie ein neues an.</p>"), preset, parent);
        if (d.exec() != QDialog::Accepted) { db.close(); continue; }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        if (const Account *a = ks.find(d.name())) {
            const QString name = a->name;
            ks.setPassword(name, d.password(), key);
            ks.setRole(name, Role::Admin);
        } else {
            ks.addAccount(d.name(), d.password(), Role::Admin, key, &err);
        }
        const bool saved = ks.save(key, &err);
        QApplication::restoreOverrideCursor();
        if (!saved) {
            QMessageBox::critical(parent, tr("Wiederherstellung"), err);
            db.close();
            return false;
        }
        const Account *a = ks.find(d.name());
        set(a ? a->name : d.name(), Role::Admin, ks, key);
        rememberUser(user());
        return true;
    }
}
#else
bool openInteractive(QWidget *, const QString &, bool) { return false; }   // Tablet: eigene Oberfläche
#endif

} // namespace Session
