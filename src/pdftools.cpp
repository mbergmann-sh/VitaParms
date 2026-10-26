// SPDX-License-Identifier: MIT
#include "pdftools.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>
#ifndef VP_NO_PRINTSUPPORT
#include <QPrinter>
#endif
#include <QRegularExpression>
#include <QTemporaryFile>

#ifdef VP_HAVE_QTPDF
#include <QPdfDocument>
#include <QPdfSelection>
#endif

namespace PdfTools {

static QString tr(const char *s) { return QCoreApplication::translate("PdfTools", s); }

bool available()
{
#ifdef VP_HAVE_QTPDF
    return true;
#else
    return false;
#endif
}

bool isPdf(const QByteArray &data)
{
    return data.left(1024).contains("%PDF-");
}

#ifdef VP_HAVE_QTPDF
// QPdfDocument lädt aus Dateien synchron – daher kurzzeitig eine private Temporärdatei.
class LoadedPdf
{
public:
    explicit LoadedPdf(const QByteArray &data)
    {
        m_tmp.setFileTemplate(QDir::tempPath() + QStringLiteral("/vitaparms-XXXXXX.pdf"));
        if (m_tmp.open()) {
            m_tmp.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            m_tmp.write(data);
            m_tmp.flush();
            m_err = m_doc.load(m_tmp.fileName());
        }
    }
    bool ok() const { return m_err == QPdfDocument::Error::None; }
    QPdfDocument &doc() { return m_doc; }
    QString errorText() const
    {
        switch (m_err) {
        case QPdfDocument::Error::IncorrectPassword:   return QCoreApplication::translate("PdfTools", "PDF ist kennwortgeschützt");
        case QPdfDocument::Error::UnsupportedSecurityScheme: return QCoreApplication::translate("PdfTools", "Sicherheitsverfahren nicht unterstützt");
        case QPdfDocument::Error::InvalidFileFormat:   return QCoreApplication::translate("PdfTools", "keine gültige PDF-Datei");
        case QPdfDocument::Error::None:                return {};
        default:                                       return QCoreApplication::translate("PdfTools", "PDF konnte nicht gelesen werden");
        }
    }

private:
    QTemporaryFile m_tmp;
    QPdfDocument   m_doc;
    QPdfDocument::Error m_err = QPdfDocument::Error::Unknown;
};
#endif

Info analyze(const QByteArray &pdf)
{
    Info info;
    if (!isPdf(pdf)) {
        info.error = tr("keine PDF-Datei");
        return info;
    }
#ifdef VP_HAVE_QTPDF
    LoadedPdf l(pdf);
    if (!l.ok()) {
        info.error = l.errorText();
        return info;
    }
    info.ok = true;
    info.pages = l.doc().pageCount();
    QStringList parts;
    for (int i = 0; i < info.pages; ++i)
        parts << l.doc().getAllText(i).text();
    info.text = parts.join(QLatin1Char('\n'));
    info.metaTitle = l.doc().metaData(QPdfDocument::MetaDataField::Title).toString().trimmed();
    info.metaDate = l.doc().metaData(QPdfDocument::MetaDataField::CreationDate).toDateTime().date();
#else
    // Ohne Qt PDF: Seiten grob zählen
    static const QRegularExpression rx(QStringLiteral("/Type\\s*/Page(?!s)"));
    info.pages = int(QString::fromLatin1(pdf).count(rx));
    info.ok = true;
#endif
    return info;
}

QByteArray imageToPdf(const QImage &img, const QString &title)
{
    return imagesToPdf({ img }, title);
}

QByteArray imagesToPdf(const QList<QImage> &images, const QString &title)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QPdfWriter w(&buf);
    w.setTitle(title);
    w.setCreator(QStringLiteral("VitaParms"));
    w.setResolution(200);
    QPainter p;
    bool first = true;
    for (const QImage &img : images) {
        if (img.isNull()) continue;
        const bool landscape = img.width() > img.height();
        const QPageLayout layout(QPageSize(QPageSize::A4), landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                                 QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
        if (first) {
            w.setPageLayout(layout);
            if (!p.begin(&w)) return {};
            first = false;
        } else {
            w.setPageLayout(layout);
            w.newPage();
        }
        const QRect area(QPoint(0, 0), w.pageLayout().paintRectPixels(w.resolution()).size());
        const QSize s = img.size().scaled(area.size(), Qt::KeepAspectRatio);
        // Große Fotos verkleinern (max. 200 dpi auf A4), damit die Akte klein bleibt
        const QImage scaled = img.width() > s.width() ? img.scaled(s, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img;
        p.drawImage(QRect(QPoint((area.width() - s.width()) / 2, 0), s), scaled.convertToFormat(QImage::Format_RGB32));
    }
    if (first) return {};
    p.end();
    return out;
}

static QDate parseDate(const QString &s)
{
    static const QList<QPair<QRegularExpression, QString>> pats = {
        { QRegularExpression(QStringLiteral("(?<!\\d)(\\d{4})-(\\d{2})-(\\d{2})(?!\\d)")), QStringLiteral("ymd") },
        { QRegularExpression(QStringLiteral("(?<!\\d)(\\d{4})(\\d{2})(\\d{2})(?!\\d)")), QStringLiteral("ymd") },
        { QRegularExpression(QStringLiteral("(?<!\\d)(\\d{1,2})\\.(\\d{1,2})\\.(\\d{4})(?!\\d)")), QStringLiteral("dmy") },
        { QRegularExpression(QStringLiteral("(?<!\\d)(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2})(?![\\d.])")), QStringLiteral("dmy2") },
    };
    for (const auto &[rx, kind] : pats) {
        const auto m = rx.match(s);
        if (!m.hasMatch()) continue;
        int y, mo, d;
        if (kind == QLatin1String("ymd")) { y = m.captured(1).toInt(); mo = m.captured(2).toInt(); d = m.captured(3).toInt(); }
        else { d = m.captured(1).toInt(); mo = m.captured(2).toInt(); y = m.captured(3).toInt(); if (kind == QLatin1String("dmy2")) y += 2000; }
        const QDate dt(y, mo, d);
        if (dt.isValid()) return dt;
    }
    return {};
}

QDate guessDate(const QString &fileName, const Info &info, const QDate &birthDate)
{
    const QDate today = QDate::currentDate();
    auto plausible = [&](const QDate &d) {
        return d.isValid() && d <= today && d.year() >= 1950 && d != birthDate;
    };
    // 1. Dateiname
    const QDate fn = parseDate(QFileInfo(fileName).completeBaseName());
    if (plausible(fn)) return fn;

    // 2. Briefkopf: jüngstes plausibles Datum im Anfangsbereich
    //    (Geburtsdatum und Zukunftsdaten werden ausgeschlossen)
    const QString head = info.text.left(2500);
    QDate best;
    auto consider = [&](const QDate &d) {
        if (plausible(d) && (!best.isValid() || d > best)) best = d;
    };
    // 30.09.2026 / 30. 9. 2026 und – britisch/europäisch – 30/09/2026
    static const QRegularExpression rxDmy(QStringLiteral("(?<!\\d)(\\d{1,2})(?:\\.\\s?|/)(\\d{1,2})(?:\\.\\s?|/)(\\d{4})(?!\\d)"));
    for (auto it = rxDmy.globalMatch(head); it.hasNext();) {
        const auto m = it.next();
        consider(QDate(m.captured(3).toInt(), m.captured(2).toInt(), m.captured(1).toInt()));
    }
    // 2026-09-30
    static const QRegularExpression rxIso(QStringLiteral("(?<!\\d)(\\d{4})-(\\d{2})-(\\d{2})(?!\\d)"));
    for (auto it = rxIso.globalMatch(head); it.hasNext();) {
        const auto m = it.next();
        consider(QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));
    }
    // Monatsnamen: "30. September 2026", "30 September 2026", "September 30, 2026"
    static const QStringList months = {
        QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("m(?:ar|är)"), QStringLiteral("apr"),
        QStringLiteral("ma[iy]"), QStringLiteral("jun"), QStringLiteral("jul"), QStringLiteral("aug"),
        QStringLiteral("sep"), QStringLiteral("o[ck]t"), QStringLiteral("nov"), QStringLiteral("de[cz]") };
    auto monthOf = [](const QString &name) {
        const QString n = name.toLower();
        for (int i = 0; i < months.size(); ++i)
            if (QRegularExpression(QStringLiteral("^") + months[i]).match(n).hasMatch()) return i + 1;
        return 0;
    };
    static const QString mon = QStringLiteral("([A-Za-zÄäö]{3,9})\\.?");
    static const QRegularExpression rxDayMon(QStringLiteral("(?<!\\d)(\\d{1,2})\\.?\\s+") + mon + QStringLiteral("\\s+(\\d{4})(?!\\d)"));
    for (auto it = rxDayMon.globalMatch(head); it.hasNext();) {
        const auto m = it.next();
        if (const int mo = monthOf(m.captured(2)))
            consider(QDate(m.captured(3).toInt(), mo, m.captured(1).toInt()));
    }
    static const QRegularExpression rxMonDay(mon + QStringLiteral("\\s+(\\d{1,2}),?\\s+(\\d{4})(?!\\d)"));
    for (auto it = rxMonDay.globalMatch(head); it.hasNext();) {
        const auto m = it.next();
        if (const int mo = monthOf(m.captured(1)))
            consider(QDate(m.captured(3).toInt(), mo, m.captured(2).toInt()));
    }
    if (best.isValid()) return best;

    // 3. Erstellungsdatum der PDF-Datei
    if (plausible(info.metaDate)) return info.metaDate;
    return today;
}

QStringList defaultCategories()
{
    return { tr("Arztbrief"), tr("Befund"), tr("Laborwerte"), tr("Entlassbrief"),
             tr("Bildgebung"), tr("OP-Bericht"), tr("Überweisung"), tr("Rezept"),
             tr("Impfnachweis"), tr("Bescheinigung"), tr("Sonstiges") };
}

QString guessCategory(const QString &fileName, const QString &text)
{
    const QString hay = (QFileInfo(fileName).completeBaseName() + QLatin1Char(' ') + text.left(4000)).toLower();
    struct Rule { const char *needle; const char *category; };
    static const Rule rules[] = {
        { "entlass", "Entlassbrief" },     { "discharge", "Entlassbrief" },
        { "operationsbericht", "OP-Bericht" }, { "operative report", "OP-Bericht" },
        { "op-bericht", "OP-Bericht" },    { "laborbefund", "Laborwerte" },
        { "blutbild", "Laborwerte" },      { "labor", "Laborwerte" },
        { "röntgen", "Bildgebung" },       { "mrt", "Bildgebung" },
        { "ct-", "Bildgebung" },           { "sonograph", "Bildgebung" },
        { "ultraschall", "Bildgebung" },   { "überweisung", "Überweisung" },
        { "rezept", "Rezept" },            { "impf", "Impfnachweis" },
        { "arbeitsunfähig", "Bescheinigung" }, { "bescheinigung", "Bescheinigung" },
        { "befund", "Befund" },            { "arztbrief", "Arztbrief" },
        { "sehr geehrte", "Arztbrief" },
        // englische Dokumente
        { "laboratory", "Laborwerte" },    { "blood count", "Laborwerte" },
        { "x-ray", "Bildgebung" },         { "radiolog", "Bildgebung" },
        { "ultrasound", "Bildgebung" },    { "referral", "Überweisung" },
        { "prescription", "Rezept" },      { "vaccinat", "Impfnachweis" },
        { "immunis", "Impfnachweis" },     { "immuniz", "Impfnachweis" },
        { "sick note", "Bescheinigung" },  { "certificate", "Bescheinigung" },
        { "findings", "Befund" },          { "dear dr", "Arztbrief" },
        { "dear colleague", "Arztbrief" },
    };
    for (const Rule &r : rules)
        if (hay.contains(QString::fromUtf8(r.needle)))
            return tr(r.category);
    return {};
}

QString guessTitle(const QString &fileName, const Info &info)
{
    if (!info.metaTitle.isEmpty() && info.metaTitle.size() < 120
        && !info.metaTitle.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
        && !info.metaTitle.contains(QStringLiteral("Microsoft Word")))
        return info.metaTitle;
    QString base = QFileInfo(fileName).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[_]+")), QStringLiteral(" "));
    return base.simplified();
}

QImage renderPage(const QByteArray &pdf, int page, int widthPx)
{
#ifdef VP_HAVE_QTPDF
    LoadedPdf l(pdf);
    if (!l.ok() || page < 0 || page >= l.doc().pageCount()) return {};
    const QSizeF pts = l.doc().pagePointSize(page);
    const QSize sz(widthPx, qRound(widthPx * pts.height() / pts.width()));
    QImage img = l.doc().render(page, sz);
    QImage out(img.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    return out;
#else
    Q_UNUSED(pdf) Q_UNUSED(page) Q_UNUSED(widthPx)
    return {};
#endif
}

bool printDocuments(QPrinter *printer, const QList<QByteArray> &pdfs, QString *error)
{
#if defined(VP_HAVE_QTPDF) && !defined(VP_NO_PRINTSUPPORT)
    QPainter p;
    if (!p.begin(printer)) {
        if (error) *error = tr("Drucker konnte nicht geöffnet werden.");
        return false;
    }
    const int dpi = std::min(printer->resolution(), 300);   // Speicherbedarf begrenzen
    bool first = true;
    for (const QByteArray &data : pdfs) {
        LoadedPdf l(data);
        if (!l.ok()) continue;
        for (int i = 0; i < l.doc().pageCount(); ++i) {
            if (!first) printer->newPage();
            first = false;
            const QRectF area(QPointF(0, 0), printer->pageLayout().paintRectPixels(printer->resolution()).size());
            const QSizeF pts = l.doc().pagePointSize(i);
            const QSize imgSize(qRound(pts.width() / 72.0 * dpi), qRound(pts.height() / 72.0 * dpi));
            QImage img = l.doc().render(i, imgSize);
            QSizeF target = QSizeF(img.size()).scaled(area.size(), Qt::KeepAspectRatio);
            // Seiten, die kleiner als der Druckbereich sind, nicht vergrößern
            const QSizeF natural(pts.width() / 72.0 * printer->resolution(), pts.height() / 72.0 * printer->resolution());
            if (natural.width() < target.width()) target = natural;
            const QRectF r(QPointF((area.width() - target.width()) / 2, 0), target);
            p.fillRect(r, Qt::white);
            p.drawImage(r, img);
        }
    }
    p.end();
    return true;
#else
    Q_UNUSED(printer) Q_UNUSED(pdfs)
    if (error) *error = tr("Diese Programmversion wurde ohne Qt PDF übersetzt.");
    return false;
#endif
}

QString snippet(const QString &text, const QString &term, int context)
{
    const int i = text.indexOf(term, 0, Qt::CaseInsensitive);
    if (i < 0) return {};
    const int a = std::max(0, i - context);
    const int b = std::min<int>(text.size(), i + term.size() + context);
    QString s = text.mid(a, b - a).simplified();
    if (a > 0) s.prepend(QStringLiteral("… "));
    if (b < text.size()) s.append(QStringLiteral(" …"));
    return s;
}

} // namespace PdfTools
