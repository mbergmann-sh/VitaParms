// SPDX-License-Identifier: MIT
#include "chartitem.h"
#include "database.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>

ChartItem::ChartItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(true);
    m_spec.scale = ChartSpec::Scale::Week;
    m_spec.panelIds = { QStringLiteral("bp"), QStringLiteral("glucose"), QStringLiteral("weight") };
    ChartSpec::rangeFor(m_spec.scale, m_ref, m_spec.from, m_spec.to);
}

void ChartItem::setPatientId(qint64 id)
{
    if (id == m_pid) return;
    m_pid = id;
    reload();
}

void ChartItem::setScale(int s)
{
    s = qBound(0, s, 3);
    if (s == int(m_spec.scale)) return;
    m_spec.scale = ChartSpec::Scale(s);
    reload();
}

void ChartItem::setRefDate(const QString &d)
{
    const QDate x = QDate::fromString(d, Qt::ISODate);
    if (!x.isValid() || x == m_ref) return;
    m_ref = x;
    reload();
}

void ChartItem::setPanels(const QStringList &p)
{
    if (p == m_spec.panelIds) return;
    m_spec.panelIds = p;
    clearTip();
    update();
    emit specChanged();
}

void ChartItem::setHiddenKeys(const QStringList &k)
{
    if (k == m_spec.hiddenKeys) return;
    m_spec.hiddenKeys = k;
    clearTip();
    update();
    emit specChanged();
}

void ChartItem::setRevision(int r)
{
    if (r == m_revision) return;
    m_revision = r;
    reload();
}

void ChartItem::reload()
{
    ChartSpec::rangeFor(m_spec.scale, m_ref, m_spec.from, m_spec.to);
    Database &db = Database::instance();
    m_all = (m_pid && db.isOpen()) ? db.vitals(m_pid) : QList<VitalRecord>();
    m_spec.records.clear();
    for (const VitalRecord &r : std::as_const(m_all))
        if (r.measuredAt >= m_spec.from && r.measuredAt < m_spec.to) m_spec.records << r;
    clearTip();
    update();
    emit specChanged();
}

QVariantList ChartItem::availablePanels() const
{
    QVariantList l;
    for (const VitalDefs::Panel &pd : VitalDefs::panels()) {
        bool has = false;
        for (const VitalRecord &r : m_all) {
            for (VitalKey k : pd.keys)
                if (VitalDefs::value(r, k)) { has = true; break; }
            if (has) break;
        }
        QVariantList colors, keys;
        for (VitalKey k : pd.keys) { colors << VitalDefs::color(k).name(); keys << VitalDefs::key(k); }
        l << QVariantMap{ { QStringLiteral("id"), pd.id }, { QStringLiteral("title"), pd.title },
                          { QStringLiteral("colors"), colors }, { QStringLiteral("keys"), keys },
                          { QStringLiteral("hasData"), has } };
    }
    return l;
}

void ChartItem::step(int dir)
{
    switch (m_spec.scale) {
    case ChartSpec::Scale::Day:   m_ref = m_ref.addDays(dir); break;
    case ChartSpec::Scale::Week:  m_ref = m_ref.addDays(7 * dir); break;
    case ChartSpec::Scale::Month: m_ref = m_ref.addMonths(dir); break;
    case ChartSpec::Scale::Year:  m_ref = m_ref.addYears(dir); break;
    case ChartSpec::Scale::Custom: break;
    }
    reload();
}

void ChartItem::today()
{
    m_ref = QDate::currentDate();
    reload();
}

void ChartItem::paint(QPainter *p)
{
    const QColor bg = m_dark ? QApplication::palette().color(QPalette::Base) : QColor(Qt::white);
    p->fillRect(QRectF(0, 0, width(), height()), bg);
    m_hits.clear();
    m_legend.clear();
    if (m_spec.panelIds.isEmpty()) return;
    ChartRenderer::paint(*p, QRectF(0, 0, width(), height()).adjusted(8, 6, -8, -6), m_spec, &m_hits, true, &m_legend);
    if (m_hover >= 0 && m_hover < m_hits.size()) {
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(m_dark ? QColor(255, 255, 255, 200) : QColor(0, 0, 0, 170), 2));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(m_hits[m_hover].pos, 11, 11);
    }
}

bool ChartItem::tapAt(qreal x, qreal y)
{
    const QPointF pos(x, y);
    for (const auto &l : std::as_const(m_legend))
        if (l.rect.adjusted(-6, -8, 6, 8).contains(pos)) {          // großzügige Trefferfläche für Finger
            QStringList h = m_spec.hiddenKeys;
            const QString k = VitalDefs::key(l.key);
            if (h.contains(k)) h.removeAll(k);
            else h << k;
            setHiddenKeys(h);
            return true;
        }
    int best = -1;
    qreal bestD = 28.0 * 28.0;
    for (int i = 0; i < m_hits.size(); ++i) {
        const QPointF d = m_hits[i].pos - pos;
        const qreal dd = d.x() * d.x() + d.y() * d.y();
        if (dd < bestD) { bestD = dd; best = i; }
    }
    m_hover = best;
    m_tip = best >= 0 ? m_hits[best].text : QString();
    m_tipPos = best >= 0 ? m_hits[best].pos : QPointF();
    update();
    emit tipChanged();
    return false;
}

void ChartItem::clearTip()
{
    if (m_hover < 0 && m_tip.isEmpty()) return;
    m_hover = -1;
    m_tip.clear();
    update();
    emit tipChanged();
}
