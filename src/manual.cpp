// SPDX-License-Identifier: MIT
#include "manual.h"
#include "i18n.h"
#include "edition.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>

namespace Manual {

QString fileName(const QString &lang, Format format, Doc doc)
{
    const bool de = lang == QLatin1String("de");
    if (Edition::light()) {       // VitaParms light: eigene, einfache Handbücher
        const QString base = doc == Doc::GettingStarted
            ? (de ? QStringLiteral("VitaParms-light-ErsteSchritte") : QStringLiteral("VitaParms-light-GettingStarted"))
            : (de ? QStringLiteral("VitaParms-light-Benutzerhandbuch") : QStringLiteral("VitaParms-light-UserGuide"));
        return base + (format == Format::Chm ? QStringLiteral(".chm") : QStringLiteral(".pdf"));
    }
    const QString base = doc == Doc::GettingStarted
        ? (de ? QStringLiteral("VitaParms-ErsteSchritte") : QStringLiteral("VitaParms-GettingStarted"))
        : (de ? QStringLiteral("VitaParms-Handbuch") : QStringLiteral("VitaParms-Manual"));
    return base + (format == Format::Chm ? QStringLiteral(".chm") : QStringLiteral(".pdf"));
}

QString title(Doc doc)
{
    if (doc == Doc::GettingStarted) return QCoreApplication::translate("Manual", "Erste Schritte");
    return Edition::light() ? QCoreApplication::translate("Manual", "Benutzerhandbuch")
                            : QCoreApplication::translate("Manual", "Handbuch");
}

QStringList searchDirs()
{
    const QDir app(QCoreApplication::applicationDirPath());
    QStringList dirs{
        app.filePath(QStringLiteral("docs")),                     // <Programmordner>/docs
        app.filePath(QStringLiteral("../docs")),                  // Build-Unterordner release/debug
        app.filePath(QStringLiteral("../Resources/docs")),        // macOS-Bundle
        app.filePath(QStringLiteral("../share/vitaparms/docs")),  // Linux: /usr/local/bin → share
        app.filePath(QStringLiteral("../share/vitaparms-light/docs")),
    };
    for (QString &d : dirs) d = QDir::cleanPath(d);
    dirs.removeDuplicates();
    return dirs;
}

Format preferredFormat()
{
#ifdef Q_OS_WIN
    return Format::Chm;
#else
    return Format::Pdf;
#endif
}

QString locate(const QString &lang, Format preferred, const QStringList &dirs, Doc doc)
{
    const QString other = lang == QLatin1String("de") ? QStringLiteral("en") : QStringLiteral("de");
    QList<QPair<QString, Format>> order{ { lang, preferred } };
    if (preferred != Format::Pdf) order.append({ lang, Format::Pdf });
    order.append({ other, preferred });
    if (preferred != Format::Pdf) order.append({ other, Format::Pdf });
    for (const auto &[l, f] : order)
        for (const QString &d : dirs) {
            const QFileInfo fi(QDir(d).filePath(fileName(l, f, doc)));
            if (fi.isFile()) return fi.absoluteFilePath();
        }
    return {};
}

bool open(QWidget *parent, Doc doc)
{
    const QString lang = I18n::effectiveLanguage();
    const QString path = locate(lang, preferredFormat(), searchDirs(), doc);
    if (path.isEmpty()) {
        QMessageBox::warning(parent, title(doc),
            QCoreApplication::translate("Manual",
                "Das Dokument „%1“ wurde nicht gefunden.\n\nErwartet wird die Datei „%2“ im Ordner\n%3")
                .arg(title(doc), fileName(lang, preferredFormat(), doc), QDir::toNativeSeparators(searchDirs().constFirst())));
        return false;
    }
#ifdef Q_OS_WIN
    if (path.endsWith(QLatin1String(".chm"), Qt::CaseInsensitive)) {
        // Windows-Hilfe (HTML Help) – hh.exe liegt im Windows-Ordner
        if (QProcess::startDetached(QStringLiteral("hh.exe"), { QDir::toNativeSeparators(path) }))
            return true;
    }
#endif
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return true;
    QMessageBox::warning(parent, title(doc),
        QCoreApplication::translate("Manual",
            "Das Dokument konnte nicht geöffnet werden. Ist ein Programm zum Anzeigen von PDF-Dateien installiert?\n\n%1")
            .arg(QDir::toNativeSeparators(path)));
    return false;
}

} // namespace Manual
