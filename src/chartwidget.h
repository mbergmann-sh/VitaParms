// VitaParms – Verlaufsdiagramme (eigene QPainter-Implementierung)
//
// Bewusst ohne Qt Charts / Qt Graphs: diese Module stehen in der
// Open-Source-Variante nur unter GPLv3 zur Verfügung, VitaParms ist MIT-lizenziert.
// Dieselbe Zeichenroutine wird für Bildschirm, Drucker (DIN A4), PDF und das
// in die Excel-Datei eingebettete Bild verwendet.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "vitaldefs.h"

#include <QImage>
#include <QPointF>
#include <QWidget>

class QPainter;

struct ChartSpec {
    enum class Scale { Day, Week, Month, Year, Custom };
    Scale       scale = Scale::Week;
    QDateTime   from;            // inklusive
    QDateTime   to;              // exklusive
    QStringList panelIds;        // siehe VitalDefs::panels()
    QStringList hiddenKeys;      // einzeln ausgeblendete Kurven (VitalDefs::key)
    bool        separatePages = false;   // Druck/PDF: jeder Bereich auf einer eigenen Seite
    QList<VitalRecord> records;  // beliebige Reihenfolge

    QString periodText() const;
    QString scaleName() const;
    bool isHidden(VitalKey k) const;
    // Lange Zeiträume (> 62 Tage) werden zu Tageswerten (Mittel, Min–Max) verdichtet
    bool aggregatesDays() const;
    // Sichtbare Bereiche (gewählt und mit mindestens einer eingeblendeten Kurve)
    QStringList visiblePanelIds() const;
    static void rangeFor(Scale s, const QDate &ref, QDateTime &from, QDateTime &to);
};

struct SeriesStats {
    VitalKey key;
    int      count = 0;
    double   min = 0, max = 0, mean = 0;
};

class ChartRenderer
{
public:
    struct Hit { QPointF pos; QString text; };
    struct LegendHit { QRectF rect; VitalKey key; };

    // Zeichnet alle gewählten Bereiche untereinander in 'rect'. Am Bildschirm ('screen')
    // erscheinen ausgeblendete Kurven grau in der Legende und lassen sich per Klick
    // wieder einblenden; im Druck fehlen sie ganz.
    static void paint(QPainter &p, const QRectF &rect, const ChartSpec &spec,
                      QList<Hit> *hits = nullptr, bool screen = false,
                      QList<LegendHit> *legend = nullptr);
    static QList<SeriesStats> statistics(const ChartSpec &spec);
    static QImage renderImage(const ChartSpec &spec, const QSize &size, qreal dpi = 150.0);
    // Schriftgröße am Bildschirm anpassen (Tablet: größer); Druck und PDF bleiben unverändert
    static void setScreenFontScale(qreal f);
};

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);
    void setSpec(const ChartSpec &spec);
    const ChartSpec &spec() const { return m_spec; }
    QSize minimumSizeHint() const override { return { 360, 240 }; }

signals:
    // Klick auf einen Legendeneintrag: Kurve ein-/ausblenden
    void legendClicked(VitalKey key);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    ChartSpec m_spec;
    QList<ChartRenderer::Hit> m_hits;
    QList<ChartRenderer::LegendHit> m_legend;
    int m_hover = -1;
};
