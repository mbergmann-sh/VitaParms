// SPDX-License-Identifier: MIT
#include "docfiles.h"
#include "aes256.h"
#include "database.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace DocFiles {

static QString tr(const char *s) { return QCoreApplication::translate("DocFiles", s); }

static QByteArray hmac(const QByteArray &key, const QByteArray &data)
{
    return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256);
}

QString fileId(const QByteArray &dbKey, const QString &docUid)
{
    return QString::fromLatin1(hmac(dbKey, "vitaparms-doc:" + docUid.toUtf8()).toHex());
}

bool isValidId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
    return re.match(id).hasMatch();
}

static void ctr(const QByteArray &key32, const QByteArray &nonce16, QByteArray &data)
{
    Aes256 aes(reinterpret_cast<const uint8_t *>(key32.constData()));
    uint8_t counter[16], block[16];
    memcpy(counter, nonce16.constData(), 16);
    char *p = data.data();
    const qsizetype n = data.size();
    for (qsizetype off = 0; off < n; off += 16) {
        aes.encryptBlock(counter, block);
        const qsizetype len = qMin<qsizetype>(16, n - off);
        for (qsizetype i = 0; i < len; ++i) p[off + i] ^= char(block[i]);
        for (int i = 15; i >= 0; --i)                // Zähler (big endian) erhöhen
            if (++counter[i] != 0) break;
    }
}

static QByteArray encKey(const QByteArray &dbKey) { return hmac(dbKey, "vitaparms-files-enc"); }
static QByteArray macKey(const QByteArray &dbKey) { return hmac(dbKey, "vitaparms-files-mac"); }

QByteArray seal(const QByteArray &plain, const QByteArray &dbKey)
{
    QByteArray nonce(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(nonce.data()), 4);
    QByteArray body = plain;
    ctr(encKey(dbKey), nonce, body);
    QByteArray out = QByteArrayLiteral("VPF1") + nonce + body;
    out += hmac(macKey(dbKey), out);
    return out;
}

bool open(const QByteArray &sealed, const QByteArray &dbKey, QByteArray *plain)
{
    if (sealed.size() < 4 + 16 + 32 || !sealed.startsWith("VPF1")) return false;
    const QByteArray head = sealed.left(sealed.size() - 32);
    const QByteArray tag = sealed.right(32);
    // Vergleich in konstanter Zeit
    const QByteArray expect = hmac(macKey(dbKey), head);
    int diff = 0;
    for (int i = 0; i < 32; ++i) diff |= expect.at(i) ^ tag.at(i);
    if (diff != 0) return false;
    QByteArray body = head.mid(20);
    ctr(encKey(dbKey), head.mid(4, 16), body);
    if (plain) *plain = body;
    return true;
}

QHash<QString, qint64> present(const QByteArray &dbKey)
{
    QHash<QString, qint64> h;
    for (const Database::DocFileRef &r : Database::instance().documentFileRefs())
        if (r.present && !r.uid.isEmpty()) h.insert(fileId(dbKey, r.uid), r.id);
    return h;
}

QHash<QString, qint64> missing(const QByteArray &dbKey)
{
    QHash<QString, qint64> h;
    for (const Database::DocFileRef &r : Database::instance().documentFileRefs())
        if (!r.present && !r.uid.isEmpty()) h.insert(fileId(dbKey, r.uid), r.id);
    return h;
}

QByteArray sealedData(const QString &id, const QByteArray &dbKey)
{
    const qint64 doc = present(dbKey).value(id);
    if (!doc) return {};
    const QByteArray data = Database::instance().documentData(doc);
    return data.isEmpty() ? QByteArray() : seal(data, dbKey);
}

bool storeSealed(const QString &id, const QByteArray &sealed, const QByteArray &dbKey, QString *error)
{
    Database &db = Database::instance();
    qint64 doc = 0;
    QByteArray sha;
    for (const Database::DocFileRef &r : db.documentFileRefs())
        if (!r.uid.isEmpty() && fileId(dbKey, r.uid) == id) { doc = r.id; sha = r.sha256; break; }
    if (!doc) {
        if (error) *error = tr("Zu diesem Inhalt gibt es kein Dokument.");
        return false;
    }
    QByteArray plain;
    if (!open(sealed, dbKey, &plain)) {
        if (error) *error = tr("Der Inhalt eines Dokuments ist beschädigt oder gehört zu einer anderen Datenbank.");
        return false;
    }
    if (!sha.isEmpty() && QCryptographicHash::hash(plain, QCryptographicHash::Sha256) != sha) {
        if (error) *error = tr("Die Prüfsumme eines Dokuments stimmt nicht.");
        return false;
    }
    return db.storeDocumentData(doc, plain, error);
}

} // namespace DocFiles
