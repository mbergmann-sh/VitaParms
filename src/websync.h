// VitaParms – Website: verschlüsseltes, schreibgeschütztes Datenpaket aller Personen
//
// VitaParms baut aus der Datenbank ein Paket (Messwerte des gesamten Verlaufs, Medikamente,
// Termine, Berichte als PDF/Excel), verschlüsselt es mit dem Master-Passwort der Website und
// lädt es per HTTPS hoch. Der Server speichert nur Chiffrat; entschlüsselt wird erst im
// Browser nach Eingabe des Passworts (Web Crypto API).
//
// Format "VPW1" (je Datei):  "VPW1" | IV (16) | AES-256-CTR(Daten) | HMAC-SHA256 (32)
//   Schlüssel: PBKDF2-HMAC-SHA256(Passwort, Salz, Iterationen) → 64 Byte = AES-Schlüssel | MAC-Schlüssel
//   CTR: Zählerblock = IV, die unteren 64 Bit werden (big endian) hochgezählt
//   HMAC über "VPW1" | IV | Chiffrat
// data.vpw enthält zlib-komprimiertes JSON, Berichte liegen unkomprimiert darin.
// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace WebCrypto {
constexpr int kIterations = 210000;
QByteArray randomBytes(int n);
QByteArray deriveKeys(const QString &password, const QByteArray &salt, int iterations = kIterations);
QByteArray encrypt(const QByteArray &plain, const QByteArray &keys);
QByteArray decrypt(const QByteArray &blob, const QByteArray &keys, bool *ok = nullptr);   // für Tests
QByteArray checkValue(const QByteArray &keys);            // erlaubt der Website, das Passwort zu prüfen
}

class WebSync : public QObject
{
    Q_OBJECT
public:
    struct Config {
        bool    enabled = false;
        QString url;              // z. B. https://vitaparms.example.de/
        QString password;         // Master-Passwort der Website (Verschlüsselung)
        QString uploadKey;        // berechtigt zum Hochladen (getrennt vom Passwort)
        bool    autoPublish = true;   // nach Änderungen und beim Beenden
        QDateTime lastUpload;
        QString lastResult;
    };
    static Config config();                    // aus der (verschlüsselten) Datenbank
    static void   saveConfig(const Config &c);
    static QString generateUploadKey();
    static QString normalizedUrl(const QString &url);
    static bool   isReady(const Config &c, QString *why = nullptr);

    // Paket bauen: Dateiname → Inhalt (verschlüsselt) plus unverschlüsseltes Manifest
    struct Bundle {
        QString id;
        QByteArray manifest;                    // current.json
        QMap<QString, QByteArray> files;        // data.vpw, r1.vpw, …
        QByteArray plainJson;                   // zum Prüfen/Testen
        int persons = 0, readings = 0;
    };
    static Bundle buildBundle(const Config &c, QString *error = nullptr);

    static WebSync &instance();

    // Hochladen (asynchron); Ergebnis über finished()
    void publish();
    // Für das Programmende und Tests: wartet höchstens timeoutMs
    bool publishBlocking(int timeoutMs, QString *message);
    void testConnection();
    bool isBusy() const { return m_building || m_reply != nullptr; }

    // Automatisch: nach Datenänderungen (verzögert) hochladen, falls eingerichtet
    void watchDatabase();
    bool hasPendingChanges() const { return m_dirty; }

signals:
    void started();
    void finished(bool ok, const QString &message);
    void connectionTested(bool ok, const QString &message);

private:
    WebSync();
    void markDirty();
    QNetworkReply *startUpload(const Config &c, const Bundle &b);
    void handleUploadReply(QNetworkReply *r);
    static QString replyMessage(QNetworkReply *r, const QByteArray &body, bool *ok);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_debounce = nullptr;
    bool m_dirty = false;
    bool m_building = false;
    bool m_watching = false;
};
