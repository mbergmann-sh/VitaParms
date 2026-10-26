// SPDX-License-Identifier: MIT
#include "mailer.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QWidget>

#ifdef Q_OS_WIN
int vpMapiSendMail(void *hwnd, wchar_t *to, wchar_t *subject, wchar_t *body,
                   wchar_t *path, wchar_t *fileName);
#endif

namespace Mailer {

static bool openMailto(const QString &to, const QString &subject, const QString &body)
{
    const QByteArray enc = "mailto:" + QUrl::toPercentEncoding(to, "@") + "?subject="
                         + QUrl::toPercentEncoding(subject) + "&body=" + QUrl::toPercentEncoding(body);
    return QDesktopServices::openUrl(QUrl::fromEncoded(enc));
}

void revealFile(const QString &path)
{
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,") + QDir::toNativeSeparators(path) });
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), path });
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

Result composeWithAttachment(const QString &to, const QString &subject, const QString &body,
                             const QString &attachmentPath)
{
    const QString abs = QFileInfo(attachmentPath).absoluteFilePath();
#if defined(Q_OS_WIN)
    std::wstring wTo = to.toStdWString(), wSubj = subject.toStdWString(), wBody = body.toStdWString(),
                 wPath = QDir::toNativeSeparators(abs).toStdWString(),
                 wName = QFileInfo(abs).fileName().toStdWString();
    QWidget *w = QApplication::activeWindow();
    const int rc = vpMapiSendMail(w ? reinterpret_cast<void *>(w->winId()) : nullptr, wTo.data(), wSubj.data(),
                                  wBody.data(), wPath.data(), wName.data());
    if (rc == 0 || rc == 1)          // 1 = vom Benutzer abgebrochen
        return Result::Opened;
#elif defined(Q_OS_MACOS)
    // Apple Mail legt beim Öffnen einer Datei eine neue Nachricht mit Anhang an
    if (QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-a"), QStringLiteral("Mail"), abs }))
        return Result::Opened;
#else
    const QString xdg = QStandardPaths::findExecutable(QStringLiteral("xdg-email"));
    if (!xdg.isEmpty()) {
        QStringList args = { QStringLiteral("--utf8"), QStringLiteral("--subject"), subject,
                             QStringLiteral("--body"), body, QStringLiteral("--attach"), abs };
        if (!to.isEmpty()) args << to;
        if (QProcess::startDetached(xdg, args))
            return Result::Opened;
    }
#endif
    // Fallback: Nachricht ohne Anhang + Datei im Dateimanager zeigen
    const bool ok = openMailto(to, subject, body);
    revealFile(abs);
    return ok ? Result::OpenedWithoutAttachment : Result::Failed;
}

} // namespace Mailer
