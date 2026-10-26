// VitaParms – Abgleich der Datenbank zwischen Geräten (Desktop, Tablet) über die eigene Website
//
// Der Server (website/api/sync.php) verwahrt nur die verschlüsselte Datenbank und die
// verschlüsselte Schlüsseldatei der Anmeldekonten; er zählt eine Revision hoch und lehnt
// veraltete Uploads ab (HTTP 409). Zusammengeführt wird auf dem Gerät:
//
//   * jede abgeglichene Zeile trägt eine geräteübergreifende Kennung (uid) und den Zeitpunkt
//     ihrer letzten Änderung (sync_at, UTC) – gepflegt von Triggern (Schema 7)
//   * gelöschte Zeilen hinterlassen einen Löschvermerk (sync_deleted)
//   * pro Zeile gewinnt die jüngere Änderung; ein jüngerer Löschvermerk gewinnt gegen ältere
//     Änderungen; neue Zeilen beider Seiten bleiben erhalten
//   * Terminvorkommen einer Serie haben feste Kennungen (Serie@Zeitpunkt), damit beide
//     Geräte dieselben Vorkommen erzeugen, ohne dass Doppelungen entstehen
//
// Gerätebezogen (nicht abgeglichen): Einstellungen, "erinnert"/"später erinnern" bei Terminen.
// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace DbMerge {
struct Stats {
    int inserted = 0, updated = 0, deleted = 0;
    int total() const { return inserted + updated + deleted; }
};
// Führt die Datenbank 'remotePath' (gleicher Schlüssel) in die geöffnete Datenbank ein.
bool mergeFrom(const QString &remotePath, const QByteArray &key, Stats *stats, QString *error);
}

class DbSync : public QObject
{
    Q_OBJECT
public:
    static DbSync &instance();

    struct Config {
        bool    enabled = false;
        bool    autoSync = true;      // nach Änderungen (verzögert) und beim Beenden
        QString url;                  // wie die Website (WebSync)
        QString uploadKey;
    };
    static Config config();
    static void   saveConfig(const Config &c);
    static bool   isReady(const Config &c, QString *why = nullptr);

    struct Status {
        qint64    revision = 0;       // zuletzt abgeglichene Server-Revision
        QDateTime last;
        QString   lastResult;
        bool      pending = false;    // lokale Änderungen seit dem letzten Abgleich
    };
    Status status();

    // Abgleich (asynchron, Ergebnis über finished())
    void sync();
    bool syncBlocking(int timeoutMs, QString *message);
    bool isBusy() const { return m_busy; }

    // Ersteinrichtung eines weiteren Geräts: Datenbank und Schlüsseldatei vom Server laden
    // (vor der Anmeldung; überschreibt 'dbPath' und 'dbPath.keys').
    static bool downloadInitial(const QString &url, const QString &uploadKey, const QString &dbPath,
                                QString *error);
    // Nach der ersten Anmeldung mit einer per downloadInitial() geholten Datenbank aufrufen
    static void adoptInitialState();
    // Zustand des Servers (für "Verbindung testen"): rev < 0 bei Fehler
    static qint64 serverRevision(const QString &url, const QString &uploadKey, QString *message);

    void watchDatabase();             // automatischer Abgleich nach Änderungen

signals:
    void started();
    void finished(bool ok, const QString &message);

private:
    DbSync();
    bool runSync(QString *message);
    void markDirty();

    bool m_busy = false;
    bool m_watching = false;
    class QTimer *m_debounce = nullptr;
};
