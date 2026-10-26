// SPDX-License-Identifier: MIT
#include "smtpclient.h"
#include "database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostInfo>
#include <QLocale>
#include <QPointer>
#include <QRegularExpression>
#include <QSslSocket>
#include <QThread>
#include <QUuid>

namespace Smtp {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("Smtp", s); }

class Session
{
public:
    Session(QSslSocket &s, int timeout) : m_s(s), m_timeout(timeout) {}

    // Liest eine (ggf. mehrzeilige) Antwort; liefert den Statuscode
    int read(QString *text = nullptr)
    {
        QString all;
        for (;;) {
            while (!m_s.canReadLine()) {
                if (!m_s.waitForReadyRead(m_timeout)) {
                    m_error = tr("Keine Antwort vom Server (%1).").arg(m_s.errorString());
                    return -1;
                }
            }
            const QString line = QString::fromUtf8(m_s.readLine()).trimmed();
            all += line + QLatin1Char('\n');
            if (line.size() < 4 || line.at(3) != QLatin1Char('-')) {
                if (text) *text = all;
                m_last = all.trimmed();
                return line.left(3).toInt();
            }
        }
    }

    bool cmd(const QByteArray &c, const QList<int> &ok, QString *text = nullptr, bool hideInError = false)
    {
        m_s.write(c + "\r\n");
        if (!m_s.waitForBytesWritten(m_timeout)) {
            m_error = tr("Senden an den Server fehlgeschlagen (%1).").arg(m_s.errorString());
            return false;
        }
        const int code = read(text);
        if (!ok.contains(code)) {
            if (m_error.isEmpty())
                m_error = tr("Server lehnte „%1“ ab: %2")
                              .arg(hideInError ? QStringLiteral("AUTH …") : QString::fromUtf8(c.left(40)), m_last);
            return false;
        }
        return true;
    }

    QString error() const { return m_error; }
    void setError(const QString &e) { m_error = e; }

private:
    QSslSocket &m_s;
    int m_timeout;
    QString m_error, m_last;
};

QByteArray encodeHeader(const QString &s)
{
    bool ascii = true;
    for (QChar c : s) if (c.unicode() > 126) { ascii = false; break; }
    if (ascii) return s.toLatin1();
    return "=?UTF-8?B?" + s.toUtf8().toBase64() + "?=";
}

QByteArray wrap76(const QByteArray &b64)
{
    QByteArray out;
    for (int i = 0; i < b64.size(); i += 76)
        out += b64.mid(i, 76) + "\r\n";
    return out;
}

} // namespace

bool isValidAddress(const QString &address)
{
    static const QRegularExpression rx(QStringLiteral("^[^@\\s<>]+@[^@\\s<>]+\\.[^@\\s<>]+$"));
    return rx.match(address.trimmed()).hasMatch();
}

Config loadConfig()
{
    Database &db = Database::instance();
    Config c;
    c.host = db.setting(QStringLiteral("smtp/host"));
    c.port = db.setting(QStringLiteral("smtp/port"), QStringLiteral("587")).toInt();
    c.security = Security(db.setting(QStringLiteral("smtp/security"), QStringLiteral("1")).toInt());
    c.user = db.setting(QStringLiteral("smtp/user"));
    c.password = db.setting(QStringLiteral("smtp/password"));
    c.from = db.setting(QStringLiteral("smtp/from"));
    c.includeDetails = db.setting(QStringLiteral("smtp/details"), QStringLiteral("0")) == QLatin1String("1");
    return c;
}

void saveConfig(const Config &c)
{
    Database &db = Database::instance();
    db.setSetting(QStringLiteral("smtp/host"), c.host);
    db.setSetting(QStringLiteral("smtp/port"), QString::number(c.port));
    db.setSetting(QStringLiteral("smtp/security"), QString::number(int(c.security)));
    db.setSetting(QStringLiteral("smtp/user"), c.user);
    db.setSetting(QStringLiteral("smtp/password"), c.password);
    db.setSetting(QStringLiteral("smtp/from"), c.from);
    db.setSetting(QStringLiteral("smtp/details"), c.includeDetails ? QStringLiteral("1") : QStringLiteral("0"));
}

bool send(const Config &c, const QString &to, const QString &subject, const QString &body,
          QString *error, int timeoutMs)
{
    auto failWith = [error](const QString &e) { if (error) *error = e; return false; };
    if (!c.isValid()) return failWith(tr("Der E-Mail-Versand ist nicht eingerichtet."));
    if (!isValidAddress(to)) return failWith(tr("Ungültige Empfängeradresse: %1").arg(to));
    if ((c.security != Security::None) && !QSslSocket::supportsSsl())
        return failWith(tr("Verschlüsselte Verbindungen werden auf diesem System nicht unterstützt (TLS fehlt)."));

    QSslSocket sock;
    if (c.security == Security::SslTls) {
        sock.connectToHostEncrypted(c.host, quint16(c.port));
        if (!sock.waitForEncrypted(timeoutMs))
            return failWith(tr("TLS-Verbindung zu %1:%2 fehlgeschlagen: %3").arg(c.host).arg(c.port).arg(sock.errorString()));
    } else {
        sock.connectToHost(c.host, quint16(c.port));
        if (!sock.waitForConnected(timeoutMs))
            return failWith(tr("Verbindung zu %1:%2 fehlgeschlagen: %3").arg(c.host).arg(c.port).arg(sock.errorString()));
    }
    Session s(sock, timeoutMs);
    if (s.read() != 220) return failWith(s.error().isEmpty() ? tr("Unerwartete Begrüßung des Servers.") : s.error());

    QByteArray helo = QHostInfo::localHostName().toUtf8();
    if (helo.isEmpty()) helo = "localhost";
    QString caps;
    if (!s.cmd("EHLO " + helo, { 250 }, &caps)) return failWith(s.error());

    if (c.security == Security::StartTls) {
        if (!s.cmd("STARTTLS", { 220 })) return failWith(s.error());
        sock.startClientEncryption();
        if (!sock.waitForEncrypted(timeoutMs))
            return failWith(tr("STARTTLS fehlgeschlagen: %1").arg(sock.errorString()));
        if (!s.cmd("EHLO " + helo, { 250 }, &caps)) return failWith(s.error());
    }

    if (!c.user.isEmpty()) {
        if (c.security == Security::None)
            return failWith(tr("Aus Sicherheitsgründen wird das Passwort nicht über eine unverschlüsselte Verbindung gesendet."));
        const QByteArray plain = QByteArray(1, '\0') + c.user.toUtf8() + QByteArray(1, '\0') + c.password.toUtf8();
        if (!s.cmd("AUTH PLAIN " + plain.toBase64(), { 235 }, nullptr, true)) {
            // Rückfall auf AUTH LOGIN
            s.setError({});
            if (!s.cmd("AUTH LOGIN", { 334 }, nullptr, true)
                || !s.cmd(c.user.toUtf8().toBase64(), { 334 }, nullptr, true)
                || !s.cmd(c.password.toUtf8().toBase64(), { 235 }, nullptr, true))
                return failWith(tr("Anmeldung am E-Mail-Server fehlgeschlagen. Benutzername/Passwort prüfen."));
        }
    }

    if (!s.cmd("MAIL FROM:<" + c.from.trimmed().toUtf8() + ">", { 250 })) return failWith(s.error());
    if (!s.cmd("RCPT TO:<" + to.trimmed().toUtf8() + ">", { 250, 251 })) return failWith(s.error());
    if (!s.cmd("DATA", { 354 })) return failWith(s.error());

    QByteArray msg;
    msg += "From: VitaParms <" + c.from.trimmed().toUtf8() + ">\r\n";
    msg += "To: <" + to.trimmed().toUtf8() + ">\r\n";
    msg += "Subject: " + encodeHeader(subject) + "\r\n";
    {
        const QDateTime now = QDateTime::currentDateTime();
        const int off = now.offsetFromUtc() / 60;
        const QString zone = QStringLiteral("%1%2%3").arg(off < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                                 .arg(qAbs(off) / 60, 2, 10, QLatin1Char('0')).arg(qAbs(off) % 60, 2, 10, QLatin1Char('0'));
        msg += "Date: " + QLocale::c().toString(now, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss ")).toLatin1()
             + zone.toLatin1() + "\r\n";
    }
    msg += "Message-ID: <" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + "@vitaparms>\r\n";
    msg += "MIME-Version: 1.0\r\n";
    msg += "Content-Type: text/plain; charset=utf-8\r\n";
    msg += "Content-Transfer-Encoding: base64\r\n";
    msg += "X-Mailer: VitaParms\r\n\r\n";
    QString b = body;
    b.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    msg += wrap76(b.toUtf8().toBase64());   // Base64 enthält keine Zeile ".": kein Dot-Stuffing nötig
    msg += ".";
    if (!s.cmd(msg, { 250 })) return failWith(s.error());
    s.cmd("QUIT", { 221, 250 });
    sock.disconnectFromHost();
    return true;
}

void sendAsync(const Config &c, const QString &to, const QString &subject, const QString &body,
               QObject *context, const std::function<void(bool, const QString &)> &done)
{
    QPointer<QObject> ctx(context);
    QThread *t = QThread::create([=] {
        QString err;
        const bool ok = send(c, to, subject, body, &err);
        if (ctx)
            QMetaObject::invokeMethod(ctx, [=] { if (done) done(ok, err); }, Qt::QueuedConnection);
    });
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

} // namespace Smtp
