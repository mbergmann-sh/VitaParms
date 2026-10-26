// VitaParms – E-Mail mit Dateianhang im Standard-Mailprogramm vorbereiten
//
//  Windows: Simple MAPI (Outlook, Thunderbird, eM Client …)
//  macOS:   Apple Mail ("open -a Mail <Datei>")
//  Linux:   xdg-email --attach (Thunderbird, Evolution, KMail …)
//  Fallback: mailto:-Link ohne Anhang + Dateimanager mit markierter Datei
//
// Die Nachricht wird nur geöffnet, nicht automatisch gesendet.
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace Mailer {

enum class Result { Opened, OpenedWithoutAttachment, Failed };

Result composeWithAttachment(const QString &to, const QString &subject, const QString &body,
                             const QString &attachmentPath);

// Zeigt die Datei im Dateimanager (Explorer/Finder) an
void revealFile(const QString &path);

} // namespace Mailer
