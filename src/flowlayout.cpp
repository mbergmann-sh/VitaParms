// VitaParms – umbrechendes Layout (nach dem Qt-Beispiel „Flow Layout“)
#include "flowlayout.h"

#include <QStyle>
#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }
int FlowLayout::count() const { return int(m_items.size()); }
QLayoutItem *FlowLayout::itemAt(int index) const { return m_items.value(index); }

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_items.size())
        return m_items.takeAt(index);
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const { return {}; }

int FlowLayout::horizontalSpacing() const
{
    return m_hSpace >= 0 ? m_hSpace : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    return m_vSpace >= 0 ? m_vSpace : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }

QSize FlowLayout::sizeHint() const
{
    // bevorzugt: alles in einer Zeile
    int w = 0, h = 0;
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty()) continue;
        const QSize s = item->sizeHint();
        w += s.width() + (w > 0 ? horizontalSpacing() : 0);
        h = qMax(h, s.height());
    }
    const QMargins m = contentsMargins();
    return QSize(w + m.left() + m.right(), h + m.top() + m.bottom());
}

QSize FlowLayout::minimumSize() const
{
    // mindestens so breit wie das breiteste Element – nicht so breit wie alle zusammen
    QSize size;
    for (QLayoutItem *item : m_items)
        if (!item->isEmpty()) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    // Elemente einer Zeile vertikal zentrieren: erst die Zeile sammeln, dann setzen
    QList<QPair<QLayoutItem *, QPoint>> line;
    auto flush = [&] {
        if (!testOnly)
            for (const auto &[item, pos] : line) {
                const QSize s = item->sizeHint();
                item->setGeometry(QRect(QPoint(pos.x(), pos.y() + (lineHeight - s.height()) / 2), s));
            }
        line.clear();
    };
    for (QLayoutItem *item : m_items) {
        if (item->isEmpty()) continue;
        const QSize s = item->sizeHint();
        const int spaceX = horizontalSpacing();
        int nextX = x + s.width() + spaceX;
        if (nextX - spaceX > r.right() + 1 && lineHeight > 0) {
            flush();
            x = r.x();
            y += lineHeight + verticalSpacing();
            nextX = x + s.width() + spaceX;
            lineHeight = 0;
        }
        line.append({ item, QPoint(x, y) });
        x = nextX;
        lineHeight = qMax(lineHeight, s.height());
    }
    flush();
    return y + lineHeight - rect.y() + m.bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    int v = -1;
    if (QObject *p = parent()) {
        if (p->isWidgetType()) {
            auto *pw = static_cast<QWidget *>(p);
            v = pw->style()->pixelMetric(pm, nullptr, pw);
        } else {
            v = static_cast<QLayout *>(p)->spacing();
        }
    }
    return v >= 0 ? v : 6;
}
