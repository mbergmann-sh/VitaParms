// VitaParms – Schreiben von Excel-Arbeitsmappen (.xlsx, Office Open XML)
// Eigenständig (nur QtCore/QtGui), unterstützt mehrere Tabellenblätter,
// einfache Formate, fixierte Kopfzeilen, A4-Seiteneinrichtung und je Blatt
// ein eingebettetes Bild (z. B. die Verlaufsgrafik).
// SPDX-License-Identifier: MIT
#pragma once

#include <QImage>
#include <QMap>
#include <QString>
#include <QVariant>

class XlsxWriter
{
public:
    enum Style { Default = 0, Header = 1, DateTime = 2, Integer = 3, Decimal1 = 4, Title = 5,
                 Wrap = 6, Date = 7, Decimal2 = 8 };

    int  addSheet(const QString &name);
    void setCell(int sheet, int row, int col, const QVariant &value, Style style = Default);
    void setColumnWidth(int sheet, int col, double chars);
    void freezeRows(int sheet, int rows);
    void setLandscape(int sheet, bool landscape);
    void setImage(int sheet, const QImage &img, int row, int col, double displayDpi = 96.0);
    void setTitle(const QString &t) { m_title = t; }

    bool save(const QString &path, QString *error = nullptr) const;

    static QString columnName(int col);   // 0 -> A

private:
    struct CellData { QVariant v; Style s; };
    struct Sheet {
        QString name;
        QMap<int, QMap<int, CellData>> rows;
        QMap<int, double> widths;
        int frozenRows = 0;
        bool landscape = false;
        QImage image;
        int imgRow = 0, imgCol = 0;
        double imgDpi = 96.0;
    };
    QList<Sheet> m_sheets;
    QString m_title;
};
