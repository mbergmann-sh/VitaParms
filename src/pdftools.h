// VitaParms – Hilfsfunktionen für PDF-Dokumente der Krankenakte
// Nutzt Qt PDF (LGPLv3), falls beim Bauen vorhanden (VP_HAVE_QTPDF).
// Ohne Qt PDF: keine Volltextsuche, keine eingebaute Vorschau, Druck über
// das Standardprogramm des Systems.
// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QDate>
#include <QImage>
#include <QList>
#include <QString>

class QPrinter;

namespace PdfTools {

bool available();                     // mit Qt PDF übersetzt?
bool isPdf(const QByteArray &data);

struct Info {
    bool    ok = false;
    QString error;
    int     pages = 0;
    QString text;                     // gesamter Text aller Seiten
    QString metaTitle;
    QDate   metaDate;                 // Erstellungsdatum laut PDF
};
Info analyze(const QByteArray &pdf);

// Bilder (Foto/Scan) in ein einseitiges A4-PDF umwandeln
QByteArray imageToPdf(const QImage &img, const QString &title);
// mehrere Fotos/Scans (z. B. Seiten eines Briefs) in ein A4-PDF, eine Seite je Bild
QByteArray imagesToPdf(const QList<QImage> &images, const QString &title);

// Vorschläge für die Metadaten beim Import
QDate   guessDate(const QString &fileName, const Info &info, const QDate &birthDate);
QString guessCategory(const QString &fileName, const QString &text);
QString guessTitle(const QString &fileName, const Info &info);
QStringList defaultCategories();

// Seiten rendern / drucken
QImage renderPage(const QByteArray &pdf, int page, int widthPx);
bool   printDocuments(QPrinter *printer, const QList<QByteArray> &pdfs, QString *error = nullptr);

// Kurzer Textausschnitt um den ersten Treffer (für Tooltips)
QString snippet(const QString &text, const QString &term, int context = 60);

} // namespace PdfTools
