// SPDX-License-Identifier: MIT
#include "keystore.h"
#include "aes256.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <cstring>

namespace {

QString tr(const char *s) { return QCoreApplication::translate("KeyStore", s); }

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        b[i] = char(QRandomGenerator::system()->bounded(256));
    return b;
}

QByteArray pbkdf2Sha256(const QByteArray &password, const QByteArray &salt, int iterations, int length)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256, password);
    QByteArray out;
    for (quint32 block = 1; out.size() < length; ++block) {
        QByteArray s = salt;
        s.append(char(block >> 24)).append(char(block >> 16)).append(char(block >> 8)).append(char(block));
        mac.reset();
        mac.addData(s);
        QByteArray u = mac.result();
        QByteArray t = u;
        for (int i = 1; i < iterations; ++i) {
            mac.reset();
            mac.addData(u);
            u = mac.result();
            for (int j = 0; j < t.size(); ++j) t[j] = char(t[j] ^ u[j]);
        }
        out.append(t);
    }
    return out.left(length);
}

QByteArray aesCtr(const QByteArray &key32, const QByteArray &nonce16, const QByteArray &data)
{
    const Aes256 aes(reinterpret_cast<const uint8_t *>(key32.constData()));
    uint8_t counter[16];
    memcpy(counter, nonce16.constData(), 16);
    uint8_t stream[16];
    QByteArray out(data.size(), Qt::Uninitialized);
    for (int off = 0; off < data.size(); off += 16) {
        aes.encryptBlock(counter, stream);
        for (int i = 15; i >= 0; --i) if (++counter[i]) break;   // big endian hochzählen
        const int n = std::min<int>(16, int(data.size()) - off);
        for (int i = 0; i < n; ++i) out[off + i] = char(data[off + i] ^ char(stream[i]));
    }
    return out;
}

bool constantTimeEqual(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (int i = 0; i < a.size(); ++i) d |= uchar(a[i] ^ b[i]);
    return d == 0;
}

void wrap(Account &a, const QByteArray &secret, const QByteArray &dbKey)
{
    a.salt = randomBytes(16);
    a.nonce = randomBytes(16);
    a.iterations = KeyStore::kIterations;
    QByteArray kek = pbkdf2Sha256(secret, a.salt, a.iterations, 64);
    a.wrapped = aesCtr(kek.left(32), a.nonce, dbKey);
    a.mac = QMessageAuthenticationCode::hash(a.nonce + a.wrapped, kek.mid(32), QCryptographicHash::Sha256);
    kek.fill(0);
}

bool unwrap(const Account &a, const QByteArray &secret, QByteArray *dbKey)
{
    if (a.wrapped.isEmpty() || a.iterations <= 0) return false;
    QByteArray kek = pbkdf2Sha256(secret, a.salt, a.iterations, 64);
    const QByteArray mac = QMessageAuthenticationCode::hash(a.nonce + a.wrapped, kek.mid(32), QCryptographicHash::Sha256);
    bool ok = constantTimeEqual(mac, a.mac);
    if (ok && dbKey) *dbKey = aesCtr(kek.left(32), a.nonce, a.wrapped);
    kek.fill(0);
    return ok;
}

QJsonObject toJson(const Account &a)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), a.name);
    o.insert(QStringLiteral("role"), a.role == Role::Admin ? QStringLiteral("admin") : QStringLiteral("user"));
    o.insert(QStringLiteral("created"), a.created.toString(Qt::ISODate));
    o.insert(QStringLiteral("salt"), QString::fromLatin1(a.salt.toBase64()));
    o.insert(QStringLiteral("nonce"), QString::fromLatin1(a.nonce.toBase64()));
    o.insert(QStringLiteral("key"), QString::fromLatin1(a.wrapped.toBase64()));
    o.insert(QStringLiteral("mac"), QString::fromLatin1(a.mac.toBase64()));
    o.insert(QStringLiteral("iter"), a.iterations);
    return o;
}

Account fromJson(const QJsonObject &o)
{
    Account a;
    a.name = o.value(QStringLiteral("name")).toString();
    a.role = o.value(QStringLiteral("role")).toString() == QLatin1String("admin") ? Role::Admin : Role::User;
    a.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    a.salt = QByteArray::fromBase64(o.value(QStringLiteral("salt")).toString().toLatin1());
    a.nonce = QByteArray::fromBase64(o.value(QStringLiteral("nonce")).toString().toLatin1());
    a.wrapped = QByteArray::fromBase64(o.value(QStringLiteral("key")).toString().toLatin1());
    a.mac = QByteArray::fromBase64(o.value(QStringLiteral("mac")).toString().toLatin1());
    a.iterations = o.value(QStringLiteral("iter")).toInt();
    return a;
}

} // namespace

QString roleName(Role r)
{
    return r == Role::Admin ? QCoreApplication::translate("KeyStore", "Administrator")
                            : QCoreApplication::translate("KeyStore", "Anwender");
}

QByteArray KeyStore::newDatabaseKey()
{
    return randomBytes(32).toHex();
}

QString KeyStore::passwordProblem(const QString &pw)
{
    if (pw.size() < 8)
        return tr("Das Passwort muss mindestens 8 Zeichen lang sein.");
    if (pw.trimmed() != pw)
        return tr("Das Passwort darf nicht mit Leerzeichen beginnen oder enden.");
    return {};
}

int KeyStore::adminCount() const
{
    int n = 0;
    for (const Account &a : m_accounts) if (a.role == Role::Admin) ++n;
    return n;
}

const Account *KeyStore::find(const QString &name) const
{
    for (const Account &a : m_accounts)
        if (a.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) return &a;
    return nullptr;
}

static QByteArray canonical(const QList<Account> &accounts)
{
    QJsonArray arr;
    for (const Account &a : accounts) arr.append(toJson(a));
    QJsonObject o;
    o.insert(QStringLiteral("accounts"), arr);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QByteArray fileMac(const QByteArray &dbKey, const QByteArray &data)
{
    const QByteArray k = QMessageAuthenticationCode::hash("VitaParms-KeyStore-v1", dbKey, QCryptographicHash::Sha256);
    return QMessageAuthenticationCode::hash(data, k, QCryptographicHash::Sha256);
}

bool KeyStore::load(const QString &path, QString *error)
{
    m_path = path;
    m_loaded = false;
    m_accounts.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toInt() != 1) {
        if (error) *error = tr("Die Schlüsseldatei ist beschädigt oder hat ein unbekanntes Format.");
        return false;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("accounts")).toArray())
        m_accounts << fromJson(v.toObject());
    m_fileMac = QByteArray::fromBase64(root.value(QStringLiteral("mac")).toString().toLatin1());
    m_loaded = true;
    return true;
}

bool KeyStore::save(const QByteArray &dbKey, QString *error) const
{
    const QByteArray data = canonical(m_accounts);
    QJsonObject root = QJsonDocument::fromJson(data).object();
    root.insert(QStringLiteral("format"), 1);
    root.insert(QStringLiteral("app"), QStringLiteral("VitaParms"));
    root.insert(QStringLiteral("mac"), QString::fromLatin1(fileMac(dbKey, data).toBase64()));
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const_cast<KeyStore *>(this)->m_fileMac = fileMac(dbKey, data);
    const_cast<KeyStore *>(this)->m_loaded = true;
    return true;
}

bool KeyStore::verify(const QByteArray &dbKey) const
{
    return constantTimeEqual(fileMac(dbKey, canonical(m_accounts)), m_fileMac);
}

bool KeyStore::unlock(const QString &name, const QString &password, QByteArray *dbKey, Role *role) const
{
    const Account *a = find(name);
    if (!a) {
        // Gleiche Rechenzeit wie bei einem existierenden Konto (kein Hinweis auf gültige Namen)
        pbkdf2Sha256(password.toUtf8(), QByteArray(16, 'x'), kIterations, 64);
        return false;
    }
    if (!unwrap(*a, password.toUtf8(), dbKey)) return false;
    if (role) *role = a->role;
    return true;
}

bool KeyStore::addAccount(const QString &name, const QString &password, Role role,
                          const QByteArray &dbKey, QString *error)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n.size() > 40) {
        if (error) *error = tr("Bitte einen Kontonamen mit 1 bis 40 Zeichen angeben.");
        return false;
    }
    if (find(n)) {
        if (error) *error = tr("Ein Konto mit diesem Namen gibt es bereits.");
        return false;
    }
    const QString p = passwordProblem(password);
    if (!p.isEmpty()) {
        if (error) *error = p;
        return false;
    }
    Account a;
    a.name = n;
    a.role = role;
    a.created = QDateTime::currentDateTime();
    wrap(a, password.toUtf8(), dbKey);
    m_accounts << a;
    return true;
}

bool KeyStore::setPassword(const QString &name, const QString &password, const QByteArray &dbKey)
{
    for (Account &a : m_accounts)
        if (a.name.compare(name, Qt::CaseInsensitive) == 0) {
            wrap(a, password.toUtf8(), dbKey);
            return true;
        }
    return false;
}

bool KeyStore::setRole(const QString &name, Role role, QString *error)
{
    for (Account &a : m_accounts)
        if (a.name.compare(name, Qt::CaseInsensitive) == 0) {
            if (a.role == Role::Admin && role != Role::Admin && adminCount() <= 1) {
                if (error) *error = tr("Es muss mindestens ein Administrator-Konto geben.");
                return false;
            }
            a.role = role;
            return true;
        }
    return false;
}

bool KeyStore::removeAccount(const QString &name, QString *error)
{
    for (int i = 0; i < m_accounts.size(); ++i)
        if (m_accounts[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            if (m_accounts[i].role == Role::Admin && adminCount() <= 1) {
                if (error) *error = tr("Das letzte Administrator-Konto kann nicht gelöscht werden.");
                return false;
            }
            m_accounts.removeAt(i);
            return true;
        }
    return false;
}

// Crockford-Base32 (ohne I, L, O, U); beim Einlesen werden O→0, I/L→1 korrigiert
static const char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

QString KeyStore::recoveryCode(const QByteArray &dbKey)
{
    const QByteArray raw = QByteArray::fromHex(dbKey);
    if (raw.size() != 32) return {};
    QByteArray data = raw + QCryptographicHash::hash(raw, QCryptographicHash::Sha256).left(3);  // 35 Byte = 56 Zeichen
    QString out;
    quint32 buffer = 0;
    int bits = 0;
    for (char ch : data) {
        buffer = (buffer << 8) | quint8(ch);
        bits += 8;
        while (bits >= 5) {
            out += QLatin1Char(kB32[(buffer >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    QString grouped;
    for (int i = 0; i < out.size(); ++i) {
        if (i && i % 4 == 0) grouped += QLatin1Char('-');
        grouped += out[i];
    }
    return grouped;
}

QByteArray KeyStore::keyFromRecoveryCode(const QString &code)
{
    QString c = code.toUpper();
    c.remove(QRegularExpression(QStringLiteral("[\\s-]")));
    c.replace(QLatin1Char('O'), QLatin1Char('0'));
    c.replace(QLatin1Char('I'), QLatin1Char('1'));
    c.replace(QLatin1Char('L'), QLatin1Char('1'));
    if (c.size() != 56) return {};
    QByteArray data;
    quint32 buffer = 0;
    int bits = 0;
    for (QChar ch : c) {
        const char *p = strchr(kB32, ch.toLatin1());
        if (!p || !*p) return {};
        buffer = (buffer << 5) | quint32(p - kB32);
        bits += 5;
        if (bits >= 8) {
            data.append(char((buffer >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    if (data.size() < 35) return {};
    const QByteArray raw = data.left(32);
    if (QCryptographicHash::hash(raw, QCryptographicHash::Sha256).left(3) != data.mid(32, 3))
        return {};   // Tippfehler
    return raw.toHex();
}
