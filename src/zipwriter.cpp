// SPDX-License-Identifier: MIT
#include "zipwriter.h"

#include "aes256.h"

#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QFile>
#include <QSaveFile>

namespace {

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data)
        c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xFFFF)); put16(b, quint16(v >> 16)); }

void dosDateTime(quint16 &date, quint16 &time)
{
    const QDateTime now = QDateTime::currentDateTime();
    time = quint16((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
    date = quint16(((now.date().year() - 1980) << 9) | (now.date().month() << 5) | now.date().day());
}

// Rohes "deflate" aus qCompress: 4 Byte Länge + 2 Byte zlib-Kopf … 4 Byte Adler-32
QByteArray rawDeflate(const QByteArray &data)
{
    const QByteArray z = qCompress(data, 9);
    if (z.size() < 10) return {};
    return z.mid(6, z.size() - 10);
}

// PBKDF2-HMAC-SHA1 (RFC 2898)
QByteArray pbkdf2Sha1(const QByteArray &password, const QByteArray &salt, int iterations, int length)
{
    QByteArray out;
    for (quint32 block = 1; out.size() < length; ++block) {
        QByteArray s = salt;
        s.append(char(block >> 24)).append(char(block >> 16)).append(char(block >> 8)).append(char(block));
        QByteArray u = QMessageAuthenticationCode::hash(s, password, QCryptographicHash::Sha1);
        QByteArray t = u;
        for (int i = 1; i < iterations; ++i) {
            u = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha1);
            for (int j = 0; j < t.size(); ++j) t[j] = char(t[j] ^ u[j]);
        }
        out.append(t);
    }
    return out.left(length);
}

// WinZip-AES (AE-2, 256 Bit): Salz | Passwortprüfwert | AES-CTR-Daten | HMAC-SHA1 (10 Byte)
QByteArray winzipAesEncrypt(const QByteArray &plain, const QString &password)
{
    QByteArray salt(16, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), 4);
    const QByteArray keys = pbkdf2Sha1(password.toUtf8(), salt, 1000, 66);
    const Aes256 aes(reinterpret_cast<const uint8_t *>(keys.constData()));
    const QByteArray macKey = keys.mid(32, 32);

    QByteArray cipher(plain.size(), Qt::Uninitialized);
    uint8_t counter[16] = {};
    uint8_t stream[16];
    quint64 n = 0;
    for (qsizetype off = 0; off < plain.size(); off += 16) {
        ++n;                                   // Zähler beginnt bei 1, little endian
        for (int i = 0; i < 8; ++i) counter[i] = uint8_t(n >> (8 * i));
        aes.encryptBlock(counter, stream);
        const qsizetype len = std::min<qsizetype>(16, plain.size() - off);
        for (qsizetype i = 0; i < len; ++i)
            cipher[off + i] = char(plain[off + i] ^ char(stream[i]));
    }
    const QByteArray mac = QMessageAuthenticationCode::hash(cipher, macKey, QCryptographicHash::Sha1).left(10);
    return salt + keys.mid(64, 2) + cipher + mac;
}

} // namespace

void ZipWriter::addFile(const QString &name, const QByteArray &data, bool compress)
{
    Entry e;
    e.name   = name.toUtf8();
    e.crc    = crc32(data);
    e.size   = quint32(data.size());
    e.offset = quint32(m_buf.size());

    QByteArray payload = data;
    e.method = 0;
    if (compress && data.size() > 64) {
        const QByteArray d = rawDeflate(data);
        if (!d.isEmpty() && d.size() < data.size()) {
            payload = d;
            e.method = 8;
        }
    }
    if (!m_password.isEmpty()) {
        const quint16 realMethod = e.method;
        payload = winzipAesEncrypt(payload, m_password);
        e.method = 99;                  // AES
        e.flags |= 0x0001;              // verschlüsselt
        e.crc = 0;                      // AE-2: keine CRC (Integrität über HMAC)
        put16(e.extra, 0x9901);         // AES-Extrafeld
        put16(e.extra, 7);
        put16(e.extra, 2);              // AE-2
        e.extra.append("AE", 2);
        e.extra.append(char(3));        // 256 Bit
        put16(e.extra, realMethod);
    }
    e.compSize = quint32(payload.size());

    quint16 dd, dt;
    dosDateTime(dd, dt);
    put32(m_buf, 0x04034b50);           // local file header
    put16(m_buf, e.method == 99 ? 51 : 20); // version needed
    put16(m_buf, e.flags);
    put16(m_buf, e.method);
    put16(m_buf, dt);
    put16(m_buf, dd);
    put32(m_buf, e.crc);
    put32(m_buf, e.compSize);
    put32(m_buf, e.size);
    put16(m_buf, quint16(e.name.size()));
    put16(m_buf, quint16(e.extra.size()));
    m_buf.append(e.name);
    m_buf.append(e.extra);
    m_buf.append(payload);
    m_entries.append(e);
}

QByteArray ZipWriter::finish()
{
    QByteArray out = m_buf;
    const quint32 cdOffset = quint32(out.size());
    quint16 dd, dt;
    dosDateTime(dd, dt);
    for (const Entry &e : m_entries) {
        put32(out, 0x02014b50);         // central directory header
        put16(out, 63);                 // version made by
        put16(out, e.method == 99 ? 51 : 20); // version needed
        put16(out, e.flags);
        put16(out, e.method);
        put16(out, dt);
        put16(out, dd);
        put32(out, e.crc);
        put32(out, e.compSize);
        put32(out, e.size);
        put16(out, quint16(e.name.size()));
        put16(out, quint16(e.extra.size()));
        put16(out, 0);                  // comment
        put16(out, 0);                  // disk
        put16(out, 0);                  // internal attrs
        put32(out, 0);                  // external attrs
        put32(out, e.offset);
        out.append(e.name);
        out.append(e.extra);
    }
    const quint32 cdSize = quint32(out.size()) - cdOffset;
    put32(out, 0x06054b50);             // end of central directory
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(m_entries.size()));
    put16(out, quint16(m_entries.size()));
    put32(out, cdSize);
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

bool ZipWriter::save(const QString &path, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(finish());
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}
