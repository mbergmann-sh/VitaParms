// SPDX-License-Identifier: MIT
#include "xlsxwriter.h"
#include "zipwriter.h"

#include <QBuffer>
#include <QDateTime>
#include <QRegularExpression>

namespace {

QString esc(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        default:
            // In XML 1.0 unzulässige Steuerzeichen verwerfen
            if (c.unicode() < 0x20 && c != QLatin1Char('\n') && c != QLatin1Char('\t') && c != QLatin1Char('\r'))
                continue;
            out += c;
        }
    }
    return out;
}

double excelSerial(const QDateTime &dt)
{
    static const QDate epoch(1899, 12, 30);
    const double days = epoch.daysTo(dt.date());
    const double frac = dt.time().msecsSinceStartOfDay() / 86400000.0;
    return days + frac;
}

QString num(double v)
{
    return QString::number(v, 'g', 15);
}

const char *kNsMain = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char *kNsRel  = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

} // namespace

QString XlsxWriter::columnName(int col)
{
    QString s;
    ++col;
    while (col > 0) {
        const int r = (col - 1) % 26;
        s.prepend(QChar('A' + r));
        col = (col - 1) / 26;
    }
    return s;
}

int XlsxWriter::addSheet(const QString &name)
{
    Sheet s;
    QString n = name;
    n.replace(QRegularExpression(QStringLiteral("[\\[\\]:*?/\\\\]")), QStringLiteral("_"));
    s.name = n.left(31);
    if (s.name.isEmpty()) s.name = QStringLiteral("Tabelle%1").arg(m_sheets.size() + 1);
    m_sheets.append(s);
    return m_sheets.size() - 1;
}

void XlsxWriter::setCell(int sheet, int row, int col, const QVariant &value, Style style)
{
    m_sheets[sheet].rows[row][col] = { value, style };
}

void XlsxWriter::setColumnWidth(int sheet, int col, double chars)
{
    m_sheets[sheet].widths[col] = chars;
}

void XlsxWriter::freezeRows(int sheet, int rows)
{
    m_sheets[sheet].frozenRows = rows;
}

void XlsxWriter::setLandscape(int sheet, bool landscape)
{
    m_sheets[sheet].landscape = landscape;
}

void XlsxWriter::setImage(int sheet, const QImage &img, int row, int col, double displayDpi)
{
    Sheet &s = m_sheets[sheet];
    s.image = img;
    s.imgRow = row;
    s.imgCol = col;
    s.imgDpi = displayDpi;
}

bool XlsxWriter::save(const QString &path, QString *error) const
{
    ZipWriter zip;

    // ------------------------------------------------ [Content_Types].xml
    QString ct = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>");
    int drawingNo = 0;
    for (int i = 0; i < m_sheets.size(); ++i) {
        ct += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>").arg(i + 1);
        if (!m_sheets[i].image.isNull())
            ct += QStringLiteral("<Override PartName=\"/xl/drawings/drawing%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>").arg(++drawingNo);
    }
    ct += QStringLiteral("</Types>");
    zip.addFile(QStringLiteral("[Content_Types].xml"), ct.toUtf8());

    // ------------------------------------------------ _rels/.rels
    zip.addFile(QStringLiteral("_rels/.rels"), QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/>"
        "</Relationships>"));

    // ------------------------------------------------ docProps/core.xml
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    zip.addFile(QStringLiteral("docProps/core.xml"), QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
        "<dc:title>%1</dc:title><dc:creator>VitaParms</dc:creator>"
        "<dcterms:created xsi:type=\"dcterms:W3CDTF\">%2</dcterms:created>"
        "<dcterms:modified xsi:type=\"dcterms:W3CDTF\">%2</dcterms:modified>"
        "</cp:coreProperties>").arg(esc(m_title), now).toUtf8());

    // ------------------------------------------------ workbook + rels
    QString wb = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"%1\" xmlns:r=\"%2\"><sheets>").arg(QLatin1String(kNsMain), QLatin1String(kNsRel));
    QString wbRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (int i = 0; i < m_sheets.size(); ++i) {
        wb += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(esc(m_sheets[i].name)).arg(i + 1);
        wbRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%1.xml\"/>").arg(i + 1);
    }
    wb += QStringLiteral("</sheets></workbook>");
    wbRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>")
                  .arg(m_sheets.size() + 1);
    zip.addFile(QStringLiteral("xl/workbook.xml"), wb.toUtf8());
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), wbRels.toUtf8());

    // ------------------------------------------------ styles
    zip.addFile(QStringLiteral("xl/styles.xml"), QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"%1\">"
        "<numFmts count=\"3\">"
        "<numFmt numFmtId=\"164\" formatCode=\"dd.mm.yyyy hh:mm\"/>"
        "<numFmt numFmtId=\"165\" formatCode=\"0.0\"/>"
        "<numFmt numFmtId=\"166\" formatCode=\"dd.mm.yyyy\"/>"
        "</numFmts>"
        "<fonts count=\"3\">"
        "<font><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
        "<font><b/><sz val=\"14\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
        "</fonts>"
        "<fills count=\"3\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE8EEF4\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left/><right/><top/><bottom style=\"thin\"><color rgb=\"FF8899AA\"/></bottom><diagonal/></border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"9\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"1\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"top\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"166\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Standard\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>").arg(QLatin1String(kNsMain)).toUtf8());

    // ------------------------------------------------ Tabellenblätter
    drawingNo = 0;
    for (int si = 0; si < m_sheets.size(); ++si) {
        const Sheet &s = m_sheets[si];
        QString x;
        x.reserve(64 * 1024);
        x += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                            "<worksheet xmlns=\"%1\" xmlns:r=\"%2\">")
                 .arg(QLatin1String(kNsMain), QLatin1String(kNsRel));
        x += QStringLiteral("<sheetPr><pageSetUpPr fitToPage=\"1\"/></sheetPr>");
        x += QStringLiteral("<sheetViews><sheetView workbookViewId=\"0\"%1>")
                 .arg(si == 0 ? QStringLiteral(" tabSelected=\"1\"") : QString());
        if (s.frozenRows > 0)
            x += QStringLiteral("<pane ySplit=\"%1\" topLeftCell=\"A%2\" activePane=\"bottomLeft\" state=\"frozen\"/>")
                     .arg(s.frozenRows).arg(s.frozenRows + 1);
        x += QStringLiteral("</sheetView></sheetViews>");
        if (!s.widths.isEmpty()) {
            x += QStringLiteral("<cols>");
            for (auto it = s.widths.begin(); it != s.widths.end(); ++it)
                x += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                         .arg(it.key() + 1).arg(num(it.value()));
            x += QStringLiteral("</cols>");
        }
        x += QStringLiteral("<sheetData>");
        for (auto rit = s.rows.begin(); rit != s.rows.end(); ++rit) {
            x += QStringLiteral("<row r=\"%1\">").arg(rit.key() + 1);
            for (auto cit = rit.value().begin(); cit != rit.value().end(); ++cit) {
                const QString ref = columnName(cit.key()) + QString::number(rit.key() + 1);
                const QVariant &v = cit.value().v;
                const int st = int(cit.value().s);
                const QString sAttr = st ? QStringLiteral(" s=\"%1\"").arg(st) : QString();
                if (!v.isValid() || v.isNull()) {
                    if (st) x += QStringLiteral("<c r=\"%1\"%2/>").arg(ref, sAttr);
                    continue;
                }
                switch (v.typeId()) {
                case QMetaType::Int: case QMetaType::LongLong: case QMetaType::Double:
                case QMetaType::Float: case QMetaType::UInt: case QMetaType::ULongLong:
                    x += QStringLiteral("<c r=\"%1\"%2><v>%3</v></c>").arg(ref, sAttr, num(v.toDouble()));
                    break;
                case QMetaType::Bool:
                    x += QStringLiteral("<c r=\"%1\"%2 t=\"b\"><v>%3</v></c>").arg(ref, sAttr).arg(v.toBool() ? 1 : 0);
                    break;
                case QMetaType::QDateTime:
                    x += QStringLiteral("<c r=\"%1\" s=\"%2\"><v>%3</v></c>")
                             .arg(ref).arg(st ? st : int(DateTime)).arg(num(excelSerial(v.toDateTime())));
                    break;
                case QMetaType::QDate:
                    x += QStringLiteral("<c r=\"%1\" s=\"%2\"><v>%3</v></c>")
                             .arg(ref).arg(st ? st : int(Date)).arg(num(excelSerial(QDateTime(v.toDate(), QTime(0, 0)))));
                    break;
                default:
                    x += QStringLiteral("<c r=\"%1\"%2 t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                             .arg(ref, sAttr, esc(v.toString()));
                }
            }
            x += QStringLiteral("</row>");
        }
        x += QStringLiteral("</sheetData>");
        x += QStringLiteral("<pageMargins left=\"0.6\" right=\"0.6\" top=\"0.7\" bottom=\"0.7\" header=\"0.3\" footer=\"0.3\"/>");
        x += QStringLiteral("<pageSetup paperSize=\"9\" orientation=\"%1\" fitToWidth=\"1\" fitToHeight=\"0\"/>")
                 .arg(s.landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
        if (!s.image.isNull())
            x += QStringLiteral("<drawing r:id=\"rId1\"/>");
        x += QStringLiteral("</worksheet>");
        zip.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(si + 1), x.toUtf8());

        if (!s.image.isNull()) {
            ++drawingNo;
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            s.image.save(&buf, "PNG");
            zip.addFile(QStringLiteral("xl/media/image%1.png").arg(drawingNo), png, false);

            zip.addFile(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(si + 1), QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing%1.xml\"/>"
                "</Relationships>").arg(drawingNo).toUtf8());
            zip.addFile(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(drawingNo), QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/image%1.png\"/>"
                "</Relationships>").arg(drawingNo).toUtf8());

            // Anzeigegröße in EMU (914400 je Zoll)
            const qint64 cx = qint64(s.image.width()  / s.imgDpi * 914400.0);
            const qint64 cy = qint64(s.image.height() / s.imgDpi * 914400.0);
            zip.addFile(QStringLiteral("xl/drawings/drawing%1.xml").arg(drawingNo), QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                "<xdr:oneCellAnchor>"
                "<xdr:from><xdr:col>%1</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>%2</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>"
                "<xdr:ext cx=\"%3\" cy=\"%4\"/>"
                "<xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"2\" name=\"Verlaufsgrafik\"/>"
                "<xdr:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></xdr:cNvPicPr></xdr:nvPicPr>"
                "<xdr:blipFill><a:blip r:embed=\"rId1\"/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill>"
                "<xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%3\" cy=\"%4\"/></a:xfrm>"
                "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr></xdr:pic>"
                "<xdr:clientData/></xdr:oneCellAnchor></xdr:wsDr>")
                .arg(s.imgCol).arg(s.imgRow).arg(cx).arg(cy).toUtf8());
        }
    }

    return zip.save(path, error);
}
