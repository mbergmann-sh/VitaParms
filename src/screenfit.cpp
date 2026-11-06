// VitaParms – Fenster an kleine Bildschirme anpassen (siehe screenfit.h)
#include "screenfit.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QLayout>
#include <QMainWindow>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Platz für Fensterrahmen und Titelleiste (kennt das Programm vor dem ersten Anzeigen noch nicht genau)
constexpr int kFrameW = 16;
constexpr int kFrameH = 48;

const char *const kWrapped = "vpScreenFitWrapped";

QRect availableFor(const QWidget *w)
{
    QScreen *s = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    s = w->screen();
#endif
    if (w->parentWidget() && w->parentWidget()->window()->isVisible()) {
        const QPoint c = w->parentWidget()->window()->frameGeometry().center();
        if (QScreen *ps = QGuiApplication::screenAt(c)) s = ps;
    }
    if (!s) s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry() : QRect(0, 0, 1366, 768);
}

bool isQtOwnDialog(const QWidget *w)
{
    for (const char *cls : { "QMessageBox", "QFileDialog", "QColorDialog", "QFontDialog", "QInputDialog",
                             "QProgressDialog", "QErrorMessage", "QAbstractPrintDialog", "QPageSetupDialog",
                             "QPrintPreviewDialog", "QWizard" })
        if (w->inherits(cls)) return true;
    return false;
}

bool containsButtonBox(QLayoutItem *item)
{
    if (!item) return false;
    if (QWidget *w = item->widget()) return qobject_cast<QDialogButtonBox *>(w) != nullptr;
    if (QLayout *l = item->layout())
        for (int i = 0; i < l->count(); ++i)
            if (containsButtonBox(l->itemAt(i))) return true;
    return false;
}

// Inhalt des Dialogs in eine rollbare Fläche verlegen; die Zeile mit der Schaltflächenleiste bleibt darunter.
bool wrap(QDialog *d, const QSize &maxClient)
{
    QLayout *lay = d->layout();
    if (!lay || d->property(kWrapped).toBool()) return false;

    // Zeile mit OK/Abbrechen suchen (letzte zuerst – dort steht sie fast immer)
    int bottomIndex = -1;
    for (int i = lay->count() - 1; i >= 0; --i)
        if (containsButtonBox(lay->itemAt(i))) { bottomIndex = i; break; }
    const QMargins m = lay->contentsMargins();
    QLayoutItem *bottom = bottomIndex >= 0 ? lay->takeAt(bottomIndex) : nullptr;

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("vpScreenFitContent"));
    content->setLayout(lay);                 // übernimmt das Layout samt aller Elemente vom Dialog
    // ohne untere Leiste kein unterer Rand im Inhalt doppelt
    if (bottom) lay->setContentsMargins(m.left(), m.top(), m.right(), m.bottom() / 2);

    auto *scroll = new QScrollArea(d);
    scroll->setObjectName(QStringLiteral("vpScreenFitScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto *nl = new QVBoxLayout(d);
    nl->setContentsMargins(0, 0, 0, 0);
    nl->setSpacing(0);
    nl->addWidget(scroll, 1);
    if (bottom) {
        auto *bl = new QVBoxLayout;
        bl->setContentsMargins(m.left(), m.bottom() / 2, m.right(), m.bottom());
        if (QWidget *w = bottom->widget()) {
            bl->addWidget(w);
            delete bottom;
        } else if (QLayout *l = bottom->layout()) {
            bl->addLayout(l);                // Elemente kommen zurück unter den Dialog
        } else {
            bl->addItem(bottom);
        }
        nl->addLayout(bl);
    }
    d->setProperty(kWrapped, true);

    // Größe: so groß wie der Inhalt es möchte, höchstens der freie Bildschirmbereich
    nl->activate();
    const int sb = scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scroll) + 2;
    const QSize contentHint = content->sizeHint();
    const int bottomH = nl->count() > 1 ? nl->itemAt(1)->sizeHint().height() : 0;
    QSize want(contentHint.width() + sb, contentHint.height() + bottomH);
    want = want.boundedTo(maxClient);
    d->setMinimumSize(0, 0);
    d->resize(want);
    return true;
}

// Fenster in den freien Bildschirmbereich holen
void clampIntoScreen(QWidget *w, const QRect &avail)
{
    const QSize maxClient(avail.width() - kFrameW, avail.height() - kFrameH);
    QSize sz = w->size();
    if (sz.width() > maxClient.width() || sz.height() > maxClient.height()) {
        const QSize minSz = w->minimumSizeHint().expandedTo(w->minimumSize());
        sz = sz.boundedTo(maxClient).expandedTo(minSz.boundedTo(maxClient));
        if (w->minimumWidth() > maxClient.width() || w->minimumHeight() > maxClient.height())
            w->setMinimumSize(qMin(w->minimumWidth(), maxClient.width()), qMin(w->minimumHeight(), maxClient.height()));
        w->resize(sz);
    }
    // Noch nicht platziertes Fenster: Qt (QDialog zentriert über dem Hauptfenster) bzw. die Fensterverwaltung
    // setzt es gleich mit der neuen Größe – nicht vorgreifen
    if (!w->testAttribute(Qt::WA_Moved)) return;
    // Position: Rahmen schätzen, wenn das Fenster noch nicht sichtbar war
    QRect fg = w->frameGeometry();
    if (fg.size() == w->size()) fg.adjust(-kFrameW / 2, -(kFrameH - kFrameW / 2), kFrameW / 2, kFrameW / 2);
    QPoint shift(0, 0);
    if (fg.right() > avail.right()) shift.rx() = avail.right() - fg.right();
    if (fg.bottom() > avail.bottom()) shift.ry() = avail.bottom() - fg.bottom();
    if (fg.left() + shift.x() < avail.left()) shift.rx() = avail.left() - fg.left();
    if (fg.top() + shift.y() < avail.top()) shift.ry() = avail.top() - fg.top();
    if (!shift.isNull()) w->move(w->pos() + shift);
}

class Filter : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Show && o->isWidgetType()) {
            auto *w = static_cast<QWidget *>(o);
            const Qt::WindowType t = w->windowType();
            if (w->isWindow() && (t == Qt::Window || t == Qt::Dialog || t == Qt::Sheet || t == Qt::Tool))
                ScreenFit::fit(w);
        }
        return QObject::eventFilter(o, e);
    }
};

} // namespace

namespace ScreenFit {

void install(QApplication &app)
{
    app.installEventFilter(new Filter(&app));
}

void fit(QWidget *w)
{
    if (!w || isQtOwnDialog(w)) return;
    const QRect avail = availableFor(w);
    const QSize maxClient(avail.width() - kFrameW, avail.height() - kFrameH);
    if (auto *d = qobject_cast<QDialog *>(w)) {
        if (QLayout *lay = d->layout()) {
            lay->activate();
            const QSize need = d->minimumSizeHint().expandedTo(d->minimumSize());
            if (need.width() > maxClient.width() || need.height() > maxClient.height())
                wrap(d, maxClient);
        }
    }
    clampIntoScreen(w, avail);
}

}
