// VitaParms Tablet – Verlaufskurven als QML-Element (gleiche Zeichenroutine wie am Desktop)
// SPDX-License-Identifier: MIT
#pragma once

#include "chartwidget.h"

#include <QQuickPaintedItem>
#include <QVariant>

class ChartItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(qint64 patientId READ patientId WRITE setPatientId NOTIFY specChanged)
    Q_PROPERTY(int scale READ scale WRITE setScale NOTIFY specChanged)              // 0 Tag … 3 Jahr
    Q_PROPERTY(QString refDate READ refDate WRITE setRefDate NOTIFY specChanged)     // ISO-Datum
    Q_PROPERTY(QStringList panels READ panels WRITE setPanels NOTIFY specChanged)
    Q_PROPERTY(QStringList hiddenKeys READ hiddenKeys WRITE setHiddenKeys NOTIFY specChanged)
    Q_PROPERTY(int revision READ revision WRITE setRevision NOTIFY specChanged)      // Datenstand → neu laden
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY specChanged)
    Q_PROPERTY(QString periodText READ periodText NOTIFY specChanged)
    Q_PROPERTY(int recordCount READ recordCount NOTIFY specChanged)
    Q_PROPERTY(QVariantList availablePanels READ availablePanels NOTIFY specChanged)
    Q_PROPERTY(QString tipText READ tipText NOTIFY tipChanged)
    Q_PROPERTY(QPointF tipPos READ tipPos NOTIFY tipChanged)

public:
    explicit ChartItem(QQuickItem *parent = nullptr);
    void paint(QPainter *p) override;

    qint64 patientId() const { return m_pid; }
    void setPatientId(qint64 id);
    int scale() const { return int(m_spec.scale); }
    void setScale(int s);
    QString refDate() const { return m_ref.toString(Qt::ISODate); }
    void setRefDate(const QString &d);
    QStringList panels() const { return m_spec.panelIds; }
    void setPanels(const QStringList &p);
    QStringList hiddenKeys() const { return m_spec.hiddenKeys; }
    void setHiddenKeys(const QStringList &k);
    int revision() const { return m_revision; }
    void setRevision(int r);
    bool dark() const { return m_dark; }
    void setDark(bool d) { if (d != m_dark) { m_dark = d; update(); emit specChanged(); } }
    QString periodText() const { return m_spec.periodText(); }
    int recordCount() const { return m_spec.records.size(); }
    QVariantList availablePanels() const;
    QString tipText() const { return m_tip; }
    QPointF tipPos() const { return m_tipPos; }

    Q_INVOKABLE void step(int dir);
    Q_INVOKABLE void today();
    Q_INVOKABLE bool tapAt(qreal x, qreal y);       // Legende: Kurve ein/aus; sonst Messpunkt zeigen
    Q_INVOKABLE void clearTip();
    Q_INVOKABLE int panelCount() const { return m_spec.panelIds.size(); }

signals:
    void specChanged();
    void tipChanged();

private:
    void reload();

    qint64 m_pid = 0;
    QDate m_ref = QDate::currentDate();
    int m_revision = 0;
    bool m_dark = false;
    ChartSpec m_spec;
    QList<VitalRecord> m_all;     // alle Messungen der Person (für "hat Daten")
    QList<ChartRenderer::Hit> m_hits;
    QList<ChartRenderer::LegendHit> m_legend;
    QString m_tip;
    QPointF m_tipPos;
    int m_hover = -1;
};
