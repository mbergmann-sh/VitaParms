// VitaParms – Abgleich mit einem Tablet oder Smartphone über das USB-Kabel
//
// Ablauf: Die Tablet-App wartet, solange sie angemeldet ist (UsbSyncServer, nur auf 127.0.0.1
// des Tablets erreichbar). Das Desktop-Programm leitet mit dem
// Android-Hilfsprogramm adb einen Port über das USB-Kabel weiter („adb forward“) und verbindet
// sich (UsbSyncClient). Verwenden beide dieselbe Datenbank, weisen sie das gegenseitig über den
// Datenbankschlüssel nach (HMAC über Zufallswerte, ohne den Schlüssel zu übertragen) – dann ist
// kein Code nötig. Sonst (erste Einrichtung, andere Datenbank) bestätigt ein sechsstelliger Code die
// Verbindung, den das Tablet auf der Seite „USB-Abgleich mit dem Computer“ anzeigt.
//
//   * Abgleichen: der Computer holt die Datenbank des Tablets, führt sie Zeile für Zeile mit der
//     eigenen zusammen (wie beim Abgleich über die Website: neue Einträge beider Seiten bleiben,
//     bei gleichzeitiger Änderung gilt die jüngere) und schickt das Ergebnis zurück; das Tablet
//     führt es ebenso ein. Danach haben beide Geräte denselben, neuesten Stand.
//     Voraussetzung: beide Geräte verwenden dieselbe Datenbank (gleicher Schlüssel) – geprüft
//     über einen Nachweis (HMAC), ohne dass der Schlüssel übertragen wird.
//   * Ersetzen (Computer → Tablet oder Tablet → Computer): eine *.vpdb-Übertragungsdatei
//     (Datenbank + Anmeldekonten) ersetzt die Datenbank des anderen Geräts; deren bisheriger
//     Stand bleibt als Kopie erhalten. Für die erste Einrichtung oder unabhängige Datenbanken.
//
// Übertragen wird nur die verschlüsselte Datenbank; der Schlüssel verlässt kein Gerät.
// Nachricht: "VPU1" · u32 Länge · JSON-Kopf · u64 Länge · Dateien (Größen im Kopf, "files").
// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <memory>

class QFile;
class QTcpServer;
class QTcpSocket;
class QTemporaryDir;

namespace UsbSync {

constexpr quint16 kDevicePort = 47811;   // Port der App auf dem Tablet (nur 127.0.0.1)
constexpr int kProtocol = 2;   // 2: Anmeldung auch über den Datenbankschlüssel (ohne Code)

// Nachweis "gleiche Datenbank": HMAC-SHA256(Schlüssel, Zufallswert)
QByteArray proof(const QByteArray &dbKey, const QByteArray &nonce);

// Anmeldekonten (.keys) des anderen Geräts übernehmen, wenn sie jünger sind und zum
// Schlüssel passen. Liefert true, wenn übernommen wurde.
bool adoptKeysIfNewer(const QString &remoteKeys, qint64 remoteMtimeMs);
qint64 keysMtimeMs();

// Eine Nachrichtenverbindung (beide Seiten)
class Channel : public QObject
{
    Q_OBJECT
public:
    explicit Channel(QTcpSocket *socket, QObject *parent = nullptr);
    ~Channel() override;
    QTcpSocket *socket() const { return m_socket; }

    // Kopf + Dateien (Name → Pfad) senden
    bool send(QJsonObject header, const QMap<QString, QString> &files = {}, QString *error = nullptr);

signals:
    // Dateien liegen bis zur nächsten Nachricht in einem temporären Ordner
    void received(const QJsonObject &header, const QMap<QString, QString> &files);
    void protocolError(const QString &message);

private:
    void onReadyRead();
    bool startFiles();

    QTcpSocket *m_socket;
    QByteArray m_buf;
    enum Stage { Magic, Header, PayloadSize, Payload } m_stage = Magic;
    quint32 m_headerLen = 0;
    QJsonObject m_header;
    quint64 m_payloadLeft = 0;
    struct Part { QString name; QString path; quint64 size; };
    QList<Part> m_parts;
    int m_part = 0;
    quint64 m_partLeft = 0;
    QFile *m_out = nullptr;
    std::unique_ptr<QTemporaryDir> m_tmp;
    QMap<QString, QString> m_files;
};

} // namespace UsbSync

// ---------------------------------------------------------------- Tablet (wartet)
class UsbSyncServer : public QObject
{
    Q_OBJECT
public:
    explicit UsbSyncServer(QObject *parent = nullptr);
    ~UsbSyncServer() override;

    bool start(QString *error = nullptr, quint16 port = UsbSync::kDevicePort);
    void stop();
    bool isListening() const;
    QString code() const { return m_code; }          // sechsstellig, z. B. "482 913"
    // Der Code gilt ab dem ersten Anzeigen (Seite „USB-Abgleich“) bis zum Ende des Programms und
    // bleibt dabei gleich (nach fünf falschen Eingaben gilt ein neuer). Ohne Code verbindet sich nur
    // ein Computer mit derselben Datenbank (Nachweis über den Schlüssel).
    void enableCode();
    bool codeEnabled() const { return m_codeEnabled; }

    // Datenbank ersetzen (*.vpdb) – von der App gesetzt (abmelden, importieren, neu anmelden)
    std::function<bool(const QString &bundlePath, QString *error)> importBundle;

signals:
    void codeChanged();
    void status(const QString &text);                 // für die Anzeige
    void connectedChanged(bool connected, const QString &peer);
    void merged(int inserted, int updated, int deleted);   // eigene Datenbank wurde ergänzt
    void replaced();                                  // Datenbank ersetzt → neu anmelden
    void finished(bool ok, const QString &message);   // Ergebnis eines Vorgangs
    void activity();                                  // gegen die automatische Sperre

private:
    void newCode();
    void onConnection();
    void handle(const QJsonObject &h, const QMap<QString, QString> &files);
    void reply(QJsonObject h, const QMap<QString, QString> &files = {});
    void fail(const QString &message);

    QTcpServer *m_server = nullptr;
    QPointer<QTcpSocket> m_socket;
    UsbSync::Channel *m_channel = nullptr;
    QString m_code;
    QString m_peer;
    bool m_authenticated = false;
    int m_badCodes = 0;
    bool m_codeEnabled = false;
    QByteArray m_serverNonce;           // Schlüssel-Anmeldung: Zufallswert des Tablets
};

// ---------------------------------------------------------------- Computer (verbindet sich)
class UsbSyncClient : public QObject
{
    Q_OBJECT
public:
    explicit UsbSyncClient(QObject *parent = nullptr);
    ~UsbSyncClient() override;

    struct Info {
        QString device;       // Gerätename
        QString version;      // VitaParms-Version der App
        bool loggedIn = false;
        bool sameDatabase = false;
    };
    struct Result {
        int localInserted = 0, localUpdated = 0, localDeleted = 0;     // auf dem Computer
        int remoteInserted = 0, remoteUpdated = 0, remoteDeleted = 0;  // auf dem Tablet
        bool keysFromDevice = false, keysToDevice = false;
    };

    bool connectTo(const QString &host, quint16 port, const QString &code, Info *info, QString *error);
    bool isConnected() const;
    void disconnectFromDevice();

    // beide Datenbanken zusammenführen (Computer muss angemeldet sein, Tablet ebenso)
    bool merge(Result *result, QString *error);
    // Computer → Tablet: Datenbank des Tablets ersetzen
    bool sendDatabase(QString *error);
    // Tablet → Computer: *.vpdb des Tablets holen (Pfad bleibt bis zum Ende des Objekts gültig)
    bool fetchDatabase(QString *bundlePath, QString *error);

signals:
    void progress(const QString &text);

private:
    bool request(const QJsonObject &h, const QMap<QString, QString> &files, QJsonObject *reply,
                 QMap<QString, QString> *replyFiles, int timeoutMs, QString *error);

    QTcpSocket *m_socket = nullptr;
    UsbSync::Channel *m_channel = nullptr;
    std::unique_ptr<QTemporaryDir> m_keep;
};
