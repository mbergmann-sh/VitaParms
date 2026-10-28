// VitaParms Tablet – Verwaltung (Stufe 6): Anmeldekonten, eigenes Passwort, Wiederherstellungs-
// schlüssel, Sicherung erstellen/wiederherstellen, Wiederherstellung bei vergessenem Passwort und
// die Demo-Datenbank (Teil von AppController). Wie am Computer dürfen nur Administratoren Konten
// verwalten, den Wiederherstellungsschlüssel sehen und Sicherungen erstellen.
// SPDX-License-Identifier: MIT
#include "appcontroller.h"

#include "appsettings.h"
#include "database.h"
#include "dbtransfer.h"
#include "demodata.h"
#include "keystore.h"
#include "session.h"
#include "tabletreminders.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QPageSize>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTextDocument>

namespace {
QString exportDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/exports");
    QDir().mkpath(dir);
    return dir;
}
} // namespace

// ---------------------------------------------------------------- Konten

QVariantList AppController::accountList()
{
    QVariantList l;
    if (!loggedIn()) return l;
    for (const Account &a : Session::keyStore().accounts())
        l << QVariantMap{ { QStringLiteral("name"), a.name }, { QStringLiteral("admin"), a.role == Role::Admin },
                          { QStringLiteral("roleText"), roleName(a.role) },
                          { QStringLiteral("created"), a.created.isValid() ? QLocale().toString(a.created.toLocalTime(), QStringLiteral("dd.MM.yyyy")) : QString() },
                          { QStringLiteral("self"), a.name.compare(Session::user(), Qt::CaseInsensitive) == 0 } };
    return l;
}

bool AppController::persistKeys()
{
    QString err;
    if (!Session::keyStore().save(Session::dbKey(), &err)) {
        Session::keyStore().load(Session::keyStore().path());   // Stand der Datei wiederherstellen
        setError(tr("Die Schlüsseldatei konnte nicht gespeichert werden: %1").arg(err));
        return false;
    }
    emit accountsChanged();
    return true;
}

static bool checkNewPassword(const QString &pw, const QString &repeat, QString *error)
{
    if (pw != repeat) { *error = AppController::tr("Die beiden Passwörter stimmen nicht überein."); return false; }
    if (const QString p = KeyStore::passwordProblem(pw); !p.isEmpty()) { *error = p; return false; }
    return true;
}

bool AppController::addAccount(const QString &name, const QString &password, const QString &repeat, bool admin)
{
    if (!isAdmin()) { setError(tr("Nur Administratoren können Konten verwalten.")); return false; }
    const QString n = name.trimmed();
    if (n.isEmpty()) { setError(tr("Bitte einen Namen für das Konto eingeben.")); return false; }
    QString err;
    if (!checkNewPassword(password, repeat, &err)) { setError(err); return false; }
    if (!Session::keyStore().addAccount(n, password, admin ? Role::Admin : Role::User, Session::dbKey(), &err)) {
        setError(err);
        return false;
    }
    return persistKeys();
}

bool AppController::resetAccountPassword(const QString &name, const QString &password, const QString &repeat)
{
    if (!isAdmin()) { setError(tr("Nur Administratoren können Konten verwalten.")); return false; }
    QString err;
    if (!checkNewPassword(password, repeat, &err)) { setError(err); return false; }
    if (!Session::keyStore().setPassword(name, password, Session::dbKey())) { setError(tr("Das Konto gibt es nicht.")); return false; }
    return persistKeys();
}

bool AppController::setAccountAdmin(const QString &name, bool admin)
{
    if (!isAdmin()) { setError(tr("Nur Administratoren können Konten verwalten.")); return false; }
    QString err;
    if (!Session::keyStore().setRole(name, admin ? Role::Admin : Role::User, &err)) {
        setError(err.isEmpty() ? tr("Das Konto gibt es nicht.") : err);
        return false;
    }
    return persistKeys();
}

bool AppController::removeAccount(const QString &name)
{
    if (!isAdmin()) { setError(tr("Nur Administratoren können Konten verwalten.")); return false; }
    if (name.compare(Session::user(), Qt::CaseInsensitive) == 0) { setError(tr("Das eigene Konto lässt sich nicht löschen.")); return false; }
    QString err;
    if (!Session::keyStore().removeAccount(name, &err)) { setError(err.isEmpty() ? tr("Das Konto gibt es nicht.") : err); return false; }
    return persistKeys();
}

bool AppController::changeOwnPassword(const QString &oldPassword, const QString &password, const QString &repeat)
{
    if (!loggedIn()) return false;
    QByteArray key;
    Role role;
    if (!Session::keyStore().unlock(Session::user(), oldPassword, &key, &role)) {
        setError(tr("Das bisherige Passwort stimmt nicht."));
        return false;
    }
    key.fill(0);
    QString err;
    if (!checkNewPassword(password, repeat, &err)) { setError(err); return false; }
    if (!Session::keyStore().setPassword(Session::user(), password, Session::dbKey())) { setError(tr("Das Konto gibt es nicht.")); return false; }
    return persistKeys();
}

// ---------------------------------------------------------------- Wiederherstellungsschlüssel

QString AppController::recoveryCode()
{
    if (!isAdmin()) { setError(tr("Nur Administratoren sehen den Wiederherstellungsschlüssel.")); return {}; }
    return KeyStore::recoveryCode(Session::dbKey());
}

QString AppController::recoveryCodePdf()
{
    const QString code = recoveryCode();
    if (code.isEmpty()) return {};
    const QString path = exportDir() + QStringLiteral("/VitaParms-Wiederherstellungsschluessel.pdf");
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(150);
    pdf.setTitle(tr("VitaParms – Wiederherstellungsschlüssel"));
    QTextDocument doc;
    doc.setHtml(tr("<h2>VitaParms – Wiederherstellungsschlüssel</h2>"
                   "<p>Erstellt am: %1</p>"
                   "<p style=\"font-family: monospace; font-size: 15pt; font-weight: bold;\">%2</p>"
                   "<p>Dieser Schlüssel öffnet die verschlüsselte Datenbank ohne Passwort. "
                   "Sicher und getrennt vom Gerät aufbewahren. Nicht weitergeben.</p>"
                   "<p>Verwendung: Anmeldung → „Passwort vergessen?“ (Tablet) bzw. „Wiederherstellungsschlüssel…“ (Computer).</p>")
                    .arg(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")), code));
    doc.setPageSize(QSizeF(pdf.width(), pdf.height()));
    doc.print(&pdf);
    return path;
}

bool AppController::recoverAccess(const QString &code, const QString &name, const QString &password, const QString &repeat)
{
    QString err;
    if (!checkNewPassword(password, repeat, &err)) { setError(err); return false; }
    if (!Session::recover(dbPath(), code, name, password, &err)) { setError(err); return false; }
    afterLogin();
    return true;
}

// ---------------------------------------------------------------- Sicherung

QString AppController::createBackup()
{
    if (!isAdmin()) { setError(tr("Nur Administratoren können Sicherungen erstellen.")); return {}; }
    const QString path = exportDir() + QStringLiteral("/VitaParms-Sicherung_%1.vpdb")
                                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm")));
    QString err;
    if (!DbTransfer::exportBundle(path, &err)) { setError(err); return {}; }
    return path;
}

bool AppController::shareBackup()
{
    const QString path = createBackup();
    return !path.isEmpty() && shareFiles({ path }, tr("VitaParms-Sicherung"));
}

// ---------------------------------------------------------------- Demo-Datenbank

bool AppController::isDemo() const { return m_demo; }

bool AppController::startDemo()
{
    if (m_demo) return true;
    if (loggedIn()) lock();
    m_demo = true;
    DemoData::setActive(true);
    QString err;
    qint64 pid = 0;
    if (!DemoData::create(DemoData::databasePath(), &pid, &err)) {
        Database::instance().close();
        Session::clear();
        m_demo = false;
        DemoData::setActive(false);
        setError(err.isEmpty() ? tr("Die Demo konnte nicht erstellt werden.") : err);
        emit sessionChanged();
        return false;
    }
    setLastPerson(pid);
    afterLogin();
    return true;
}

void AppController::stopDemo()
{
    if (!m_demo) return;
    lock();                         // beendet die Demo (siehe lock())
}
