// VitaParms – minimaler ZIP-Schreiber (für .xlsx und die Akten-Archive), ohne
// Fremdbibliothek. Komprimierung über zlib aus QtCore (qCompress), ansonsten "stored".
// Optional AES-256-Verschlüsselung im WinZip-AES-Format (AE-2), lesbar u. a. mit
// 7-Zip, WinRAR, WinZip, PeaZip, Keka/The Unarchiver (nicht: Windows-Explorer,
// macOS-Archivierungsprogramm, Info-ZIP unzip 6.0).
// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

class ZipWriter
{
public:
    // Ab dem Setzen werden alle folgenden Dateien verschlüsselt (leer = keine Verschlüsselung)
    void setPassword(const QString &password) { m_password = password; }
    void addFile(const QString &name, const QByteArray &data, bool compress = true);
    QByteArray finish();      // liefert das komplette ZIP-Archiv
    bool save(const QString &path, QString *error = nullptr);

private:
    struct Entry {
        QByteArray name;
        quint32 crc = 0, compSize = 0, size = 0, offset = 0;
        quint16 method = 0;
        quint16 flags = 0x0800;     // UTF-8-Dateinamen
        QByteArray extra;
    };
    QString      m_password;
    QByteArray   m_buf;
    QList<Entry> m_entries;
};
