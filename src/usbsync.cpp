// VitaParms – Abgleich mit einem Tablet oder Smartphone über das USB-Kabel (siehe usbsync.h)
// SPDX-License-Identifier: MIT
#include "usbsync.h"
#include "docfiles.h"

#include "database.h"
#include "dbsync.h"
#include "dbtransfer.h"
#include "keystore.h"
#include "session.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSet>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>

#ifndef VITAPARMS_VERSION
#define VITAPARMS_VERSION "?"
#endif

namespace {

const QByteArray kMagic("VPU1");
constexpr quint32 kMaxHeader = 1024 * 1024;
constexpr quint64 kMaxPayload = quint64(4) * 1024 * 1024 * 1024;   // 4 GB

QString fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QString statsText(int ins, int upd, int del)
{
    return QCoreApplication::translate("UsbSyncServer", "%1 neu, %2 geändert, %3 gelöscht").arg(ins).arg(upd).arg(del);
}
} // namespace

// ================================================================ Hilfsfunktionen

QByteArray UsbSync::proof(const QByteArray &dbKey, const QByteArray &nonce)
{
    return QMessageAuthenticationCode::hash(QByteArrayLiteral("vitaparms-usb-sync:") + nonce, dbKey,
                                            QCryptographicHash::Sha256);
}

qint64 UsbSync::keysMtimeMs()
{
    const QFileInfo fi(KeyStore::pathFor(Database::instance().path()));
    return fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
}

bool UsbSync::adoptKeysIfNewer(const QString &remoteKeys, qint64 remoteMtimeMs)
{
    const QString keysPath = KeyStore::pathFor(Database::instance().path());
    if (remoteKeys.isEmpty() || !QFile::exists(remoteKeys)) return false;
    if (fileHash(remoteKeys) == fileHash(keysPath)) return false;
    if (remoteMtimeMs <= keysMtimeMs()) return false;               // eigene Konten sind jünger
    KeyStore probe;
    if (!probe.load(remoteKeys) || !probe.verify(Session::dbKey())) return false;
    QFile::remove(keysPath + QStringLiteral(".bak"));
    QFile::copy(keysPath, keysPath + QStringLiteral(".bak"));
    QFile::remove(keysPath);
    if (!QFile::copy(remoteKeys, keysPath)) {
        QFile::copy(keysPath + QStringLiteral(".bak"), keysPath);
        return false;
    }
    QFile f(keysPath);
    if (f.open(QIODevice::ReadWrite))
        f.setFileTime(QDateTime::fromMSecsSinceEpoch(remoteMtimeMs), QFileDevice::FileModificationTime);
    f.close();
    Session::keyStore().load(keysPath);
    return true;
}

// ================================================================ Channel

using UsbSync::Channel;

Channel::Channel(QTcpSocket *socket, QObject *parent)
    : QObject(parent), m_socket(socket)
{
    connect(m_socket, &QTcpSocket::readyRead, this, &Channel::onReadyRead);
}

Channel::~Channel()
{
    delete m_out;
}

bool Channel::send(QJsonObject header, const QMap<QString, QString> &files, QString *error)
{
    QJsonArray list;
    quint64 total = 0;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const qint64 size = QFileInfo(it.value()).size();
        if (!QFileInfo::exists(it.value())) {
            if (error) *error = tr("Datei fehlt: %1").arg(it.value());
            return false;
        }
        list << QJsonObject{ { QStringLiteral("name"), it.key() }, { QStringLiteral("size"), QString::number(size) } };
        total += quint64(size);
    }
    header.insert(QStringLiteral("files"), list);
    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray head = kMagic;
    char n4[4];
    qToBigEndian<quint32>(quint32(json.size()), n4);
    head.append(n4, 4);
    head.append(json);
    char n8[8];
    qToBigEndian<quint64>(total, n8);
    head.append(n8, 8);
    m_socket->write(head);
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QFile f(it.value());
        if (!f.open(QIODevice::ReadOnly)) {
            if (error) *error = f.errorString();
            m_socket->abort();
            return false;
        }
        while (!f.atEnd()) {
            m_socket->write(f.read(1024 * 1024));
            // Puffer nicht beliebig wachsen lassen
            while (m_socket->bytesToWrite() > 8 * 1024 * 1024 && m_socket->state() == QAbstractSocket::ConnectedState)
                m_socket->waitForBytesWritten(1000);
        }
    }
    m_socket->flush();
    return true;
}

bool Channel::startFiles()
{
    m_parts.clear();
    m_files.clear();
    m_tmp = std::make_unique<QTemporaryDir>();
    if (!m_tmp->isValid()) return false;
    quint64 sum = 0;
    int i = 0;
    for (const QJsonValue &v : m_header.value(QStringLiteral("files")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        // nur einfache Namen (kein Pfad)
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.startsWith(QLatin1Char('.')))
            return false;
        const quint64 size = o.value(QStringLiteral("size")).toString().toULongLong();
        const QString path = m_tmp->filePath(QStringLiteral("%1-%2").arg(i++).arg(name));
        m_parts << Part{ name, path, size };
        m_files.insert(name, path);
        sum += size;
    }
    return sum == m_payloadLeft;
}

void Channel::onReadyRead()
{
    m_buf.append(m_socket->readAll());
    for (;;) {
        switch (m_stage) {
        case Magic:
            if (m_buf.size() < 8) return;
            if (!m_buf.startsWith(kMagic)) { emit protocolError(tr("Unbekannte Gegenstelle.")); m_socket->abort(); return; }
            m_headerLen = qFromBigEndian<quint32>(m_buf.constData() + 4);
            if (m_headerLen > kMaxHeader) { emit protocolError(tr("Ungültige Nachricht.")); m_socket->abort(); return; }
            m_buf.remove(0, 8);
            m_stage = Header;
            break;
        case Header: {
            if (quint32(m_buf.size()) < m_headerLen) return;
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(m_buf.left(int(m_headerLen)), &pe);
            m_buf.remove(0, int(m_headerLen));
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) { emit protocolError(tr("Ungültige Nachricht.")); m_socket->abort(); return; }
            m_header = doc.object();
            m_stage = PayloadSize;
            break;
        }
        case PayloadSize:
            if (m_buf.size() < 8) return;
            m_payloadLeft = qFromBigEndian<quint64>(m_buf.constData());
            m_buf.remove(0, 8);
            if (m_payloadLeft > kMaxPayload || !startFiles()) { emit protocolError(tr("Ungültige Nachricht.")); m_socket->abort(); return; }
            m_part = 0;
            m_partLeft = m_parts.isEmpty() ? 0 : m_parts.first().size;
            m_stage = Payload;
            break;
        case Payload: {
            while (m_part < m_parts.size()) {
                if (!m_out) {
                    m_out = new QFile(m_parts.at(m_part).path);
                    if (!m_out->open(QIODevice::WriteOnly)) { emit protocolError(m_out->errorString()); m_socket->abort(); return; }
                }
                if (m_partLeft > 0) {
                    if (m_buf.isEmpty()) return;
                    const int take = int(qMin<quint64>(m_partLeft, quint64(m_buf.size())));
                    m_out->write(m_buf.constData(), take);
                    m_buf.remove(0, take);
                    m_partLeft -= quint64(take);
                    if (m_partLeft > 0) return;
                }
                m_out->close();
                delete m_out;
                m_out = nullptr;
                if (++m_part < m_parts.size()) m_partLeft = m_parts.at(m_part).size;
            }
            m_stage = Magic;
            const QJsonObject h = m_header;
            const QMap<QString, QString> f = m_files;
            QPointer<Channel> self(this);
            emit received(h, f);
            if (!self || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
            break;
        }
        }
    }
}

// ================================================================ Server (Tablet)

UsbSyncServer::UsbSyncServer(QObject *parent) : QObject(parent) {}

UsbSyncServer::~UsbSyncServer() { stop(); }

void UsbSyncServer::enableCode()
{
    if (!m_codeEnabled) newCode();               // einmal je Programmlauf
    m_codeEnabled = true;
    emit status(m_authenticated ? tr("Verbunden mit dem Computer %1.").arg(m_peer) : tr("Wartet auf den Computer …"));
}

void UsbSyncServer::newCode()
{
    const quint32 n = QRandomGenerator::system()->bounded(1000000u);
    m_code = QStringLiteral("%1 %2").arg(n / 1000, 3, 10, QLatin1Char('0')).arg(n % 1000, 3, 10, QLatin1Char('0'));
    m_badCodes = 0;
    emit codeChanged();
}

bool UsbSyncServer::start(QString *error, quint16 port)
{
    if (isListening()) return true;
    m_server = new QTcpServer(this);
    // nur lokal erreichbar: der Computer kommt über „adb forward“ (USB-Kabel) herein
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        if (error) *error = tr("Der USB-Abgleich lässt sich nicht starten: %1").arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &UsbSyncServer::onConnection);
    if (m_code.isEmpty()) newCode();
    emit status(tr("Wartet auf den Computer …"));
    return true;
}

void UsbSyncServer::stop()
{
    if (m_socket) m_socket->abort();
    delete m_channel;
    m_channel = nullptr;
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

bool UsbSyncServer::isListening() const
{
    return m_server && m_server->isListening();
}

void UsbSyncServer::onConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_socket) {
            if (m_authenticated && m_socket->state() == QAbstractSocket::ConnectedState) {
                s->abort();        // immer nur ein Computer
                s->deleteLater();
                continue;
            }
            // abgebrochene oder noch nicht bestätigte Verbindung ersetzen
            QTcpSocket *old = m_socket;
            m_socket = nullptr;
            delete m_channel;
            m_channel = nullptr;
            old->abort();
            old->deleteLater();
        }
        m_socket = s;
        m_authenticated = false;
        m_channel = new UsbSync::Channel(s, this);
        connect(m_channel, &UsbSync::Channel::received, this, &UsbSyncServer::handle);
        connect(m_channel, &UsbSync::Channel::protocolError, this, [this](const QString &m) { emit status(m); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            s->deleteLater();
            if (s != m_socket) return;          // bereits ersetzt
            if (m_channel) {
                m_channel->deleteLater();
                m_channel = nullptr;
            }
            m_socket = nullptr;
            if (m_authenticated) emit connectedChanged(false, m_peer);
            m_authenticated = false;
            emit status(tr("Wartet auf den Computer …"));
        });
        emit activity();
    }
}

void UsbSyncServer::reply(QJsonObject h, const QMap<QString, QString> &files)
{
    if (m_channel) m_channel->send(h, files);
}

void UsbSyncServer::fail(const QString &message)
{
    if (m_authenticated) emit status(tr("Verbunden mit dem Computer %1.").arg(m_peer));
    reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), message } });
    emit finished(false, message);
}

void UsbSyncServer::handle(const QJsonObject &h, const QMap<QString, QString> &files)
{
    emit activity();
    const QString cmd = h.value(QStringLiteral("cmd")).toString();
    Database &db = Database::instance();
    const bool loggedIn = Session::isLoggedIn() && db.isOpen();

    auto accept = [&](const QString &peer) {
        m_authenticated = true;
        m_peer = peer;
        emit connectedChanged(true, m_peer);
        emit status(tr("Verbunden mit dem Computer %1.").arg(m_peer));
    };
    if (cmd == QLatin1String("hello")) {
        if (h.value(QStringLiteral("proto")).toInt() != UsbSync::kProtocol) {
            reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("proto") } });
            if (m_socket) m_socket->disconnectFromHost();
            return;
        }
        QJsonObject r{ { QStringLiteral("ok"), true },
                       { QStringLiteral("proto"), UsbSync::kProtocol },
                       { QStringLiteral("version"), QStringLiteral(VITAPARMS_VERSION) },
                       { QStringLiteral("device"), QSysInfo::prettyProductName() },
                       { QStringLiteral("loggedIn"), loggedIn } };
        const QByteArray nonce = QByteArray::fromBase64(h.value(QStringLiteral("nonce")).toString().toLatin1());
        if (loggedIn)
            r.insert(QStringLiteral("proof"), QString::fromLatin1(UsbSync::proof(Session::dbKey(), nonce).toBase64()));
        QString given = h.value(QStringLiteral("code")).toString();
        given.remove(QLatin1Char(' '));
        if (given.isEmpty()) {
            // ohne Code: nur mit demselben Datenbankschlüssel (zweiter Schritt „auth“)
            if (!loggedIn) {
                reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("locked") } });
                if (m_socket) m_socket->disconnectFromHost();
                return;
            }
            m_serverNonce = QByteArray(32, Qt::Uninitialized);
            QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(m_serverNonce.data()), m_serverNonce.size() / 4);
            m_peer = h.value(QStringLiteral("host")).toString();
            r.insert(QStringLiteral("auth"), QStringLiteral("key"));
            r.insert(QStringLiteral("challenge"), QString::fromLatin1(m_serverNonce.toBase64()));
            reply(r);
            return;
        }
        if (!m_codeEnabled) {
            reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("nocode") } });
            if (m_socket) m_socket->disconnectFromHost();
            return;
        }
        QString mine = m_code;
        mine.remove(QLatin1Char(' '));
        // zeitkonstanter Vergleich
        bool same = given.size() == mine.size();
        for (int i = 0; i < mine.size() && i < given.size(); ++i) same &= given.at(i) == mine.at(i);
        if (!same) {
            if (++m_badCodes >= 5) {
                newCode();
                emit status(tr("Mehrmals ein falscher Code – es gilt jetzt ein neuer Code."));
            }
            reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("code") } });
            if (m_socket) m_socket->disconnectFromHost();
            return;
        }
        reply(r);
        accept(h.value(QStringLiteral("host")).toString());
        return;
    }
    if (cmd == QLatin1String("auth") && !m_authenticated) {
        const QByteArray theirs = QByteArray::fromBase64(h.value(QStringLiteral("proof")).toString().toLatin1());
        const bool ok = loggedIn && !m_serverNonce.isEmpty()
                     && theirs == UsbSync::proof(Session::dbKey(), QByteArrayLiteral("client:") + m_serverNonce);
        m_serverNonce.clear();
        if (!ok) {
            reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("key") } });
            if (m_socket) m_socket->disconnectFromHost();
            return;
        }
        reply({ { QStringLiteral("ok"), true } });
        accept(m_peer);
        return;
    }
    if (!m_authenticated) { fail(tr("Nicht angemeldet.")); return; }

    if (cmd == QLatin1String("bye")) {
        if (m_socket) m_socket->disconnectFromHost();
        return;
    }
    if (cmd == QLatin1String("putBundle")) {
        emit status(tr("Datenbank vom Computer wird übernommen …"));
        const QString bundle = files.value(QStringLiteral("bundle"));
        QString err;
        // vor dem Import beantworten wäre zu früh – erst ersetzen, dann Ergebnis melden
        if (!importBundle) { fail(tr("Nicht möglich.")); return; }
        const QString keep = QDir::temp().filePath(QStringLiteral("vitaparms-usb-%1.vpdb").arg(QCoreApplication::applicationPid()));
        QFile::remove(keep);
        if (!QFile::copy(bundle, keep)) { fail(tr("Die Datei konnte nicht gespeichert werden.")); return; }
        const bool ok = importBundle(keep, &err);
        QFile::remove(keep);
        if (!ok) { fail(err); return; }
        reply({ { QStringLiteral("ok"), true } });
        emit finished(true, tr("Die Datenbank des Computers wurde übernommen. Bitte mit einem Konto des Computers anmelden."));
        emit replaced();
        return;
    }

    if (!loggedIn) {
        fail(tr("Bitte zuerst auf dem Tablet anmelden."));
        return;
    }

    if (cmd == QLatin1String("pull")) {
        emit status(tr("Datenbank wird an den Computer übertragen …"));
        QTemporaryDir tmp;
        const QString copy = tmp.filePath(QStringLiteral("db.sqlite"));
        QString err;
        const bool docFiles = h.value(QStringLiteral("docFiles")).toBool();
        if (!db.backupTo(copy, &err, docFiles)) { fail(err); return; }
        QJsonObject r{ { QStringLiteral("ok"), true }, { QStringLiteral("keysMtime"), QString::number(UsbSync::keysMtimeMs()) } };
        if (docFiles) {
            r.insert(QStringLiteral("docFiles"), true);
            r.insert(QStringLiteral("have"), QJsonArray::fromStringList(DocFiles::present(Session::dbKey()).keys()));
        }
        reply(r, { { QStringLiteral("db"), copy }, { QStringLiteral("keys"), KeyStore::pathFor(db.path()) } });
        emit status(tr("Verbunden mit dem Computer %1.").arg(m_peer));
        return;
    }
    if (cmd == QLatin1String("push")) {
        emit status(tr("Stand des Computers wird eingearbeitet …"));
        DbMerge::Stats st;
        QString err;
        if (!DbMerge::mergeFrom(files.value(QStringLiteral("db")), Session::dbKey(), &st, &err)) { fail(err); return; }
        if (st.total() > 0) db.notifyExternalChange();
        const bool keys = UsbSync::adoptKeysIfNewer(files.value(QStringLiteral("keys")),
                                                     h.value(QStringLiteral("keysMtime")).toString().toLongLong());
        QJsonObject r{ { QStringLiteral("ok"), true }, { QStringLiteral("inserted"), st.inserted },
                       { QStringLiteral("updated"), st.updated }, { QStringLiteral("deleted"), st.deleted },
                       { QStringLiteral("keys"), keys } };
        if (h.value(QStringLiteral("docFiles")).toBool())
            r.insert(QStringLiteral("missing"), QJsonArray::fromStringList(DocFiles::missing(Session::dbKey()).keys()));
        reply(r);
        emit merged(st.inserted, st.updated, st.deleted);
        emit status(tr("Verbunden mit dem Computer %1.").arg(m_peer));
        emit finished(true, tr("Mit dem Computer abgeglichen: %1.").arg(statsText(st.inserted, st.updated, st.deleted))
                                + (keys ? tr(" Anmeldekonten übernommen.") : QString()));
        return;
    }
    if (cmd == QLatin1String("getFiles")) {          // Dokumentinhalte an den Computer
        QTemporaryDir tmp;
        QMap<QString, QString> out;
        for (const QJsonValue &v : h.value(QStringLiteral("ids")).toArray()) {
            const QString id = v.toString();
            if (!DocFiles::isValidId(id)) continue;
            const QByteArray sealed = DocFiles::sealedData(id, Session::dbKey());
            if (sealed.isEmpty()) continue;
            QFile f(tmp.filePath(id));
            if (!f.open(QIODevice::WriteOnly) || f.write(sealed) != sealed.size()) { fail(f.errorString()); return; }
            out.insert(id, f.fileName());
        }
        reply({ { QStringLiteral("ok"), true } }, out);
        return;
    }
    if (cmd == QLatin1String("putFiles")) {          // Dokumentinhalte vom Computer
        int stored = 0;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            QFile f(it.value());
            QString err;
            if (!f.open(QIODevice::ReadOnly) || !DocFiles::storeSealed(it.key(), f.readAll(), Session::dbKey(), &err)) {
                fail(err.isEmpty() ? f.errorString() : err);
                return;
            }
            ++stored;
        }
        if (stored) db.notifyDocumentsChanged();
        reply({ { QStringLiteral("ok"), true }, { QStringLiteral("stored"), stored } });
        return;
    }
    if (cmd == QLatin1String("getBundle")) {
        emit status(tr("Datenbank wird an den Computer übertragen …"));
        QTemporaryDir tmp;
        const QString bundle = tmp.filePath(QStringLiteral("tablet.vpdb"));
        QString err;
        if (!DbTransfer::exportBundle(bundle, &err)) { fail(err); return; }
        reply({ { QStringLiteral("ok"), true } }, { { QStringLiteral("bundle"), bundle } });
        emit status(tr("Verbunden mit dem Computer %1.").arg(m_peer));
        emit finished(true, tr("Die Datenbank dieses Geräts wurde an den Computer übertragen."));
        return;
    }
    fail(tr("Unbekannter Befehl."));
}

// ================================================================ Client (Computer)

UsbSyncClient::UsbSyncClient(QObject *parent) : QObject(parent) {}

UsbSyncClient::~UsbSyncClient()
{
    disconnectFromDevice();
}

bool UsbSyncClient::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void UsbSyncClient::disconnectFromDevice()
{
    if (isConnected()) {
        m_channel->send({ { QStringLiteral("cmd"), QStringLiteral("bye") } });
        m_socket->waitForBytesWritten(1000);
        m_socket->disconnectFromHost();
    }
    delete m_channel;
    m_channel = nullptr;
    delete m_socket;
    m_socket = nullptr;
}

bool UsbSyncClient::request(const QJsonObject &h, const QMap<QString, QString> &files, QJsonObject *reply,
                            QMap<QString, QString> *replyFiles, int timeoutMs, QString *error)
{
    if (!isConnected()) {
        if (error) *error = tr("Keine Verbindung zum Tablet.");
        return false;
    }
    QEventLoop loop;
    bool got = false;
    QString problem;
    auto c1 = connect(m_channel, &UsbSync::Channel::received, &loop, [&](const QJsonObject &r, const QMap<QString, QString> &f) {
        got = true;
        if (reply) *reply = r;
        if (replyFiles) *replyFiles = f;
        loop.quit();
    });
    auto c2 = connect(m_channel, &UsbSync::Channel::protocolError, &loop, [&](const QString &m) { problem = m; loop.quit(); });
    auto c3 = connect(m_socket, &QTcpSocket::disconnected, &loop, [&] { loop.quit(); });
    // Zeitlimit: ohne eingehende Daten (große Datenbanken dürfen länger dauern)
    QTimer idle;
    idle.setSingleShot(true);
    idle.setInterval(timeoutMs);
    connect(&idle, &QTimer::timeout, &loop, [&] { problem = tr("Das Tablet antwortet nicht."); loop.quit(); });
    auto c4 = connect(m_socket, &QTcpSocket::readyRead, &idle, [&] { idle.start(); });
    idle.start();
    QString err;
    if (!m_channel->send(h, files, &err)) {
        if (error) *error = err;
        return false;
    }
    loop.exec();
    disconnect(c1);
    disconnect(c2);
    disconnect(c3);
    disconnect(c4);
    if (!got) {
        if (error) *error = problem.isEmpty() ? tr("Die Verbindung zum Tablet wurde getrennt.") : problem;
        return false;
    }
    if (reply && !reply->value(QStringLiteral("ok")).toBool()) {
        if (error) *error = reply->value(QStringLiteral("error")).toString();
        return false;
    }
    return true;
}

bool UsbSyncClient::connectTo(const QString &host, quint16 port, const QString &code, Info *info, QString *error)
{
    disconnectFromDevice();
    m_socket = new QTcpSocket(this);
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(8000)) {
        if (error) *error = tr("Keine Verbindung: %1").arg(m_socket->errorString());
        disconnectFromDevice();
        return false;
    }
    m_channel = new UsbSync::Channel(m_socket, this);
    QByteArray nonce(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(nonce.data()), nonce.size() / 4);
    QJsonObject r;
    QString err;
    if (!request({ { QStringLiteral("cmd"), QStringLiteral("hello") }, { QStringLiteral("code"), code },
                   { QStringLiteral("proto"), UsbSync::kProtocol },
                   { QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64()) },
                   { QStringLiteral("version"), QStringLiteral(VITAPARMS_VERSION) },
                   { QStringLiteral("host"), QSysInfo::machineHostName() } },
                 {}, &r, nullptr, 15000, &err)) {
        if (err == QLatin1String("code"))
            err = tr("Der Code stimmt nicht. Bitte den Code eingeben, den das Tablet gerade anzeigt.");
        else if (err == QLatin1String("nocode"))
            err = tr("Am Tablet wurde seit dem Start von VitaParms noch kein Code angezeigt. Bitte dort Einstellungen → "
                     "„USB-Abgleich mit dem Computer“ öffnen – oder das Codefeld leer lassen, wenn beide Geräte dieselbe "
                     "Datenbank verwenden.");
        else if (err == QLatin1String("locked"))
            err = tr("Am Tablet ist niemand angemeldet. Bitte VitaParms auf dem Tablet öffnen und anmelden – oder für die "
                     "erste Einrichtung dort „USB-Abgleich mit dem Computer“ öffnen und den Code eingeben.");
        else if (err == QLatin1String("proto"))
            err = tr("VitaParms auf Computer und Tablet passen nicht zusammen. Bitte beide auf denselben Stand bringen.");
        else if (r.isEmpty() && !isConnected())
            err = tr("VitaParms auf dem Tablet antwortet nicht. Bitte das Tablet entsperren, VitaParms öffnen und "
                     "anmelden (die App muss im Vordergrund sein).");
        if (error) *error = err;
        disconnectFromDevice();
        return false;
    }
    if (info) {
        info->device = r.value(QStringLiteral("device")).toString();
        info->version = r.value(QStringLiteral("version")).toString();
        info->loggedIn = r.value(QStringLiteral("loggedIn")).toBool();
        info->sameDatabase = false;
        if (info->loggedIn && Session::isLoggedIn()) {
            const QByteArray theirs = QByteArray::fromBase64(r.value(QStringLiteral("proof")).toString().toLatin1());
            const QByteArray mine = UsbSync::proof(Session::dbKey(), nonce);
            info->sameDatabase = !theirs.isEmpty() && theirs == mine;
        }
    }
    // ohne Code: das Tablet hat seinen Schlüssel nachgewiesen – jetzt weist der Computer seinen nach
    if (r.value(QStringLiteral("auth")).toString() == QLatin1String("key")) {
        const QByteArray theirs = QByteArray::fromBase64(r.value(QStringLiteral("proof")).toString().toLatin1());
        if (!Session::isLoggedIn() || theirs.isEmpty() || theirs != UsbSync::proof(Session::dbKey(), nonce)) {
            if (error) *error = tr("Das Tablet verwendet eine andere Datenbank. Für die erste Einrichtung bitte am Tablet "
                                   "„USB-Abgleich mit dem Computer“ öffnen und den angezeigten Code eingeben.");
            disconnectFromDevice();
            return false;
        }
        const QByteArray challenge = QByteArray::fromBase64(r.value(QStringLiteral("challenge")).toString().toLatin1());
        QJsonObject a;
        if (!request({ { QStringLiteral("cmd"), QStringLiteral("auth") },
                       { QStringLiteral("proof"), QString::fromLatin1(UsbSync::proof(Session::dbKey(), QByteArrayLiteral("client:") + challenge).toBase64()) } },
                     {}, &a, nullptr, 15000, &err)) {
            if (error) *error = tr("Die Anmeldung am Tablet ist fehlgeschlagen.");
            disconnectFromDevice();
            return false;
        }
        if (info) info->sameDatabase = true;
    }
    return true;
}

bool UsbSyncClient::merge(Result *result, QString *error)
{
    Database &db = Database::instance();
    Result res;
    QJsonObject r;
    QMap<QString, QString> f;
    // 1. Stand des Tablets holen und hier einarbeiten
    emit progress(tr("Datenbank des Tablets wird geholt …"));
    if (!request({ { QStringLiteral("cmd"), QStringLiteral("pull") }, { QStringLiteral("docFiles"), true } }, {}, &r, &f, 60000, error))
        return false;
    const bool docFiles = r.value(QStringLiteral("docFiles")).toBool();     // ältere App: Datenbank vollständig
    QSet<QString> deviceHas;
    for (const QJsonValue &v : r.value(QStringLiteral("have")).toArray()) deviceHas.insert(v.toString());
    emit progress(tr("Wird zusammengeführt …"));
    DbMerge::Stats st;
    QString err;
    if (!DbMerge::mergeFrom(f.value(QStringLiteral("db")), Session::dbKey(), &st, &err)) {
        if (error) *error = err;
        return false;
    }
    if (st.total() > 0) db.notifyExternalChange();
    res.localInserted = st.inserted;
    res.localUpdated = st.updated;
    res.localDeleted = st.deleted;
    res.keysFromDevice = UsbSync::adoptKeysIfNewer(f.value(QStringLiteral("keys")), r.value(QStringLiteral("keysMtime")).toString().toLongLong());
    if (docFiles) {          // Inhalte, die hier fehlen und das Tablet hat
        QStringList want;
        const QHash<QString, qint64> missing = DocFiles::missing(Session::dbKey());
        for (auto it = missing.cbegin(); it != missing.cend(); ++it)
            if (deviceHas.contains(it.key())) want << it.key();
        if (!want.isEmpty()) {
            emit progress(tr("Dokumente werden vom Tablet geholt …"));
            if (!fetchFiles(want, &res.filesFromDevice, error)) return false;
        }
    }

    // 2. zusammengeführten Stand an das Tablet schicken
    emit progress(tr("Stand wird an das Tablet übertragen …"));
    QTemporaryDir tmp;
    const QString copy = tmp.filePath(QStringLiteral("db.sqlite"));
    if (!db.backupTo(copy, &err, docFiles)) {
        if (error) *error = err;
        return false;
    }
    const QString keysPath = KeyStore::pathFor(db.path());
    if (!request({ { QStringLiteral("cmd"), QStringLiteral("push") }, { QStringLiteral("keysMtime"), QString::number(UsbSync::keysMtimeMs()) },
                   { QStringLiteral("docFiles"), docFiles } },
                 { { QStringLiteral("db"), copy }, { QStringLiteral("keys"), keysPath } }, &r, nullptr, 120000, error))
        return false;
    if (docFiles) {          // Inhalte, die dem Tablet fehlen
        QStringList give;
        const QHash<QString, qint64> mine = DocFiles::present(Session::dbKey());
        for (const QJsonValue &v : r.value(QStringLiteral("missing")).toArray())
            if (mine.contains(v.toString())) give << v.toString();
        if (!give.isEmpty()) {
            emit progress(tr("Dokumente werden an das Tablet übertragen …"));
            if (!sendFiles(give, &res.filesToDevice, error)) return false;
        }
    }
    res.remoteInserted = r.value(QStringLiteral("inserted")).toInt();
    res.remoteUpdated = r.value(QStringLiteral("updated")).toInt();
    res.remoteDeleted = r.value(QStringLiteral("deleted")).toInt();
    res.keysToDevice = r.value(QStringLiteral("keys")).toBool();
    if (result) *result = res;
    return true;
}

bool UsbSyncClient::fetchFiles(const QStringList &ids, int *count, QString *error)
{
    for (int i = 0; i < ids.size(); i += 10) {
        QJsonObject r;
        QMap<QString, QString> f;
        if (!request({ { QStringLiteral("cmd"), QStringLiteral("getFiles") },
                       { QStringLiteral("ids"), QJsonArray::fromStringList(ids.mid(i, 10)) } }, {}, &r, &f, 120000, error))
            return false;
        for (auto it = f.cbegin(); it != f.cend(); ++it) {
            QFile in(it.value());
            QString err;
            if (!in.open(QIODevice::ReadOnly) || !DocFiles::storeSealed(it.key(), in.readAll(), Session::dbKey(), &err)) {
                if (error) *error = err.isEmpty() ? in.errorString() : err;
                return false;
            }
            ++*count;
        }
    }
    if (*count) Database::instance().notifyDocumentsChanged();
    return true;
}

bool UsbSyncClient::sendFiles(const QStringList &ids, int *count, QString *error)
{
    for (int i = 0; i < ids.size(); i += 10) {
        QTemporaryDir tmp;
        QMap<QString, QString> out;
        for (const QString &id : ids.mid(i, 10)) {
            const QByteArray sealed = DocFiles::sealedData(id, Session::dbKey());
            if (sealed.isEmpty()) continue;
            QFile f(tmp.filePath(id));
            if (!f.open(QIODevice::WriteOnly) || f.write(sealed) != sealed.size()) {
                if (error) *error = f.errorString();
                return false;
            }
            out.insert(id, f.fileName());
        }
        QJsonObject r;
        if (!request({ { QStringLiteral("cmd"), QStringLiteral("putFiles") } }, out, &r, nullptr, 120000, error)) return false;
        *count += r.value(QStringLiteral("stored")).toInt();
    }
    return true;
}

bool UsbSyncClient::sendDatabase(QString *error)
{
    emit progress(tr("Datenbank wird an das Tablet übertragen …"));
    QTemporaryDir tmp;
    const QString bundle = tmp.filePath(QStringLiteral("computer.vpdb"));
    QString err;
    if (!DbTransfer::exportBundle(bundle, &err)) {
        if (error) *error = err;
        return false;
    }
    QJsonObject r;
    return request({ { QStringLiteral("cmd"), QStringLiteral("putBundle") } }, { { QStringLiteral("bundle"), bundle } },
                   &r, nullptr, 120000, error);
}

bool UsbSyncClient::fetchDatabase(QString *bundlePath, QString *error)
{
    emit progress(tr("Datenbank des Tablets wird geholt …"));
    QJsonObject r;
    QMap<QString, QString> f;
    if (!request({ { QStringLiteral("cmd"), QStringLiteral("getBundle") } }, {}, &r, &f, 60000, error)) return false;
    m_keep = std::make_unique<QTemporaryDir>();
    const QString target = m_keep->filePath(QStringLiteral("tablet.vpdb"));
    if (!QFile::copy(f.value(QStringLiteral("bundle")), target)) {
        if (error) *error = tr("Die Datei konnte nicht gespeichert werden.");
        return false;
    }
    if (bundlePath) *bundlePath = target;
    return true;
}
