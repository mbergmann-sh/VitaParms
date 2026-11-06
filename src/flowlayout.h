// VitaParms – umbrechendes Layout (Zeilen wie Text): Elemente, die nicht mehr nebeneinander passen,
// rutschen in die nächste Zeile. Für Leisten mit vielen Schaltern (z. B. Bereiche der Grafik),
// damit das Hauptfenster auch auf kleinen Bildschirmen (1366 × 768) ganz sichtbar bleibt.
#pragma once

#include <QLayout>
#include <QList>
#include <QStyle>

class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;

    int horizontalSpacing() const;
    int verticalSpacing() const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_items;
    int m_hSpace;
    int m_vSpace;
};
