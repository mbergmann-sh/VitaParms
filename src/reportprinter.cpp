// SPDX-License-Identifier: MIT
#include "reportprinter.h"
#include "wounds.h"
#include "peakflow.h"
#include "edition.h"
#include "inr.h"
#include "fluid.h"
#include "contacts.h"
#include "vaccinations.h"
#include "medprep.h"
#include "apptconflicts.h"
#include <QGuiApplication>
#include "careinfo.h"
#include "database.h"
#include "formloader.h"
#include "bmi.h"
#include "medschedule.h"
#include "journalmodel.h"
#include "vitaldefs.h"
#include "xlsxwriter.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QMessageBox>
#include <QFontMetricsF>
#include <QPainter>
#include <QPdfWriter>
#ifndef VP_NO_PRINTSUPPORT
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#endif
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>
#include <functional>
#include <numeric>

namespace ReportPrinter {

static QString tr(const char *s) { return QCoreApplication::translate("ReportPrinter", s); }

QString disclaimerShort()
{
    return tr("Persönliches Gesundheitstagebuch – kein Medizinprodukt");
}

namespace {

using RenderFn = std::function<void(QPagedPaintDevice *)>;

const QMarginsF kMarginsMm(15, 12, 15, 12);

// Übliche Druckränder (in Anlehnung an DIN 5008): links 25 mm (Heftrand), sonst 20 mm
const QMarginsF kStandardMarginsMm(25, 20, 20, 20);

void configure(QPagedPaintDevice *dev, QPageLayout::Orientation o, const QMarginsF &margins = kMarginsMm)
{
    dev->setPageLayout(QPageLayout(QPageSize(QPageSize::A4), o, margins, QPageLayout::Millimeter));
}

QString exportDir()
{
    QString d = QSettings().value(QStringLiteral("paths/export")).toString();
    if (d.isEmpty() || !QDir(d).exists())
        d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return d;
}

void rememberDir(const QString &file)
{
    QSettings().setValue(QStringLiteral("paths/export"), QFileInfo(file).absolutePath());
}

QString safe(const QString &s)
{
    QString r = s;
    r.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("_"));
    return r;
}

QString suggestName(const QString &kind, const Patient &p, const QString &ext)
{
    return QDir(exportDir()).filePath(QStringLiteral("VitaParms_%1_%2_%3.%4")
        .arg(kind, safe(p.lastName + QLatin1Char('_') + p.firstName),
             QDate::currentDate().toString(Qt::ISODate), ext));
}

QString askFile(QWidget *parent, const QString &title, const QString &suggested, const QString &filter)
{
    const QString f = QFileDialog::getSaveFileName(parent, title, suggested, filter);
    if (!f.isEmpty()) rememberDir(f);
    return f;
}

void run(QWidget *parent, Target t, const QString &title, const QString &kind, const Patient &p,
         QPageLayout::Orientation o, const RenderFn &fn, const QMarginsF &margins = kMarginsMm)
{
    switch (t) {
    case Target::Pdf: {
        const QString path = askFile(parent, title + tr(" – als PDF speichern"),
                                     suggestName(kind, p, QStringLiteral("pdf")), tr("PDF-Dateien (*.pdf)"));
        if (path.isEmpty()) return;
        QPdfWriter w(path);
        w.setResolution(300);
        w.setTitle(title);
        w.setCreator(QStringLiteral("VitaParms"));
        configure(&w, o, margins);
        fn(&w);
        break;
    }
#ifndef VP_NO_PRINTSUPPORT
    case Target::Printer: {
        QPrinter printer(QPrinter::HighResolution);
        printer.setDocName(title);
        configure(&printer, o, margins);
        QPrintDialog dlg(&printer, parent);
        dlg.setWindowTitle(title + tr(" drucken"));
        if (dlg.exec() == QDialog::Accepted)
            fn(&printer);
        break;
    }
    case Target::Preview: {
        QPrinter printer(QPrinter::HighResolution);
        printer.setDocName(title);
        configure(&printer, o, margins);
        QPrintPreviewDialog dlg(&printer, parent);
        dlg.setWindowTitle(title + tr(" – Druckvorschau"));
        QObject::connect(&dlg, &QPrintPreviewDialog::paintRequested, [&](QPrinter *pr) { fn(pr); });
        dlg.resize(900, 900);
        dlg.exec();
        break;
    }
#else
    default:
        run(parent, Target::Pdf, title, kind, p, o, fn, margins);   // ohne Druckunterstützung: PDF
        break;
#endif
    }
}

qreal mm(QPaintDevice *d, qreal v) { return v / 25.4 * d->logicalDpiY(); }
qreal ptPx(QPaintDevice *d, qreal v) { return v / 72.0 * d->logicalDpiY(); }

QString birthText(const Patient &p)
{
    if (!p.birthDate.isValid()) return {};
    return tr("geb. %1").arg(QLocale().toString(p.birthDate, QStringLiteral("dd.MM.yyyy")));
}

// Kopf- und Fußzeile, liefert den Inhaltsbereich
QRectF paintFrame(QPainter &pa, QPagedPaintDevice *dev, const QString &title, const Patient &p,
                  int page, int pages)
{
    const QRectF full(QPointF(0, 0), dev->pageLayout().paintRectPixels(dev->logicalDpiX()).size());
    const QLocale loc;
    pa.save();
    QFont f = pa.font();

    // Kopf
    f.setPointSizeF(13); f.setBold(true);
    pa.setFont(f);
    pa.setPen(QColor(0x22, 0x22, 0x22));
    const qreal headH = mm(dev, 10);
    pa.drawText(QRectF(full.left(), full.top(), full.width() * 0.55, headH), Qt::AlignLeft | Qt::AlignBottom, title);
    f.setPointSizeF(10); f.setBold(true);
    pa.setFont(f);
    QString who = p.displayName();
    pa.drawText(QRectF(full.left() + full.width() * 0.45, full.top(), full.width() * 0.55, headH * 0.6),
                Qt::AlignRight | Qt::AlignBottom, who);
    f.setBold(false); f.setPointSizeF(8.5);
    pa.setFont(f);
    QString sub = birthText(p);
    const int age = p.ageAt(QDate::currentDate());
    if (age >= 0) sub += tr(" (%1 J.)").arg(age);
    pa.drawText(QRectF(full.left() + full.width() * 0.45, full.top() + headH * 0.6, full.width() * 0.55, headH * 0.4),
                Qt::AlignRight | Qt::AlignVCenter, sub);
    pa.setPen(QPen(QColor(0x88, 0x99, 0xAA), ptPx(dev, 0.8)));
    pa.drawLine(QPointF(full.left(), full.top() + headH + mm(dev, 1.5)),
                QPointF(full.right(), full.top() + headH + mm(dev, 1.5)));

    // Fuß
    f.setPointSizeF(7.5);
    pa.setFont(f);
    pa.setPen(QColor(0x66, 0x66, 0x66));
    const qreal footH = mm(dev, 6);
    const QRectF foot(full.left(), full.bottom() - footH, full.width(), footH);
    pa.setPen(QPen(QColor(0xBB, 0xBB, 0xBB), ptPx(dev, 0.5)));
    pa.drawLine(foot.topLeft(), foot.topRight());
    pa.setPen(QColor(0x66, 0x66, 0x66));
    const QString leftText = tr("VitaParms · Ausdruck vom %1").arg(loc.toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")));
    const QString rightText = tr("Seite %1 von %2").arg(page).arg(pages);
    pa.drawText(foot, Qt::AlignLeft | Qt::AlignVCenter, leftText);
    pa.drawText(foot, Qt::AlignRight | Qt::AlignVCenter, rightText);
    // Mitte: zentriert, wenn Platz ist – sonst zwischen links und rechts (ggf. gekürzt), damit nichts überlappt
    {
        const QFontMetricsF fm(pa.font(), dev);
        const qreal gap = mm(dev, 3);
        const qreal lw = fm.horizontalAdvance(leftText) + gap, rw = fm.horizontalAdvance(rightText) + gap;
        const QString mid = disclaimerShort();
        const qreal mw = fm.horizontalAdvance(mid);
        const qreal half = foot.width() / 2;
        if (mw / 2 <= half - std::max(lw, rw)) {
            pa.drawText(foot, Qt::AlignHCenter | Qt::AlignVCenter, mid);
        } else {
            const QRectF midRect(foot.left() + lw, foot.top(), std::max<qreal>(0, foot.width() - lw - rw), foot.height());
            pa.drawText(midRect, Qt::AlignHCenter | Qt::AlignVCenter, fm.elidedText(mid, Qt::ElideRight, midRect.width()));
        }
    }
    pa.restore();

    return QRectF(full.left(), full.top() + headH + mm(dev, 4), full.width(),
                  full.height() - headH - mm(dev, 4) - footH - mm(dev, 3));
}

void printHtml(QPagedPaintDevice *dev, const QString &title, const Patient &p, const QString &html,
               qreal fontPt = 9.0)
{
    QPainter pa(dev);
    // Erst den Inhaltsbereich bestimmen (Kopf/Fuß sind auf allen Seiten gleich hoch)
    const QRectF full(QPointF(0, 0), dev->pageLayout().paintRectPixels(dev->logicalDpiX()).size());
    const qreal headH = mm(dev, 10) + mm(dev, 4);
    const qreal footH = mm(dev, 6) + mm(dev, 3);
    const QRectF body(0, headH, full.width(), full.height() - headH - footH);

    QTextDocument doc;
    doc.documentLayout()->setPaintDevice(dev);
    QFont f = pa.font();
    f.setPointSizeF(fontPt);
    doc.setDefaultFont(f);
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.setPageSize(body.size());
    const int pages = doc.pageCount();

    for (int i = 0; i < pages; ++i) {
        if (i) dev->newPage();
        paintFrame(pa, dev, title, p, i + 1, pages);
        pa.save();
        pa.translate(body.left(), body.top() - i * body.height());
        const QRectF clip(0, i * body.height(), body.width(), body.height());
        pa.setClipRect(clip);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = clip;
        ctx.palette.setColor(QPalette::Text, Qt::black);
        doc.documentLayout()->draw(&pa, ctx);
        pa.restore();
    }
}

// Tabellen-HTML mit an die Druckauflösung angepasster Polsterung
QString tableOpen(QPaintDevice *dev, int border = 1)
{
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"%1\" border=\"%2\" "
                          "style=\"border-collapse: collapse; border-color: #8899aa;\">")
        .arg(qRound(ptPx(dev, 1.3))).arg(qMax(1, qRound(ptPx(dev, 0.4) * border)));
}

QString th(const QString &s, const QString &extra = {})
{
    return QStringLiteral("<th bgcolor=\"#e8eef4\" align=\"left\" %2>%1</th>").arg(s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")), extra);
}

QString td(const QString &s, const QString &align = QStringLiteral("left"))
{
    return QStringLiteral("<td align=\"%2\">%1</td>").arg(s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")), align);
}

QString journalHtml(QPaintDevice *dev, const JournalModel &m, const QString &periodText)
{
    QString h;
    h += QStringLiteral("<p><b>%1</b> %2 &nbsp;·&nbsp; %3</p>")
             .arg(tr("Zeitraum:"), periodText.toHtmlEscaped(),
                  tr("%n Messung(en), aktuellste zuerst").replace(QStringLiteral("%n"), QString::number(m.rowCount())));
    // Spaltenbreiten in Prozent – Beobachtungen erhalten den Rest
    QList<double> widths;
    double used = 0;
    for (const auto &c : m.columns()) {
        double w = 0;
        switch (c.kind) {
        case JournalModel::ColKind::DateTime:      w = 12.0; break;
        case JournalModel::ColKind::Context:       w = 12.0; break;
        case JournalModel::ColKind::BloodPressure: w = 6.0; break;
        case JournalModel::ColKind::Vital:         w = 6.0; break;
        case JournalModel::ColKind::Extra:         w = 6; break;
        case JournalModel::ColKind::Observations:  w = 0; break;
        }
        widths << w;
        used += w;
    }
    const double obs = std::max(12.0, 100.0 - used);
    h += tableOpen(dev);
    h += QStringLiteral("<thead><tr>");
    for (int i = 0; i < m.columns().size(); ++i)
        h += th(m.columns()[i].header,
                QStringLiteral("width=\"%1%\"").arg(widths[i] > 0 ? widths[i] : obs, 0, 'f', 1));
    h += QStringLiteral("</tr></thead>");
    for (int r = 0; r < m.rowCount(); ++r) {
        const VitalRecord *rec = m.recordAt(r);
        h += rec && rec->isAdmission ? QStringLiteral("<tr style=\"font-weight:bold;\">")
             : (r % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>"));
        for (int c = 0; c < m.columnCount(); ++c) {
            const auto kind = m.columns()[c].kind;
            const bool num = kind == JournalModel::ColKind::Vital || kind == JournalModel::ColKind::BloodPressure;
            h += td(m.cellText(r, c), num ? QStringLiteral("right") : QStringLiteral("left"));
        }
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    if (m.rowCount() == 0)
        h += QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine Messungen im gewählten Zeitraum."));
    return h;
}

// ---------------------------------------------------------------- Stell-Liste (Medikamente stellen)
// Eine Zeile je Medikament: zu lange Texte werden gekürzt („…“), damit keine Zeile umbricht.
QString preparationListHtml(QPaintDevice *dev, const QList<MedPrep::ListRow> &rows, const QString &subtitle, qreal fontPt)
{
    // etwas Luft für Rahmenlinien und Rundung der Spaltenbreiten, damit nichts über den rechten Rand ragt
    const qreal bodyW = 0.97 * static_cast<QPagedPaintDevice *>(dev)->pageLayout().paintRectPixels(dev->logicalDpiX()).width();
    QFont f = QGuiApplication::font();
    f.setPointSizeF(fontPt);
    const qreal pad = 2 * qRound(ptPx(dev, 1.3)) + 2 * qMax(1, qRound(ptPx(dev, 0.4))) + ptPx(dev, 3);
    struct Col { QString head; double pct; Qt::Alignment align; };
    QList<Col> cols{ { tr("Präparat"), 0, Qt::AlignLeft }, { tr("Wirkstoff"), 0, Qt::AlignLeft },
                     { tr("Stärke"), 0, Qt::AlignLeft }, { tr("Tagesgabe"), 0, Qt::AlignHCenter },
                     { tr("Rest nach Stellen"), 0, Qt::AlignRight } };
    {   // Spaltenbreiten nach dem Inhalt: Stärke, Tagesgabe und Rest bekommen ihre volle Breite,
        // Präparat und Wirkstoff teilen sich den Rest (zu Langes wird gekürzt)
        QFont bf = f;
        bf.setBold(true);
        const QFontMetricsF fmN(f, dev), fmB(bf, dev);
        QList<qreal> nat(cols.size(), 0);
        for (int c = 0; c < cols.size(); ++c) nat[c] = fmB.horizontalAdvance(cols[c].head) + pad;
        for (const MedPrep::ListRow &r : rows) {
            nat[0] = std::max(nat[0], fmB.horizontalAdvance(r.name) + pad);
            nat[1] = std::max(nat[1], fmN.horizontalAdvance(r.substance) + pad);
            nat[2] = std::max(nat[2], fmN.horizontalAdvance(r.strength) + pad);
            nat[3] = std::max(nat[3], fmN.horizontalAdvance(r.daily) + pad);
            nat[4] = std::max(nat[4], fmB.horizontalAdvance(r.rest) + pad);
        }
        for (int c = 2; c < 5; ++c) nat[c] = std::min(nat[c], bodyW * 0.22);
        const qreal total = std::accumulate(nat.begin(), nat.end(), qreal(0));
        if (total <= bodyW) {
            for (int c = 0; c < cols.size(); ++c) cols[c].pct = 100.0 * nat[c] / total;   // freien Platz anteilig verteilen
        } else {
            const qreal fixed = nat[2] + nat[3] + nat[4];
            const qreal free = std::max(bodyW * 0.4, bodyW - fixed);
            const qreal scale = (bodyW - free) / fixed;                                  // nur falls fixed zu groß
            for (int c = 2; c < 5; ++c) cols[c].pct = 100.0 * nat[c] * std::min<qreal>(1, scale) / bodyW;
            const qreal both = nat[0] + nat[1];
            const qreal rest = 100.0 - cols[2].pct - cols[3].pct - cols[4].pct;
            cols[0].pct = rest * nat[0] / both;
            cols[1].pct = rest * nat[1] / both;
        }
    }
    auto cell = [&](const QString &text, int c, bool bold = false, const QString &color = {}) {
        const qreal w = bodyW * cols[c].pct / 100.0 - pad;
        QFont cf = f;
        cf.setBold(bold);
        const QString t = QFontMetricsF(cf, dev).elidedText(text, Qt::ElideRight, w);
        const QString align = cols[c].align == Qt::AlignRight ? QStringLiteral("right")
                            : cols[c].align == Qt::AlignHCenter ? QStringLiteral("center") : QStringLiteral("left");
        QString style = QStringLiteral("white-space: nowrap;");
        if (!color.isEmpty()) style += QStringLiteral(" color: %1;").arg(color);
        return QStringLiteral("<td align=\"%1\" style=\"%2\">%3%4%5</td>")
            .arg(align, style, bold ? QStringLiteral("<b>") : QString(), t.toHtmlEscaped(), bold ? QStringLiteral("</b>") : QString());
    };
    QString h = QStringLiteral("<p><b>%1</b><br>%2</p>")
                    .arg(subtitle.toHtmlEscaped(),
                         tr("Zu stellen: %1 Medikament(e). Restbestand berechnet am %2.").arg(rows.size())
                             .arg(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm"))).toHtmlEscaped());
    if (rows.isEmpty())
        return h + QStringLiteral("<p><i>%1</i></p>").arg(tr("Für diesen Tag und Zeitpunkt ist nichts zu stellen."));
    h += tableOpen(dev) + QStringLiteral("<thead><tr>");
    for (const Col &c : cols)
        h += QStringLiteral("<th width=\"%1%\" align=\"%2\" bgcolor=\"#e8edf2\" style=\"white-space: nowrap;\">%3</th>")
                 .arg(c.pct, 0, 'f', 1)
                 .arg(c.align == Qt::AlignRight ? QStringLiteral("right") : c.align == Qt::AlignHCenter ? QStringLiteral("center") : QStringLiteral("left"),
                      c.head.toHtmlEscaped());
    h += QStringLiteral("</tr></thead>");
    int n = 0;
    bool anyShort = false, anyUntracked = false;
    for (const MedPrep::ListRow &r : rows) {
        h += n++ % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
        h += cell(r.name, 0, true) + cell(r.substance, 1) + cell(r.strength, 2) + cell(r.daily, 3)
             + cell(r.rest, 4, r.shortage, r.shortage ? QStringLiteral("#B00020") : QString());
        h += QStringLiteral("</tr>");
        anyShort = anyShort || r.shortage;
        anyUntracked = anyUntracked || !r.tracked;
    }
    h += QStringLiteral("</table>");
    QStringList notes{ tr("Tagesgabe: morgens - mittags - abends - zur Nacht.") };
    if (anyUntracked) notes << tr("— = ohne Bestandsführung.");
    if (anyShort) notes << tr("Rot: Der Bestand reicht für das Stellen nicht.");
    h += QStringLiteral("<p><small>%1</small></p>").arg(notes.join(QLatin1Char(' ')).toHtmlEscaped());
    return h;
}

// ---------------------------------------------------------------- Terminkollisionen
QString conflictsHtml(QPaintDevice *dev, const ApptConflicts::Options &o, const ApptConflicts::Result &r)
{
    QString h = QStringLiteral("<p><b>%1</b><br>%2<br>%3</p>")
                    .arg(tr("Personen: %1").arg(r.persons.join(QStringLiteral("; "))).toHtmlEscaped(),
                         ApptConflicts::optionsText(o).toHtmlEscaped(),
                         tr("%1 Termine geprüft, %2 Kollision(en).").arg(r.checked).arg(r.conflicts.size()).toHtmlEscaped());
    if (!r.error.isEmpty()) return h + QStringLiteral("<p><i>%1</i></p>").arg(r.error.toHtmlEscaped());
    if (r.conflicts.isEmpty())
        return h + QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine Überschneidungen im gewählten Zeitraum."));
    auto cellAppt = [](const Appointment &a, const QString &person, bool mark) {
        QString t = QStringLiteral("<b>%1</b><br>%2<br>%3").arg(ApptConflicts::when(a).toHtmlEscaped(),
                                                               a.title.toHtmlEscaped(), person.toHtmlEscaped());
        if (!a.location.isEmpty()) t += QStringLiteral("<br><small>%1</small>").arg(a.location.toHtmlEscaped());
        if (mark) t += QStringLiteral("<br><small><i>%1</i></small>").arg(tr("verschieben").toHtmlEscaped());
        return QStringLiteral("<td valign=\"top\">%1</td>").arg(t);
    };
    h += tableOpen(dev) + QStringLiteral("<thead><tr>")
         + th(tr("Termin 1"), QStringLiteral("width=\"27%\"")) + th(tr("Termin 2"), QStringLiteral("width=\"27%\""))
         + th(tr("Kollision"), QStringLiteral("width=\"20%\"")) + th(tr("Ausweichtermine")) + QStringLiteral("</tr></thead>");
    int n = 0;
    for (const ApptConflicts::Conflict &c : r.conflicts) {
        h += n++ % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
        h += cellAppt(c.a, c.personA, !c.moveB) + cellAppt(c.b, c.personB, c.moveB);
        h += QStringLiteral("<td valign=\"top\">%1</td>").arg(ApptConflicts::overlapText(c, o.bufferMinutes).toHtmlEscaped());
        h += QStringLiteral("<td valign=\"top\">%1<br>%2</td>")
                 .arg(tr("für %1:").arg(ApptConflicts::movedTitle(c)).toHtmlEscaped(),
                      ApptConflicts::alternativesText(c).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    h += QStringLiteral("<p><small>%1</small></p>").arg(tr("Ausweichtermine: gleiche Dauer, frei für alle gewählten Personen. "
                                                           "Ganztägige und erledigte Termine werden nicht geprüft. "
                                                           "Bitte Vorschläge mit den Beteiligten abstimmen.").toHtmlEscaped());
    return h;
}

Patient conflictsHeaderPerson(const ApptConflicts::Result &r)
{
    Patient p;
    p.lastName = r.persons.size() <= 2 ? r.persons.join(QStringLiteral(" · ")) : tr("%1 Personen").arg(r.persons.size());
    return p;
}

// ---------------------------------------------------------------- Impfpass
QString vaccinationTable(QPaintDevice *dev, const QList<Vaccinations::Vaccination> &l)
{
    const QLocale loc;
    QString h = tableOpen(dev);
    h += QStringLiteral("<thead><tr>") + th(tr("Datum"), QStringLiteral("width=\"12%\"")) + th(tr("Impfung gegen"), QStringLiteral("width=\"23%\""))
         + th(tr("Impfstoff (Präparat)"), QStringLiteral("width=\"17%\"")) + th(tr("Ch.-B."), QStringLiteral("width=\"15%\""))
         + th(tr("Arzt / Praxis"), QStringLiteral("width=\"17%\"")) + th(tr("Bemerkung")) + QStringLiteral("</tr></thead>");
    int n = 0;
    for (const auto &v : l) {
        h += n++ % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
        h += td(v.date.isValid() ? loc.toString(v.date, QStringLiteral("dd.MM.yyyy")) : QStringLiteral("—")) + td(v.against) + td(v.product)
             + QStringLiteral("<td style=\"white-space: nowrap\">%1</td>").arg(v.batch.toHtmlEscaped()) + td(v.doctor) + td(v.note) + QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    return h;
}

QString vaccinationHtml(QPaintDevice *dev, const Patient &p)
{
    const QList<Vaccinations::Vaccination> l = Vaccinations::list(p);
    QString h;
    if (l.isEmpty())
        return QStringLiteral("<p><i>%1</i></p>").arg(tr("Im Impfpass sind noch keine Impfungen eingetragen."));
    h += QStringLiteral("<p>%1</p>").arg(tr("%1 Impfung(en), neueste zuerst.").arg(l.size()));
    h += vaccinationTable(dev, l);
    if (const QStringList last = Vaccinations::latestPerAgainst(l); !last.isEmpty()) {
        QStringList esc;
        for (const QString &x : last) esc << x.toHtmlEscaped();
        h += QStringLiteral("<h3>%1</h3><p>%2</p>").arg(tr("Zuletzt geimpft"), esc.join(QStringLiteral("<br>")));
    }
    h += QStringLiteral("<p><i>%1</i></p>").arg(tr("Abschrift aus dem persönlichen Gesundheitstagebuch – ersetzt nicht den amtlichen Impfausweis."));
    return h;
}

// ---------------------------------------------------------------- Verlauf nach Wertegruppen
struct HistCol {
    QString head;
    std::function<QString(const VitalRecord &)> text;
    QList<VitalKey> stat;            // Kennwerte: ein Schlüssel, bei RR zwei (sys/dia)
};

QList<VitalKey> groupKeys(const QString &id)
{
    for (const auto &pd : VitalDefs::panels())
        if (pd.id == id) return pd.keys;
    return {};
}

QList<HistCol> historyColumns(const QString &id)
{
    QList<HistCol> c;
    auto vital = [](VitalKey k) {
        return HistCol{ VitalDefs::header(k), [k](const VitalRecord &r) { return VitalDefs::formatted(r, k); }, { k } };
    };
    if (id == QLatin1String("bp")) {
        c << HistCol{ tr("RR [mmHg]"), [](const VitalRecord &r) {
                         if (!r.systolic && !r.diastolic) return QString();
                         return QStringLiteral("%1/%2").arg(r.systolic ? QString::number(*r.systolic) : QStringLiteral("–"),
                                                            r.diastolic ? QString::number(*r.diastolic) : QStringLiteral("–"));
                     }, { VitalKey::Systolic, VitalKey::Diastolic } };
        c << vital(VitalKey::Pulse) << vital(VitalKey::PulsePressure);
    } else if (id == QLatin1String("fluid")) {
        c << vital(VitalKey::FluidIn)
          << HistCol{ tr("Art (Einfuhr)"), [](const VitalRecord &r) { return r.fluidIn ? r.fluidInKind : QString(); }, {} }
          << vital(VitalKey::FluidOut)
          << HistCol{ tr("Art (Ausfuhr)"), [](const VitalRecord &r) { return r.fluidOut ? r.fluidOutKind : QString(); }, {} };
    } else if (id == QLatin1String("edema")) {
        c << HistCol{ VitalDefs::label(VitalKey::Edema), [](const VitalRecord &r) { return r.edema ? Fluid::edemaText(r) : QString(); }, {} };
    } else if (id == QLatin1String("stool")) {
        c << HistCol{ VitalDefs::label(VitalKey::Stool), [](const VitalRecord &r) { return VitalDefs::formatted(r, VitalKey::Stool); }, {} };
    } else if (id == QLatin1String("obs")) {
        c << HistCol{ tr("Beobachtungen"), [](const VitalRecord &r) { return r.observations; }, {} };
    } else {
        for (VitalKey k : groupKeys(id)) c << vital(k);
    }
    return c;
}

QString historyHtml(QPaintDevice *dev, QList<VitalRecord> recs, const QStringList &groups, const QString &periodText)
{
    std::sort(recs.begin(), recs.end(), [](const VitalRecord &a, const VitalRecord &b) { return a.measuredAt < b.measuredAt; });
    const QLocale loc;
    QStringList titles;
    for (const auto &g : historyGroups()) if (groups.contains(g.id)) titles << g.title;
    QString h = QStringLiteral("<p><b>%1</b> %2<br><b>%3</b> %4<br><i>%5</i></p>")
                    .arg(tr("Zeitraum:"), periodText.toHtmlEscaped(), tr("Werte:"),
                         titles.isEmpty() ? tr("keine gewählt") : titles.join(QStringLiteral(", ")).toHtmlEscaped(),
                         tr("Je Wertegruppe chronologisch – älteste Messung zuerst."));
    for (const auto &g : historyGroups()) {
        if (!groups.contains(g.id)) continue;
        const QList<HistCol> cols = historyColumns(g.id);
        QList<const VitalRecord *> rows;
        for (const VitalRecord &r : recs) {
            bool any = false;
            for (const HistCol &c : cols) if (!c.text(r).trimmed().isEmpty() && !c.head.startsWith(tr("Art"))) { any = true; break; }
            if (any) rows << &r;
        }
        h += QStringLiteral("<h3 style=\"margin-top:14px;\">%1</h3>").arg(g.title.toHtmlEscaped());
        if (rows.isEmpty()) {
            h += QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine Werte im gewählten Zeitraum."));
            continue;
        }
        const bool wide = g.id == QLatin1String("obs") || g.id == QLatin1String("edema") || g.id == QLatin1String("fluid");
        h += tableOpen(dev);
        h += QStringLiteral("<thead><tr>") + th(tr("Datum / Uhrzeit"), QStringLiteral("width=\"23%\""))
             + th(tr("Messzeitpunkt"), QStringLiteral("width=\"17%\""));
        for (const HistCol &c : cols)
            h += th(c.head, wide ? QString() : QStringLiteral("width=\"%1%\"").arg(int(60 / cols.size())));
        h += QStringLiteral("</tr></thead>");
        int n = 0;
        for (const VitalRecord *r : rows) {
            h += n++ % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
            h += td(loc.toString(r->measuredAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")));
            h += td(r->isAdmission ? tr("Aufnahme") : r->context);
            for (const HistCol &c : cols) h += td(c.text(*r), c.stat.isEmpty() ? QStringLiteral("left") : QStringLiteral("right"));
            h += QStringLiteral("</tr>");
        }
        // Kennwerte je Spalte: Anzahl, Mittelwert, Minimum, Maximum
        bool anyStat = false;
        for (const HistCol &c : cols) if (!c.stat.isEmpty()) anyStat = true;
        if (anyStat) {
            struct Agg { int n = 0; double sum = 0, lo = 0, hi = 0; };
            auto agg = [&](VitalKey k) {
                Agg a;
                for (const VitalRecord *r : rows)
                    if (const auto v = VitalDefs::value(*r, k)) {
                        if (!a.n) { a.lo = a.hi = *v; }
                        a.lo = std::min(a.lo, *v); a.hi = std::max(a.hi, *v); a.sum += *v; ++a.n;
                    }
                return a;
            };
            auto num = [&](double v, VitalKey k) { return loc.toString(v, 'f', VitalDefs::decimals(k)); };
            const QStringList labels = { tr("Anzahl"), tr("Mittelwert"), tr("Minimum"), tr("Maximum") };
            for (int line = 0; line < labels.size(); ++line) {
                h += QStringLiteral("<tr bgcolor=\"#e8eef4\">") + QStringLiteral("<td colspan=\"2\"><b>%1</b></td>").arg(labels[line]);
                for (const HistCol &c : cols) {
                    QStringList parts;
                    for (VitalKey k : c.stat) {
                        const Agg a = agg(k);
                        if (!a.n) { parts << QStringLiteral("–"); continue; }
                        const double mean = a.sum / a.n;
                        parts << (line == 0 ? QString::number(a.n)
                                  : line == 1 ? loc.toString(mean, 'f', std::max(1, VitalDefs::decimals(k)))
                                  : line == 2 ? num(a.lo, k) : num(a.hi, k));
                    }
                    h += td(parts.join(QLatin1Char('/')), QStringLiteral("right"));
                }
                h += QStringLiteral("</tr>");
            }
        }
        h += QStringLiteral("</table>");
    }
    return h;
}

// Eine Seite Verlaufsgrafik: Kopf, Diagramm(e), Kennwerte
void renderChartPage(QPainter &pa, QPagedPaintDevice *dev, const Patient &p, const ChartSpec &spec,
                     int page, int pages)
{
    const QRectF body = paintFrame(pa, dev, tr("Verlaufsgrafik"), p, page, pages);
    const QLocale loc;

    QFont f = pa.font();
    f.setPointSizeF(10);
    pa.setFont(f);
    pa.setPen(Qt::black);
    const qreal lineH = QFontMetricsF(f, dev).height();
    int count = 0;
    for (const VitalRecord &r : spec.records)
        if (r.measuredAt >= spec.from && r.measuredAt < spec.to) ++count;
    const QString scaleName = spec.scale == ChartSpec::Scale::Custom ? tr("Verlauf") : spec.scaleName();
    pa.drawText(QRectF(body.left(), body.top(), body.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter,
                tr("%1 · %2 · %3 Messung(en)").arg(scaleName, spec.periodText()).arg(count));

    // Kennwerte-Tabelle unten
    const QList<SeriesStats> stats = ChartRenderer::statistics(spec);
    QFont sf = f;
    sf.setPointSizeF(8.5);
    const qreal sH = QFontMetricsF(sf, dev).height() * 1.35;
    const qreal statsH = sH * (stats.size() + 1) + mm(dev, 4);

    const QRectF chartRect(body.left(), body.top() + lineH + mm(dev, 3), body.width(),
                           body.height() - lineH - mm(dev, 3) - statsH);
    ChartRenderer::paint(pa, chartRect, spec);

    qreal y = chartRect.bottom() + mm(dev, 4);
    const QList<qreal> colW = { 0.34, 0.12, 0.14, 0.14, 0.14, 0.12 };
    const QStringList heads = { tr("Parameter"), tr("Anzahl"), tr("Minimum"), tr("Maximum"), tr("Mittelwert"), tr("Einheit") };
    auto drawRow = [&](const QStringList &cells, bool head, const QColor &swatch) {
        qreal x = body.left();
        if (head) pa.fillRect(QRectF(body.left(), y, body.width(), sH), QColor(0xE8, 0xEE, 0xF4));
        QFont rf = sf;
        rf.setBold(head);
        pa.setFont(rf);
        for (int i = 0; i < cells.size(); ++i) {
            const qreal w = body.width() * colW[i];
            QRectF cell(x + mm(dev, 1.5), y, w - mm(dev, 3), sH);
            if (i == 0 && swatch.isValid()) {
                pa.fillRect(QRectF(cell.left(), y + sH * 0.3, sH * 0.4, sH * 0.4), swatch);
                cell.setLeft(cell.left() + sH * 0.6);
            }
            pa.setPen(Qt::black);
            pa.drawText(cell, (i == 0 || i == 5 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter, cells[i]);
            x += w;
        }
        pa.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), ptPx(dev, 0.4)));
        pa.drawLine(QPointF(body.left(), y + sH), QPointF(body.right(), y + sH));
        y += sH;
    };
    drawRow(heads, true, QColor());
    for (const SeriesStats &s : stats) {
        const int d = VitalDefs::decimals(s.key);
        const bool r = s.hasRange();
        const int md = s.key == VitalKey::Stool ? 1 : d;
        drawRow({ s.label(),
                  QString::number(s.count),
                  r ? loc.toString(s.min, 'f', d) : QStringLiteral("–"),
                  r ? loc.toString(s.max, 'f', d) : QStringLiteral("–"),
                  r ? loc.toString(s.mean, 'f', md) : QStringLiteral("–"),
                  VitalDefs::unit(s.key) },
                false, VitalDefs::color(s.key));
    }
}

void renderCharts(QPagedPaintDevice *dev, const Patient &p, const ChartSpec &spec0)
{
    ChartSpec spec = spec0;              // Zielbereiche aus dem Stammblatt
    if (spec.inrHigh <= 0) spec.setPatient(p);
    QPainter pa(dev);
    const QStringList ids = spec.visiblePanelIds();
    if (!spec.separatePages || ids.size() < 2) {
        renderChartPage(pa, dev, p, spec, 1, 1);
        return;
    }
    // Jeder Bereich auf einer eigenen Seite – mit seinen eigenen Kennwerten
    for (int i = 0; i < ids.size(); ++i) {
        if (i) dev->newPage();
        ChartSpec one = spec;
        one.panelIds = QStringList{ ids[i] };
        renderChartPage(pa, dev, p, one, i + 1, int(ids.size()));
    }
}

QString medicationHtml(QPaintDevice *dev, const Patient &p, const QList<Medication> &meds)
{
    const QLocale loc;
    QString h;
    QStringList info;
    if (p.birthDate.isValid()) info << birthText(p);
    if (!p.sex.isEmpty()) info << (p.sex == QLatin1String("m") ? tr("männlich") : p.sex == QLatin1String("w") ? tr("weiblich") : tr("divers"));
    h += QStringLiteral("<p>%1 &nbsp;·&nbsp; %2</p>")
             .arg(info.join(QStringLiteral(", ")).toHtmlEscaped(),
                  tr("Stand: %1").arg(loc.toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy"))));
    const QString allergies = p.extra.value(QStringLiteral("Allergien")).toString().trimmed();
    if (!allergies.isEmpty())
        h += QStringLiteral("<p><b>%1</b> %2</p>").arg(tr("Allergien/Unverträglichkeiten:"),
                                                     allergies.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("; ")));
    // Abschnitte wie im Medikationsplan: Regel-, Bedarfs-, Notfallmedikation, Sonstiges
    for (const auto &[cat, list] : MedSchedule::grouped(meds)) {
        if (list.isEmpty()) continue;
        const QString color = cat == Medication::Emergency ? QStringLiteral("#D52B1E")
                            : cat == Medication::AsNeeded ? QStringLiteral("#9a6a00")
                            : cat == Medication::Other ? QStringLiteral("#555555") : QStringLiteral("#1f3a56");
        h += QStringLiteral("<h4 style=\"color:%1; margin-top:8px; margin-bottom:2px;\">%2</h4>")
                 .arg(color, MedSchedule::categoryName(cat).toHtmlEscaped());
        h += tableOpen(dev);
        const bool regular = cat == Medication::Regular;
        h += QStringLiteral("<thead><tr>") + th(tr("Wirkstoff")) + th(tr("Handelsname")) + th(tr("Stärke"))
           + th(tr("Form")) + th(tr("mo"), QStringLiteral("align=\"center\"")) + th(tr("mi"), QStringLiteral("align=\"center\""))
           + th(tr("ab"), QStringLiteral("align=\"center\"")) + th(tr("na"), QStringLiteral("align=\"center\""))
           + th(tr("Einheit"))
           + th(regular ? tr("Einnahme / Bemerkung") : cat == Medication::Other ? tr("Bemerkung") : tr("Anlass / Bemerkung"))
           + th(tr("Hinweise")) + th(tr("Grund")) + QStringLiteral("</tr></thead>");
        for (int i = 0; i < list.size(); ++i) {
            const Medication &m = list[i];
            auto dose = [regular](const QString &s) {
                return s.trimmed().isEmpty() ? (regular ? QStringLiteral("0") : QString()) : s;
            };
            h += i % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
            h += td(m.substance) + td(m.name) + td(m.strength) + td(m.form)
               + td(dose(m.morning), QStringLiteral("center")) + td(dose(m.noon), QStringLiteral("center"))
               + td(dose(m.evening), QStringLiteral("center")) + td(dose(m.night), QStringLiteral("center"))
               + td(m.unit);
            if (!regular)
                h += td(m.rhythmNote);
            else if (MedSchedule::isDaily(m))
                h += td(m.rhythmNote.isEmpty() ? MedSchedule::describe(m) : m.rhythmNote);
            else
                h += QStringLiteral("<td><b>%1</b>%2</td>").arg(MedSchedule::describe(m).toHtmlEscaped(),
                         m.rhythmNote.isEmpty() ? QString() : QStringLiteral("<br>") + m.rhythmNote.toHtmlEscaped());
            h += td(m.notes) + td(m.reason) + QStringLiteral("</tr>");
        }
        h += QStringLiteral("</table>");
    }
    if (meds.isEmpty())
        h += QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine aktuellen Medikamente eingetragen."));
    h += QStringLiteral("<p><small>%1</small></p>")
             .arg(tr("Dieser Plan wurde vom Anwender selbst geführt und ersetzt nicht den "
                     "ärztlich ausgestellten (bundeseinheitlichen) Medikationsplan."));
    return h;
}

void writeMeasurements(XlsxWriter &x, int sh, int row, const QList<VitalRecord> &records,
                       const QStringList &extraKeys)
{
    QStringList heads = { tr("Datum/Uhrzeit"), tr("Messzeitpunkt"), tr("Aufnahme") };
    QList<VitalKey> keys = { VitalKey::Systolic, VitalKey::Diastolic, VitalKey::Pulse, VitalKey::PulsePressure, VitalKey::Temperature,
                             VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose, VitalKey::Weight, VitalKey::Height, VitalKey::Bmi };
    if (Edition::light()) keys.removeAll(VitalKey::PeakFlow);
    for (VitalKey k : { VitalKey::Pain, VitalKey::Inr, VitalKey::FluidIn, VitalKey::FluidOut, VitalKey::Edema, VitalKey::Stool })
        if ((k == VitalKey::Pain || !Edition::light()) && std::any_of(records.cbegin(), records.cend(), [k](const VitalRecord &r) { return VitalDefs::value(r, k).has_value(); }))
            keys << k;
    for (VitalKey k : keys)
        heads << VitalDefs::header(k);
    heads << FormIO::fieldLabels(extraKeys);
    heads << tr("Beobachtungen");
    for (int c = 0; c < heads.size(); ++c)
        x.setCell(sh, row, c, heads[c], XlsxWriter::Header);
    x.setColumnWidth(sh, 0, 17);
    x.setColumnWidth(sh, 1, 20);
    x.setColumnWidth(sh, 2, 9);
    for (int c = 3; c < heads.size() - 1; ++c) x.setColumnWidth(sh, c, 11);
    x.setColumnWidth(sh, heads.size() - 1, 50);

    ++row;
    for (const VitalRecord &r : records) {
        int c = 0;
        x.setCell(sh, row, c++, r.measuredAt, XlsxWriter::DateTime);
        x.setCell(sh, row, c++, r.context);
        x.setCell(sh, row, c++, r.isAdmission ? QVariant(tr("ja")) : QVariant());
        for (VitalKey k : keys) {
            if (k == VitalKey::Stool || k == VitalKey::Edema) {       // Text: "Typ 4"/"ja", "Grad 2"/"keine"
                const QString t = VitalDefs::formatted(r, k);
                x.setCell(sh, row, c++, t.isEmpty() ? QVariant() : QVariant(t));
                continue;
            }
            const auto v = VitalDefs::value(r, k);
            const int d = VitalDefs::decimals(k);
            x.setCell(sh, row, c++, v ? QVariant(d == 0 ? double(qRound(*v)) : *v) : QVariant(),
                      d == 0 ? XlsxWriter::Integer : d == 1 ? XlsxWriter::Decimal1 : XlsxWriter::Decimal2);
        }
        for (const QString &e : extraKeys) {
            const QJsonValue v = r.extra.value(e);
            x.setCell(sh, row, c++, v.isDouble() ? QVariant(v.toDouble()) : v.isUndefined() || v.isNull()
                                                       ? QVariant() : QVariant(JournalModel::extraText(v)));
        }
        x.setCell(sh, row, c++, r.observations, XlsxWriter::Wrap);
        ++row;
    }
}

void writeTitle(XlsxWriter &x, int sh, const QString &title, const Patient &p, const QString &period)
{
    x.setCell(sh, 0, 0, title, XlsxWriter::Title);
    QString who = p.displayName();
    if (p.birthDate.isValid()) who += QStringLiteral(", ") + birthText(p);
    x.setCell(sh, 1, 0, who);
    x.setCell(sh, 2, 0, tr("Zeitraum: %1").arg(period));
}

} // namespace

// ================================================================ öffentlich

void journal(QWidget *parent, const Patient &p, const JournalModel &model,
             const QString &periodText, Target t)
{
    run(parent, t, tr("Verlauf"), QStringLiteral("Verlauf"), p, QPageLayout::Landscape,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Verlauf der Vitalwerte"), p, journalHtml(dev, model, periodText), 7.5); });
}

void preparationList(QWidget *parent, const Patient &p, const QList<Medication> &meds, const QDate &day, int days,
                     int slot, Target t)
{
    const QList<MedPrep::ListRow> rows = MedPrep::listRows(MedPrep::select(meds, day, days, slot).list, day, days, slot);
    const QString sub = MedPrep::listSubtitle(day, days, slot);
    run(parent, t, tr("Stell-Liste"), QStringLiteral("Stellliste"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Medikamente stellen – Liste"), p, preparationListHtml(dev, rows, sub, 9), 9); },
        kStandardMarginsMm);
}

bool preparationListToPdf(const QString &path, const Patient &p, const QList<Medication> &meds, const QDate &day,
                          int days, int slot)
{
    const QList<MedPrep::ListRow> rows = MedPrep::listRows(MedPrep::select(meds, day, days, slot).list, day, days, slot);
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    w.setTitle(tr("Medikamente stellen – Liste"));
    configure(&w, QPageLayout::Portrait, kStandardMarginsMm);
    printHtml(&w, tr("Medikamente stellen – Liste"), p, preparationListHtml(&w, rows, MedPrep::listSubtitle(day, days, slot), 9), 9);
    return true;
}

void appointmentConflicts(QWidget *parent, const ApptConflicts::Options &o, const ApptConflicts::Result &r, Target t)
{
    const Patient hp = conflictsHeaderPerson(r);
    run(parent, t, tr("Terminkollisionen"), QStringLiteral("Terminkollisionen"), hp, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Terminkollisionen"), hp, conflictsHtml(dev, o, r), 9); },
        kStandardMarginsMm);
}

bool appointmentConflictsToPdf(const QString &path, const ApptConflicts::Options &o, const ApptConflicts::Result &r)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    w.setTitle(tr("Terminkollisionen"));
    configure(&w, QPageLayout::Portrait, kStandardMarginsMm);
    printHtml(&w, tr("Terminkollisionen"), conflictsHeaderPerson(r), conflictsHtml(&w, o, r), 9);
    return true;
}

void vaccinationRecord(QWidget *parent, const Patient &p, Target t)
{
    run(parent, t, tr("Impfpass"), QStringLiteral("Impfpass"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Impfpass"), p, vaccinationHtml(dev, p), 9); });
}

bool vaccinationRecordToPdf(const QString &path, const Patient &p)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Impfpass"), p, vaccinationHtml(&w, p), 9);
    return true;
}

QList<HistoryGroup> historyGroups()
{
    QList<HistoryGroup> l;
    for (const auto &pd : VitalDefs::panels()) l << HistoryGroup{ pd.id, pd.title };
    l << HistoryGroup{ QStringLiteral("obs"), tr("Beobachtungen") };
    return l;
}

QStringList historyGroupsWithData(const QList<VitalRecord> &records)
{
    QStringList l;
    for (const auto &g : historyGroups()) {
        const QList<HistCol> cols = historyColumns(g.id);
        const bool has = std::any_of(records.cbegin(), records.cend(), [&](const VitalRecord &r) {
            return std::any_of(cols.cbegin(), cols.cend(), [&](const HistCol &c) { return !c.text(r).trimmed().isEmpty(); });
        });
        if (has) l << g.id;
    }
    return l;
}

void history(QWidget *parent, const Patient &p, const QList<VitalRecord> &records, const QStringList &groups,
             const QString &periodText, Target t)
{
    run(parent, t, tr("Verlauf"), QStringLiteral("Verlauf"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Verlauf der Vitalwerte"), p, historyHtml(dev, records, groups, periodText), 9); });
}

bool historyToPdf(const QString &path, const Patient &p, const QList<VitalRecord> &records,
                  const QStringList &groups, const QString &periodText)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Verlauf der Vitalwerte"), p, historyHtml(&w, records, groups, periodText), 9);
    return true;
}

bool journalToPdf(const QString &path, const Patient &p, const JournalModel &model, const QString &periodText)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Landscape);
    printHtml(&w, tr("Verlauf der Vitalwerte"), p, journalHtml(&w, model, periodText), 7.5);
    return true;
}

bool journalToXlsx(const QString &path, const Patient &p, const JournalModel &model,
                   const QString &periodText, QString *error)
{
    XlsxWriter x;
    x.setTitle(tr("VitaParms – Verlauf %1").arg(p.displayName()));
    const int sh = x.addSheet(tr("Verlauf"));
    x.setLandscape(sh, true);
    writeTitle(x, sh, tr("VitaParms – Verlauf der Vitalwerte"), p, periodText);
    writeMeasurements(x, sh, 4, model.records(), model.extraKeys());
    x.freezeRows(sh, 5);
    return x.save(path, error);
}

bool journalExcel(QWidget *parent, const Patient &p, const JournalModel &model, const QString &periodText)
{
    const QString path = askFile(parent, tr("Verlauf nach Excel exportieren"),
                                 suggestName(QStringLiteral("Verlauf"), p, QStringLiteral("xlsx")),
                                 tr("Excel-Arbeitsmappe (*.xlsx)"));
    if (path.isEmpty()) return false;
    QString err;
    if (!journalToXlsx(path, p, model, periodText, &err)) {
        QMessageBox::critical(parent, tr("Export"), tr("Export fehlgeschlagen:\n%1").arg(err));
        return false;
    }
    return true;
}

QString askExportFile(QWidget *parent, const QString &caption, const QString &base, const Patient &p,
                      const QString &ext, const QString &filter)
{
    return askFile(parent, caption, suggestName(base, p, ext), filter);
}

void charts(QWidget *parent, const Patient &p, const ChartSpec &spec, Target t)
{
    run(parent, t, tr("Verlaufsgrafik"), QStringLiteral("Grafik"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { renderCharts(dev, p, spec); });
}

bool chartsToPdf(const QString &path, const Patient &p, const ChartSpec &spec)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Portrait);
    renderCharts(&w, p, spec);
    return true;
}

bool chartsToXlsx(const QString &path, const Patient &p, const ChartSpec &spec0, QString *error)
{
    ChartSpec spec = spec0;
    if (spec.inrHigh <= 0) spec.setPatient(p);
    XlsxWriter x;
    x.setTitle(tr("VitaParms – Verlaufsgrafik %1").arg(p.displayName()));
    const QLocale loc;

    // Blatt 1: Grafik (DIN-A4-Seitenverhältnis)
    const int g = x.addSheet(tr("Grafik"));
    writeTitle(x, g, tr("VitaParms – Verlaufsgrafik"), p, spec.periodText());
    const qreal dpi = 150;
    const QSize imgSize(qRound(180 / 25.4 * dpi), qRound(230 / 25.4 * dpi));   // 18 × 23 cm
    x.setImage(g, ChartRenderer::renderImage(spec, imgSize, dpi), 4, 0, dpi);

    // Blatt 2: Kennwerte
    const int k = x.addSheet(tr("Kennwerte"));
    writeTitle(x, k, tr("Kennwerte im Zeitraum"), p, spec.periodText());
    const QStringList heads = { tr("Parameter"), tr("Einheit"), tr("Anzahl"), tr("Minimum"), tr("Maximum"), tr("Mittelwert") };
    for (int c = 0; c < heads.size(); ++c) x.setCell(k, 4, c, heads[c], XlsxWriter::Header);
    int row = 5;
    for (const SeriesStats &s : ChartRenderer::statistics(spec)) {
        const auto st = VitalDefs::decimals(s.key) == 0 ? XlsxWriter::Integer : XlsxWriter::Decimal1;
        x.setCell(k, row, 0, s.label());
        x.setCell(k, row, 1, VitalDefs::unit(s.key));
        x.setCell(k, row, 2, s.count);
        if (s.hasRange()) {
            x.setCell(k, row, 3, s.min, st);
            x.setCell(k, row, 4, s.max, st);
            x.setCell(k, row, 5, s.mean, XlsxWriter::Decimal1);
        }
        ++row;
    }
    x.setColumnWidth(k, 0, 30);
    for (int c = 1; c < 6; ++c) x.setColumnWidth(k, c, 12);

    // Blatt 3: Messwerte des Zeitraums (Rohdaten der Grafik)
    QList<VitalRecord> recs;
    QStringList extraKeys;
    for (const VitalRecord &r : spec.records) {
        if (r.measuredAt < spec.from || r.measuredAt >= spec.to) continue;
        recs << r;
        for (auto it = r.extra.begin(); it != r.extra.end(); ++it)
            if (!extraKeys.contains(it.key())) extraKeys << it.key();
    }
    std::sort(recs.begin(), recs.end(), [](const VitalRecord &a, const VitalRecord &b) { return a.measuredAt > b.measuredAt; });
    extraKeys.sort(Qt::CaseInsensitive);
    const int m = x.addSheet(tr("Messwerte"));
    x.setLandscape(m, true);
    writeTitle(x, m, tr("Messwerte im Zeitraum"), p, spec.periodText());
    writeMeasurements(x, m, 4, recs, extraKeys);
    x.freezeRows(m, 5);
    Q_UNUSED(loc)
    return x.save(path, error);
}

bool chartsExcel(QWidget *parent, const Patient &p, const ChartSpec &spec)
{
    const QString path = askFile(parent, tr("Grafik nach Excel exportieren"),
                                 suggestName(QStringLiteral("Grafik"), p, QStringLiteral("xlsx")),
                                 tr("Excel-Arbeitsmappe (*.xlsx)"));
    if (path.isEmpty()) return false;
    QString err;
    if (!chartsToXlsx(path, p, spec, &err)) {
        QMessageBox::critical(parent, tr("Export"), tr("Export fehlgeschlagen:\n%1").arg(err));
        return false;
    }
    return true;
}

void medicationPlan(QWidget *parent, const Patient &p, const QList<Medication> &meds, Target t)
{
    run(parent, t, tr("Medikamentenplan"), QStringLiteral("Medikamentenplan"), p, QPageLayout::Landscape,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Medikamentenplan"), p, medicationHtml(dev, p, meds)); });
}

bool medicationPlanToPdf(const QString &path, const Patient &p, const QList<Medication> &meds)
{
    QPdfWriter w(path);
    w.setResolution(300);
    configure(&w, QPageLayout::Landscape);
    printHtml(&w, tr("Medikamentenplan"), p, medicationHtml(&w, p, meds));
    return true;
}

QString slotText(int slot)
{
    switch (slot) {
    case 0:  return QCoreApplication::translate("MedicationAssistant", "morgens");
    case 1:  return QCoreApplication::translate("MedicationAssistant", "mittags");
    case 2:  return QCoreApplication::translate("MedicationAssistant", "abends");
    case 3:  return QCoreApplication::translate("MedicationAssistant", "zur Nacht");
    default: return QCoreApplication::translate("MedicationAssistant", "alle Zeitpunkte");
    }
}

void htmlReport(QWidget *parent, const Patient &p, const QString &title, const QString &html, Target t, bool landscape)
{
    run(parent, t, title, title, p, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, title, p, html, 8.5); });
}

bool htmlReportToPdf(const QString &path, const Patient &p, const QString &title, const QString &html, bool landscape)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setTitle(title);
    configure(&w, landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    printHtml(&w, title, p, html, 8.5);
    return true;
}

QString preparationForText(const MedPreparation &e)
{
    const QDate d = e.forDate.isValid() ? e.forDate : e.preparedAt.date();
    if (e.days > 1)
        return tr("Woche %1–%2").arg(QLocale().toString(d, QStringLiteral("ddd dd.MM.")),
                                     QLocale().toString(d.addDays(e.days - 1), QStringLiteral("ddd dd.MM.")))
               + QStringLiteral(" · ") + slotText(e.slot);
    return QLocale().toString(d, QStringLiteral("ddd dd.MM.")) + QStringLiteral(" · ") + slotText(e.slot);
}

QString preparationLogHtml(QPaintDevice *dev, const QList<MedPreparation> &log, const QString &periodText)
{
    const QLocale loc;
    QString h = QStringLiteral("<p><b>%1</b> %2 &nbsp;·&nbsp; %3</p>")
                    .arg(tr("Zeitraum:"), periodText.toHtmlEscaped(), tr("%1 Eintrag/Einträge, neueste zuerst").arg(log.size()));
    h += tableOpen(dev);
    h += QStringLiteral("<tr>") + th(tr("Gestellt am"), QStringLiteral("width=\"13%\"")) + th(tr("Für"), QStringLiteral("width=\"13%\""))
       + th(tr("Gestellt von"), QStringLiteral("width=\"10%\"")) + th(tr("Status"), QStringLiteral("width=\"15%\""))
       + th(tr("Medikamente")) + th(tr("Bemerkung"), QStringLiteral("width=\"15%\"")) + QStringLiteral("</tr>");
    for (const MedPreparation &e : log) {
        QStringList items;
        for (const QJsonValue &v : e.items) {
            const QJsonObject o = v.toObject();
            QString line = o.value(QStringLiteral("name")).toString();
            const QString st = o.value(QStringLiteral("strength")).toString();
            if (!st.isEmpty()) line += QStringLiteral(" ") + st;
            const QString dose = o.value(QStringLiteral("dose")).toString();
            if (!dose.isEmpty()) line += QStringLiteral(" – ") + dose;
            if (!o.value(QStringLiteral("confirmed")).toBool(true)) line += tr(" (nicht bestätigt)");
            items << line;
        }
        const QString status = e.complete ? tr("vollständig")
                                          : tr("abgebrochen\n(%1 von %2)").arg(e.confirmed).arg(e.total);
        h += QStringLiteral("<tr>") + td(loc.toString(e.preparedAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")))
           + td(preparationForText(e)) + td(e.preparedBy) + td(status) + td(items.join(QLatin1Char('\n')))
           + td(e.note) + QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    return h;
}

void preparationLog(QWidget *parent, const Patient &p, const QList<MedPreparation> &log,
                    const QString &periodText, Target t)
{
    run(parent, t, tr("Protokoll Medikamente stellen"), QStringLiteral("Stellprotokoll"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) {
            printHtml(dev, tr("Protokoll Medikamente stellen"), p, preparationLogHtml(dev, log, periodText), 8.5);
        });
}

bool preparationLogToPdf(const QString &path, const Patient &p, const QList<MedPreparation> &log,
                         const QString &periodText)
{
    QPdfWriter w(path);
    w.setResolution(300);
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Protokoll Medikamente stellen"), p, preparationLogHtml(&w, log, periodText), 8.5);
    return true;
}

QString patientSheetHtml(QPagedPaintDevice *dev, const Patient &p, const QList<Medication> &meds,
                         const std::optional<VitalRecord> &admission)
{
            const QLocale loc;
            QString h;
            h += QStringLiteral("<h3>%1</h3>").arg(tr("Persönliche Daten"));
            h += tableOpen(dev, 0);
            auto row = [&](const QString &k, const QString &v) {
                if (v.trimmed().isEmpty()) return;
                h += QStringLiteral("<tr><td width=\"28%\"><b>%1</b></td><td>%2</td></tr>")
                         .arg(k.toHtmlEscaped(), v.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
            };
            row(tr("Name"), p.lastName);
            row(tr("Vorname"), p.firstName);
            row(tr("Geburtsdatum"), p.birthDate.isValid() ? loc.toString(p.birthDate, QStringLiteral("dd.MM.yyyy")) : QString());
            row(tr("Geschlecht"), p.sex == QLatin1String("m") ? tr("männlich") : p.sex == QLatin1String("w") ? tr("weiblich")
                                  : p.sex == QLatin1String("d") ? tr("divers") : QString());
            row(tr("Anschrift"), QStringList({ p.street, (p.zip + QLatin1Char(' ') + p.city).trimmed() })
                                     .filter(QRegularExpression(QStringLiteral("\\S"))).join(QStringLiteral(", ")));
            row(tr("Telefon"), p.phone);
            row(tr("Mobiltelefon"), p.mobile);
            for (auto it = p.extra.begin(); it != p.extra.end(); ++it)
                if (!CareInfo::isInternalExtraKey(it.key()))
                    row(FormIO::fieldLabel(it.key()), JournalModel::extraText(it.value()));
            if (PeakFlow::bioSexDiffers(p))
                row(tr("Biologisches Geschlecht"), PeakFlow::bioSexSetting(p) == QLatin1Char('m') ? tr("männlich") : tr("weiblich"));
            row(tr("Gliedmaßen"), p.amputations.any() ? Bmi::describe(p.amputations) : QString());
            {   // Peak Flow: persönlicher Bestwert und Sollwert (Normwerttabelle)
                QStringList pf;
                if (const int best = PeakFlow::personalBest(p)) pf << tr("persönlicher Bestwert %1 l/min").arg(best);
                const PeakFlow::Reference norm = PeakFlow::normReference(p, QDate::currentDate(), Database::instance().latestHeight(p.id));
                if (norm.value) pf << tr("Sollwert %1 l/min (%2)").arg(norm.value).arg(norm.basis);
                row(tr("Peak Flow"), pf.join(QStringLiteral("; ")));
            }
            if (const Inr::Range r = Inr::range(p); r.personal)
                row(tr("INR-Zielbereich"), QStringLiteral("%1–%2").arg(QLocale().toString(r.low, 'f', 1), QLocale().toString(r.high, 'f', 1)));
            {
                QStringList fl;
                if (const QString t = Fluid::limitsText(p); !t.isEmpty()) fl << t;
                if (const double w = Fluid::weightWatch(p); w > 0)
                    fl << tr("Gewichtszunahme melden ab %1 kg in %2 Tagen").arg(QLocale().toString(w, 'f', 1)).arg(Fluid::kWeightDays);
                row(tr("Trinkmenge/Gewicht"), fl.join(QStringLiteral("; ")));
            }
            row(tr("Pflegegrad"), CareInfo::careLevelText(p));
            row(tr("Schwerbehindertenausweis"), CareInfo::disabilityText(p));
            for (int k = 0; k < CareInfo::AdvanceCount; ++k)
                if (const QString l = CareInfo::advanceDetails(p, k); !l.isEmpty())
                    row(CareInfo::advanceName(k), l);
            h += QStringLiteral("</table>");
            if (const QList<Contacts::Contact> cl = Contacts::list(p); !cl.isEmpty()) {
                h += QStringLiteral("<h3>%1</h3>").arg(tr("Kontakte"));
                h += tableOpen(dev);
                h += QStringLiteral("<tr>") + th(tr("Art")) + th(tr("Name")) + th(tr("Anschrift")) + th(tr("Telefon / Mobil"))
                   + th(tr("E-Mail / Website")) + QStringLiteral("</tr>");
                for (const Contacts::Contact &c : cl) {
                    QStringList name{ c.title() };
                    if (!c.person.isEmpty()) name << c.person;
                    if (!c.insuranceNo.isEmpty()) name << tr("Vers.-Nr. %1").arg(c.insuranceNo);
                    if (!c.note.isEmpty()) name << c.note;
                    h += QStringLiteral("<tr>") + td(c.kindName()) + td(name.join(QLatin1Char('\n'))) + td(c.address())
                       + td(QStringList{ c.phone, c.mobile }.filter(QRegularExpression(QStringLiteral("\\S"))).join(QLatin1Char('\n')))
                       + td(QStringList{ c.email, c.website }.filter(QRegularExpression(QStringLiteral("\\S"))).join(QLatin1Char('\n')))
                       + QStringLiteral("</tr>");
                }
                h += QStringLiteral("</table>");
            }
            if (const QList<Vaccinations::Vaccination> vl = Vaccinations::list(p); !vl.isEmpty()) {
                h += QStringLiteral("<h3>%1</h3>").arg(tr("Impfpass"));
                h += vaccinationTable(dev, vl);
            }
            if (!p.diagnoses.trimmed().isEmpty())
                h += QStringLiteral("<h3>%1</h3><p>%2</p>").arg(tr("Diagnosen"),
                         p.diagnoses.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
            if (!p.notes.trimmed().isEmpty())
                h += QStringLiteral("<h3>%1</h3><p>%2</p>").arg(tr("Bemerkungen"),
                         p.notes.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
            if (admission) {
                h += QStringLiteral("<h3>%1</h3>").arg(tr("Vitalwerte bei Aufnahme (%1)")
                         .arg(loc.toString(admission->measuredAt, QStringLiteral("dd.MM.yyyy HH:mm"))));
                h += tableOpen(dev);
                h += QStringLiteral("<tr>");
                QStringList vals;
                if (admission->systolic || admission->diastolic) {
                    h += th(tr("RR [mmHg]"));
                    vals << QStringLiteral("%1/%2").arg(admission->systolic ? QString::number(*admission->systolic) : QStringLiteral("–"),
                                                        admission->diastolic ? QString::number(*admission->diastolic) : QStringLiteral("–"));
                }
                for (VitalKey k : { VitalKey::Pulse, VitalKey::PulsePressure, VitalKey::Temperature, VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose,
                                    VitalKey::Weight, VitalKey::Height, VitalKey::Bmi, VitalKey::Pain }) {
                    const QString s = VitalDefs::formatted(*admission, k);
                    if (s.isEmpty()) continue;
                    h += th(VitalDefs::header(k));
                    vals << s;
                }
                h += QStringLiteral("</tr><tr>");
                for (const QString &v : vals) h += td(v, QStringLiteral("right"));
                h += QStringLiteral("</tr></table>");
                if (!admission->observations.trimmed().isEmpty())
                    h += QStringLiteral("<p>%1</p>").arg(admission->observations.toHtmlEscaped());
            }
            h += Wounds::admissionHtml(p.id);
            h += QStringLiteral("<h3>%1</h3>").arg(tr("Aktueller Medikamentenplan"));
            QList<Medication> active;
            for (const Medication &m : meds) if (!m.archived) active << m;
            h += medicationHtml(dev, p, active);
                return h;
}

int longTermMedicationCount(const QList<Medication> &meds)
{
    int n = 0;
    for (const Medication &m : meds)
        if (!m.archived && m.category == Medication::Regular) ++n;
    return n;
}

QString pharmacyListHtml(QPaintDevice *dev, const Patient &p, const QList<Medication> &meds)
{
    const QLocale loc;
    Database &db = Database::instance();
    QString h;
    h += QStringLiteral("<p>%1</p>").arg(tr("Bitte prüfen Sie diese Liste auf Wechselwirkungen, doppelte Wirkstoffe, "
                                            "Dosierung und Einnahmezeitpunkte. Ergänzungen und Notizen bitte unten eintragen.").toHtmlEscaped());

    // ---- Angaben zur Person, die für die Prüfung wichtig sind
    h += tableOpen(dev, 0);
    auto row = [&](const QString &k, const QString &v, bool strong = false) {
        if (v.trimmed().isEmpty()) return;
        QString val = v.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("; "));
        if (strong) val = QStringLiteral("<b>%1</b>").arg(val);
        h += QStringLiteral("<tr><td width=\"20%\"><b>%1</b></td><td>%2</td></tr>").arg(k.toHtmlEscaped(), val);
    };
    row(tr("Name"), p.displayName());
    QString born;
    if (p.birthDate.isValid()) {
        born = loc.toString(p.birthDate, QStringLiteral("dd.MM.yyyy"));
        const int age = p.ageAt(QDate::currentDate());
        if (age >= 0) born += tr(" (%1 Jahre)").arg(age);
    }
    row(tr("Geburtsdatum"), born);
    row(tr("Geschlecht"), p.sex == QLatin1String("m") ? tr("männlich") : p.sex == QLatin1String("w") ? tr("weiblich")
                         : p.sex == QLatin1String("d") ? tr("divers") : QString());
    // letztes Gewicht und letzte Größe (Dosierung, Nierenfunktion)
    std::optional<VitalRecord> lastWeight;
    for (const VitalRecord &r : db.vitals(p.id))
        if (r.weight && (!lastWeight || r.measuredAt > lastWeight->measuredAt)) lastWeight = r;
    if (lastWeight)
        row(tr("Gewicht"), tr("%1 kg (gemessen am %2)").arg(loc.toString(*lastWeight->weight, 'f', 1),
                                                          loc.toString(lastWeight->measuredAt.date(), QStringLiteral("dd.MM.yyyy"))));
    if (const auto height = db.latestHeight(p.id))
        row(tr("Größe"), tr("%1 cm").arg(loc.toString(*height, 'f', 0)));
    row(tr("Allergien/Unverträglichkeiten"), p.extra.value(QStringLiteral("Allergien")).toString().trimmed(), true);
    row(tr("Diagnosen"), p.diagnoses.trimmed());
    row(tr("Pflegegrad"), CareInfo::careLevelText(p));
    row(tr("Hausarztpraxis"), Contacts::familyDoctorLine(p));
    row(tr("Stand der Liste"), loc.toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy")));
    h += QStringLiteral("</table>");

    // ---- Medikamente nach Art
    QList<Medication> current;
    for (const Medication &m : meds) if (!m.archived) current << m;
    for (const auto &[cat, list] : MedSchedule::grouped(current)) {
        if (list.isEmpty()) continue;
        h += QStringLiteral("<h4 style=\"margin-top:8px; margin-bottom:2px;\">%1 (%2)</h4>")
                 .arg(MedSchedule::categoryName(cat).toHtmlEscaped()).arg(list.size());
        h += tableOpen(dev);
        const bool regular = cat == Medication::Regular;
        auto w = [](int pct, const char *align = nullptr) {
            return QStringLiteral("width=\"%1%\"").arg(pct) + (align ? QStringLiteral(" align=\"%1\"").arg(QLatin1String(align)) : QString());
        };
        h += QStringLiteral("<thead><tr>") + th(tr("Wirkstoff"), w(13)) + th(tr("Handelsname"), w(13)) + th(tr("Stärke"), w(9))
           + th(tr("Form"), w(9)) + th(tr("mo"), w(3, "center")) + th(tr("mi"), w(3, "center"))
           + th(tr("ab"), w(3, "center")) + th(tr("na"), w(3, "center"))
           + th(tr("Einheit"), w(6)) + th(regular ? tr("Einnahme") : tr("Anlass"), w(15)) + th(tr("Grund"), w(10))
           + th(tr("seit"), w(7)) + th(tr("PZN"), w(6)) + QStringLiteral("</tr></thead>");
        for (int i = 0; i < list.size(); ++i) {
            const Medication &m = list[i];
            auto dose = [regular](const QString &s) { return s.trimmed().isEmpty() ? (regular ? QStringLiteral("0") : QString()) : s; };
            const QString intake = regular ? QStringList{ MedSchedule::isDaily(m) ? QString() : MedSchedule::describe(m), m.rhythmNote, m.notes }
                                                 .filter(QRegularExpression(QStringLiteral("\\S"))).join(QStringLiteral("; "))
                                           : QStringList{ m.rhythmNote, m.notes }.filter(QRegularExpression(QStringLiteral("\\S"))).join(QStringLiteral("; "));
            h += i % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
            h += QStringLiteral("<td><b>%1</b></td>").arg(m.substance.toHtmlEscaped())
               + td(m.name) + td(m.strength) + td(m.form)
               + td(dose(m.morning), QStringLiteral("center")) + td(dose(m.noon), QStringLiteral("center"))
               + td(dose(m.evening), QStringLiteral("center")) + td(dose(m.night), QStringLiteral("center"))
               + td(m.unit) + td(intake) + td(m.reason)
               + td(m.startDate.isValid() ? loc.toString(m.startDate, QStringLiteral("MM/yyyy")) : QString())
               + td(m.pzn) + QStringLiteral("</tr>");
        }
        h += QStringLiteral("</table>");
    }
    if (current.isEmpty())
        h += QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine aktuellen Medikamente eingetragen."));

    // ---- Was oft fehlt: zum Ankreuzen und Ergänzen
    const QString box = QStringLiteral("&#9744;");
    h += QStringLiteral("<h4 style=\"margin-top:10px; margin-bottom:2px;\">%1</h4>").arg(tr("Bitte ergänzen – wird oft vergessen"));
    h += QStringLiteral("<p>");
    for (const QString &item : { tr("frei verkäufliche Mittel (Schmerz-, Abführ-, Schlaf-, Erkältungsmittel)"),
                                 tr("Nahrungsergänzung, Vitamine, Mineralstoffe"), tr("pflanzliche Mittel und Tees"),
                                 tr("Salben, Augentropfen, Sprays, Pflaster, Inhalatoren"),
                                 tr("Medikamente von anderen Ärzten oder aus dem Krankenhaus") })
        h += box + QStringLiteral("&nbsp;") + item.toHtmlEscaped() + QStringLiteral("&nbsp;&nbsp;&nbsp; ");
    h += QStringLiteral("</p>");
    h += tableOpen(dev);
    h += QStringLiteral("<thead><tr>") + th(tr("Präparat / Wirkstoff")) + th(tr("Stärke")) + th(tr("Wie oft / wann"))
       + th(tr("Wofür")) + QStringLiteral("</tr></thead>");
    for (int i = 0; i < 4; ++i)
        h += QStringLiteral("<tr><td>&nbsp;<br>&nbsp;</td><td></td><td></td><td></td></tr>");
    h += QStringLiteral("</table>");

    // ---- Fragen und Notizen
    h += QStringLiteral("<h4 style=\"margin-top:10px; margin-bottom:2px;\">%1</h4>").arg(tr("Fragen an die Apotheke"));
    h += QStringLiteral("<p>");
    for (const QString &q : { tr("Gibt es Wechselwirkungen zwischen diesen Mitteln?"),
                              tr("Ist ein Wirkstoff doppelt enthalten?"),
                              tr("Passt die Dosierung (Alter, Gewicht, Nierenfunktion)?"),
                              tr("Wann und wie einnehmen (vor/nach dem Essen, Abstand zu anderen Mitteln)?") })
        h += box + QStringLiteral("&nbsp;") + q.toHtmlEscaped() + QStringLiteral("<br>");
    h += QStringLiteral("</p>");
    h += QStringLiteral("<h4 style=\"margin-top:10px; margin-bottom:2px;\">%1</h4>").arg(tr("Notizen der Apotheke"));
    h += tableOpen(dev);
    h += QStringLiteral("<tr><td>&nbsp;<br>&nbsp;<br>&nbsp;<br>&nbsp;<br>&nbsp;</td></tr></table>");

    // ---- Hinweis auf die Medikationsberatung
    const int n = longTermMedicationCount(current);
    const QString advice = n >= 5
        ? tr("Hinweis: Bei fünf oder mehr dauerhaft ärztlich verordneten Arzneimitteln besteht alle zwölf Monate "
             "Anspruch auf eine erweiterte Medikationsberatung in der Apotheke (pharmazeutische Dienstleistung, "
             "die Kosten trägt die Krankenkasse). In dieser Liste stehen %1 Arzneimittel der Regelmedikation.").arg(n)
        : tr("Hinweis: Apotheken prüfen Medikamente auf Wechselwirkungen. Bei fünf oder mehr dauerhaft ärztlich "
             "verordneten Arzneimitteln besteht alle zwölf Monate Anspruch auf eine erweiterte Medikationsberatung "
             "(pharmazeutische Dienstleistung, die Kosten trägt die Krankenkasse).");
    h += QStringLiteral("<p style=\"margin-top:8px;\"><b>%1</b></p>").arg(advice.toHtmlEscaped());
    h += QStringLiteral("<p><small>%1</small></p>")
             .arg(tr("Diese Liste wurde vom Anwender selbst mit VitaParms geführt. VitaParms prüft nicht auf "
                     "Wechselwirkungen und ersetzt weder die Beratung in Apotheke oder Arztpraxis noch den "
                     "bundeseinheitlichen Medikationsplan.").toHtmlEscaped());
    return h;
}

void pharmacyList(QWidget *parent, const Patient &p, const QList<Medication> &meds, Target t)
{
    run(parent, t, tr("Medikationsliste für die Apotheke"), QStringLiteral("Apotheke"), p, QPageLayout::Landscape,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Medikationsliste für die Apotheke"), p, pharmacyListHtml(dev, p, meds), 8.5); });
}

bool pharmacyListToPdf(const QString &path, const Patient &p, const QList<Medication> &meds)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setTitle(tr("Medikationsliste für die Apotheke"));
    configure(&w, QPageLayout::Landscape);
    printHtml(&w, tr("Medikationsliste für die Apotheke"), p, pharmacyListHtml(&w, p, meds), 8.5);
    return true;
}

QString orderListHtml(QPaintDevice *dev, const Patient &p, const MedStock::Order &o)
{
    const QLocale loc;
    QString h;
    h += tableOpen(dev, 0);
    auto row = [&](const QString &k, const QString &v) {
        if (v.trimmed().isEmpty()) return;
        h += QStringLiteral("<tr><td width=\"22%\"><b>%1</b></td><td>%2</td></tr>").arg(k.toHtmlEscaped(), v.toHtmlEscaped());
    };
    row(tr("Name"), p.displayName());
    if (p.birthDate.isValid()) row(tr("Geburtsdatum"), loc.toString(p.birthDate, QStringLiteral("dd.MM.yyyy")));
    row(tr("Krankenkasse"), Contacts::insuranceLine(p));
    row(tr("Hausarztpraxis"), Contacts::familyDoctorLine(p));
    row(tr("Bestellliste vom"), loc.toString(o.createdAt, QStringLiteral("dd.MM.yyyy HH:mm")) + (o.createdBy.isEmpty() ? QString() : QStringLiteral(" (") + o.createdBy + QLatin1Char(')')));
    row(tr("Status"), o.statusText() + (o.orderedBy.isEmpty() ? QString() : tr(" von %1").arg(o.orderedBy)));
    row(tr("Bemerkung"), o.note);
    h += QStringLiteral("</table><br>");
    h += tableOpen(dev);
    auto w = [](int pct) { return QStringLiteral("width=\"%1%\"").arg(pct); };
    h += QStringLiteral("<thead><tr>") + th(tr("Präparat"), w(19)) + th(tr("Wirkstoff"), w(13)) + th(tr("Stärke"), w(8))
       + th(tr("Form"), w(9)) + th(tr("PZN"), w(11)) + th(tr("Menge"), w(12)) + th(tr("Grund"), w(17))
       + th(tr("geliefert"), w(11)) + QStringLiteral("</tr></thead>");
    for (int i = 0; i < o.items.size(); ++i) {
        const MedStock::OrderItem &it = o.items[i];
        h += i % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
        h += QStringLiteral("<td><b>%1</b></td>").arg(it.name.toHtmlEscaped()) + td(it.substance) + td(it.strength) + td(it.form)
           + td(it.pzn) + td(it.amount) + td(it.reason)
           + td(it.delivered ? loc.toString(it.deliveredAt, QStringLiteral("dd.MM.")) : QString()) + QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    h += QStringLiteral("<br><p>%1</p><p>%2</p>")
             .arg(tr("Rezept angefordert am: ______________ &nbsp;&nbsp;&nbsp; bestellt bei: ______________________________"),
                  tr("bestellt am: ______________ &nbsp;&nbsp;&nbsp; Abholung/Lieferung am: ______________"));
    h += QStringLiteral("<p><small>%1</small></p>").arg(tr("Erstellt mit VitaParms – Bestandsführung. Die Mengen sind Vorschläge; "
                                                           "maßgeblich ist die ärztliche Verordnung.").toHtmlEscaped());
    return h;
}

void orderList(QWidget *parent, const Patient &p, const MedStock::Order &o, Target t)
{
    run(parent, t, tr("Bestellliste Medikamente"), QStringLiteral("Bestellliste"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Bestellliste Medikamente"), p, orderListHtml(dev, p, o), 9); });
}

bool orderListToPdf(const QString &path, const Patient &p, const MedStock::Order &o)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    w.setTitle(tr("Bestellliste Medikamente"));
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Bestellliste Medikamente"), p, orderListHtml(&w, p, o), 9);
    return true;
}

void patientSheet(QWidget *parent, const Patient &p, const QList<Medication> &meds,
                  const std::optional<VitalRecord> &admission, Target t)
{
    run(parent, t, tr("Stammblatt"), QStringLiteral("Stammblatt"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Stammblatt"), p, patientSheetHtml(dev, p, meds, admission)); });
}

bool patientSheetToPdf(const QString &path, const Patient &p, const QList<Medication> &meds,
                       const std::optional<VitalRecord> &admission)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Stammblatt"), p, patientSheetHtml(&w, p, meds, admission));
    return true;
}

QString documentIndexHtml(QPaintDevice *dev, const QList<MedicalDocument> &docs,
                          const QList<QPair<QString, QString>> &reports, const QString &note)
{
    const QLocale loc;
    QString h;
    if (!note.isEmpty())
        h += QStringLiteral("<p>%1</p>").arg(note.toHtmlEscaped());
    if (!reports.isEmpty()) {
        h += QStringLiteral("<h3>%1</h3>").arg(tr("Berichte aus VitaParms"));
        h += tableOpen(dev);
        h += QStringLiteral("<thead><tr>") + th(tr("Datei"), QStringLiteral("width=\"45%\""))
           + th(tr("Inhalt")) + QStringLiteral("</tr></thead>");
        for (const auto &r : reports)
            h += QStringLiteral("<tr>") + td(r.first) + td(r.second) + QStringLiteral("</tr>");
        h += QStringLiteral("</table>");
    }
    h += QStringLiteral("<h3>%1</h3>").arg(tr("Dokumente (%1)").arg(docs.size()));
    h += tableOpen(dev);
    h += QStringLiteral("<thead><tr>") + th(tr("Nr."), QStringLiteral("width=\"6%\""))
       + th(tr("Datum"), QStringLiteral("width=\"14%\"")) + th(tr("Art"), QStringLiteral("width=\"15%\""))
       + th(tr("Titel")) + th(tr("Von"), QStringLiteral("width=\"20%\""))
       + th(tr("Seiten"), QStringLiteral("width=\"9%\" align=\"right\"")) + QStringLiteral("</tr></thead>");
    for (int i = 0; i < docs.size(); ++i) {
        const MedicalDocument &d = docs[i];
        h += i % 2 ? QStringLiteral("<tr bgcolor=\"#f6f8fa\">") : QStringLiteral("<tr>");
        h += td(QString::number(i + 1), QStringLiteral("right"))
           + td(d.docDate.isValid() ? loc.toString(d.docDate, QStringLiteral("dd.MM.yyyy")) : QString())
           + td(d.category) + td(d.title + (d.notes.isEmpty() ? QString() : QStringLiteral("\n") + d.notes))
           + td(d.issuer) + td(d.pages > 0 ? QString::number(d.pages) : QString(), QStringLiteral("right"));
        h += QStringLiteral("</tr>");
    }
    h += QStringLiteral("</table>");
    return h;
}

bool documentIndexToPdf(const QString &path, const Patient &p, const QList<MedicalDocument> &docs,
                        const QList<QPair<QString, QString>> &reports, const QString &note)
{
    QPdfWriter w(path);
    w.setResolution(300);
    w.setCreator(QStringLiteral("VitaParms"));
    configure(&w, QPageLayout::Portrait);
    printHtml(&w, tr("Inhaltsverzeichnis der Krankenakte"), p, documentIndexHtml(&w, docs, reports, note));
    return true;
}

void documentIndex(QWidget *parent, const Patient &p, const QList<MedicalDocument> &docs, Target t)
{
    run(parent, t, tr("Dokumentenliste"), QStringLiteral("Dokumentenliste"), p, QPageLayout::Portrait,
        [&](QPagedPaintDevice *dev) { printHtml(dev, tr("Dokumente der Krankenakte"), p, documentIndexHtml(dev, docs, {}, {})); });
}

} // namespace ReportPrinter
