// SPDX-License-Identifier: MIT
#include "stockdialog.h"
#include "appsettings.h"
#include "database.h"
#include "reportprinter.h"
#include "session.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace MedStock;

namespace {
enum StockCol { SName, SStock, SDaily, SUntil, SOrderBy, SExpiry, SState, SCount };

QString d(const QDate &x, const char *fmt = "dd.MM.yyyy")
{
    return x.isValid() ? QLocale().toString(x, QString::fromLatin1(fmt)) : QString();
}

QTableWidget *makeTable(QWidget *parent, const QStringList &heads)
{
    auto *t = new QTableWidget(0, int(heads.size()), parent);
    t->setHorizontalHeaderLabels(heads);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->hide();
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

QColor stateColor(int state, bool onOrder)
{
    if (onOrder && state != Ok) return QColor(0x2f, 0x6f, 0xb3);   // bestellt bzw. auf der Liste
    switch (state) {
    case Empty:     return QColor(0xc6, 0x28, 0x28);
    case OrderNow:  return QColor(0xd3, 0x5f, 0x00);
    case OrderSoon: return QColor(0xa6, 0x7c, 0x00);
    default:        return {};
    }
}
} // namespace

StockDialog::StockDialog(const Patient &p, QWidget *parent, Page page)
    : QDialog(parent), m_patient(p)
{
    setWindowTitle(tr("Medikamentenbestand – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    auto *sp = new QWidget(m_tabs), *op = new QWidget(m_tabs);
    sp->setObjectName(QStringLiteral("stockPage"));
    op->setObjectName(QStringLiteral("ordersPage"));
    buildStockPage(sp);
    buildOrdersPage(op);
    m_tabs->addTab(sp, tr("&Bestand"));
    m_tabs->addTab(op, tr("Bestell&ungen"));
    lay->addWidget(m_tabs, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    bb->button(QDialogButtonBox::Close)->setText(tr("Schließen"));
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(&Database::instance(), &Database::stockChanged, this, [this](qint64 pid) { if (pid == m_patient.id) reload(); });
    reload();
    m_tabs->setCurrentIndex(page);
    resize(1050, 640);
}

void StockDialog::buildStockPage(QWidget *page)
{
    auto *v = new QVBoxLayout(page);
    m_stock = makeTable(page, { tr("Medikament"), tr("Bestand"), tr("Bedarf/Tag"), tr("reicht bis"),
                                tr("bestellen bis"), tr("nächster Verfall"), tr("Status") });
    m_stock->setObjectName(QStringLiteral("stockTable"));
    v->addWidget(m_stock, 1);
    m_stockInfo = new QLabel(page);
    m_stockInfo->setWordWrap(true);
    v->addWidget(m_stockInfo);
    auto *h = new QHBoxLayout;
    m_btnReceive = new QPushButton(tr("Packung &einbuchen…"), page);
    m_btnPackages = new QPushButton(tr("&Packungen und Buchungen…"), page);
    m_btnToOrder = new QPushButton(tr("Zur Bestellliste &hinzufügen"), page);
    auto *lead = new QSpinBox(page);
    lead->setObjectName(QStringLiteral("leadDaysSpin"));
    lead->setRange(1, 60);
    lead->setValue(leadDays());
    lead->setSuffix(tr(" Tage"));
    lead->setToolTip(tr("Zeit, die Rezept, Bestellung und Lieferung brauchen. Fällt der Bestelltag auf ein "
                        "Wochenende, gilt der Freitag davor."));
    auto *leadLbl = new QLabel(tr("&Vorlauf:"), page);
    leadLbl->setBuddy(lead);
    connect(lead, &QSpinBox::valueChanged, this, [this](int v) { setLeadDays(v); fillStock(); });
    h->addWidget(m_btnReceive);
    h->addWidget(m_btnPackages);
    h->addWidget(m_btnToOrder);
    h->addStretch();
    // Stell-Rhythmus: täglich oder wöchentlich (der Bestand muss dann am Stelltag für die ganze Woche reichen)
    auto *mode = new QComboBox(page);
    mode->setObjectName(QStringLiteral("prepModeCombo"));
    mode->addItem(tr("täglich"), 1);
    mode->addItem(tr("wöchentlich ab"), 7);
    auto *wd = new QComboBox(page);
    wd->setObjectName(QStringLiteral("prepWeekdayCombo"));
    for (int i = 1; i <= 7; ++i) wd->addItem(QLocale().dayName(i), i);
    const PrepPlan plan = prepPlan(m_patient.id);
    mode->setCurrentIndex(plan.weekly() ? 1 : 0);
    wd->setCurrentIndex(plan.weekday - 1);
    wd->setEnabled(plan.weekly());
    auto applyPlan = [this, mode, wd] {
        wd->setEnabled(mode->currentData().toInt() >= 7);
        setPrepPlan(m_patient.id, PrepPlan{ mode->currentData().toInt(), wd->currentData().toInt() });
        fillStock();
    };
    connect(mode, &QComboBox::activated, this, applyPlan);
    connect(wd, &QComboBox::activated, this, applyPlan);
    auto *modeLbl = new QLabel(tr("&Gestellt wird:"), page);
    modeLbl->setBuddy(mode);
    mode->setToolTip(tr("Wird der Wochendosierer gestellt, muss der Bestand am Stelltag für die ganze Woche reichen."));
    h->addWidget(modeLbl);
    h->addWidget(mode);
    h->addWidget(wd);
    h->addSpacing(12);
    h->addWidget(leadLbl);
    h->addWidget(lead);
    v->addLayout(h);
    connect(m_btnReceive, &QPushButton::clicked, this, [this] {
        if (Medication *m = currentMedication()) receivePackage(this, *m);
    });
    connect(m_btnPackages, &QPushButton::clicked, this, &StockDialog::showPackages);
    connect(m_btnToOrder, &QPushButton::clicked, this, &StockDialog::addCurrentToOrder);
    connect(m_stock, &QTableWidget::itemSelectionChanged, this, &StockDialog::updateButtons);
    connect(m_stock, &QTableWidget::cellDoubleClicked, this, [this] { showPackages(); });
}

void StockDialog::buildOrdersPage(QWidget *page)
{
    auto *v = new QVBoxLayout(page);
    m_orderInfo = new QLabel(page);
    m_orderInfo->setWordWrap(true);
    v->addWidget(m_orderInfo);
    m_orders = makeTable(page, { tr("Erstellt"), tr("Status"), tr("Positionen"), tr("Bemerkung") });
    m_orders->setObjectName(QStringLiteral("ordersTable"));
    v->addWidget(m_orders, 2);
    m_items = makeTable(page, { tr("Präparat"), tr("Stärke"), tr("PZN"), tr("Menge"), tr("Grund"), tr("geliefert") });
    m_items->setObjectName(QStringLiteral("itemsTable"));
    v->addWidget(m_items, 3);
    auto *h = new QHBoxLayout;
    auto *newList = new QPushButton(tr("Bestellliste e&rstellen"), page);
    m_btnConfirm = new QPushButton(tr("Als &bestellt bestätigen…"), page);
    m_btnDeliver = new QPushButton(tr("&Lieferung einbuchen…"), page);
    m_btnAmount = new QPushButton(tr("&Menge ändern…"), page);
    m_btnRemoveItem = new QPushButton(tr("Position &entfernen"), page);
    m_btnDeleteOrder = new QPushButton(tr("Bestellung l&öschen"), page);
    m_btnPrint = new QPushButton(tr("&Drucken…"), page);
    m_btnPdf = new QPushButton(tr("Als &PDF…"), page);
    for (QPushButton *b : { newList, m_btnConfirm, m_btnDeliver, m_btnAmount, m_btnRemoveItem, m_btnDeleteOrder }) h->addWidget(b);
    h->addStretch();
    h->addWidget(m_btnPrint);
    h->addWidget(m_btnPdf);
    v->addLayout(h);
    connect(newList, &QPushButton::clicked, this, &StockDialog::createOrderList);
    connect(m_btnConfirm, &QPushButton::clicked, this, &StockDialog::confirmOrder);
    connect(m_btnDeliver, &QPushButton::clicked, this, &StockDialog::deliverItem);
    connect(m_btnAmount, &QPushButton::clicked, this, &StockDialog::editAmount);
    connect(m_btnRemoveItem, &QPushButton::clicked, this, &StockDialog::removeItem);
    connect(m_btnDeleteOrder, &QPushButton::clicked, this, &StockDialog::deleteOrder);
    connect(m_btnPrint, &QPushButton::clicked, this, [this] { printOrder(false); });
    connect(m_btnPdf, &QPushButton::clicked, this, [this] { printOrder(true); });
    connect(m_orders, &QTableWidget::itemSelectionChanged, this, [this] { fillItems(); updateButtons(); });
    connect(m_items, &QTableWidget::itemSelectionChanged, this, &StockDialog::updateButtons);
}

void StockDialog::reload()
{
    m_meds.clear();
    for (const Medication &m : Database::instance().medications(m_patient.id, false)) m_meds << m;
    fillStock();
    fillOrders();
}

void StockDialog::fillStock()
{
    const qint64 sel = currentMedication() ? currentMedication()->id : 0;
    m_stock->setRowCount(0);
    int warn = 0;
    for (int i = 0; i < m_meds.size(); ++i) {
        const Medication &m = m_meds[i];
        const Forecast f = forecast(m);
        const int r = m_stock->rowCount();
        m_stock->insertRow(r);
        const QString unit = m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit;
        QString name = m.name + (m.strength.isEmpty() ? QString() : QLatin1Char(' ') + m.strength);
        QStringList cells(SCount);
        cells[SName] = name;
        cells[SState] = f.stateText();
        if (f.tracked) {
            cells[SStock] = qtyText(f.stock) + unit + (f.expiredQty > 0 ? tr(" (+%1 verfallen)").arg(qtyText(f.expiredQty)) : QString());
            cells[SDaily] = f.daily > 0 ? qtyText(std::round(f.daily * 100) / 100) : tr("–");
            cells[SUntil] = f.runOut.isValid() ? d(f.runOut.addDays(-1), "ddd dd.MM.yyyy") : (f.daily > 0 ? tr("> 1 Jahr") : tr("–"));
            cells[SOrderBy] = f.orderBy.isValid() ? d(f.orderBy, "ddd dd.MM.yyyy") : QString();
            cells[SExpiry] = d(f.nextExpiry);
            if ((f.state == OrderNow || f.state == Empty) && !f.onOrder) ++warn;
        }
        const QColor col = stateColor(f.state, f.onOrder);
        for (int c = 0; c < SCount; ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            it->setData(Qt::UserRole, i);
            if (!f.tracked) it->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            if (c == SState && col.isValid()) { it->setForeground(col); QFont fo = it->font(); fo.setBold(true); it->setFont(fo); }
            if (c == SStock || c == SDaily) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_stock->setItem(r, c, it);
        }
        if (m.id == sel) m_stock->selectRow(r);
    }
    m_stock->resizeColumnsToContents();
    m_stockInfo->setText(tr("<small>Erfasst werden Medikamente, für die eine Packung eingebucht ist. Nach jedem Stellen "
                            "mit dem Assistenten wird die gestellte Menge abgebucht, zuerst aus der Packung, die am "
                            "frühesten verfällt; verfallene Packungen zählen nicht zum Bestand. „Bestellen bis“ = "
                            "Reichweite minus Vorlauf (%1 Tage), an Wochenenden auf den Freitag davor. Gestellt wird %2%3.</small>")
                             .arg(leadDays())
                             .arg(prepPlanText(prepPlan(m_patient.id)),
                                  prepPlan(m_patient.id).weekly() ? tr(" – der Bestand muss am Stelltag für die ganze Woche reichen") : QString())
                         + (warn ? tr("<br><b>Medikamente jetzt nachbestellen: %1</b>").arg(warn) : QString()));
    updateButtons();
}

void StockDialog::fillOrders()
{
    const qint64 sel = currentOrder() ? currentOrder()->id : 0;
    m_orderList = orders(m_patient.id);
    m_orders->setRowCount(0);
    int selRow = m_orderList.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_orderList.size(); ++i) {
        const Order &o = m_orderList[i];
        int open = 0;
        for (const OrderItem &it : o.items) open += !it.delivered;
        const QStringList cells = { QLocale().toString(o.createdAt, QStringLiteral("dd.MM.yyyy HH:mm")), o.statusText(),
                                    tr("%1 (offen: %2)").arg(o.items.size()).arg(open), o.note };
        m_orders->insertRow(i);
        for (int c = 0; c < cells.size(); ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            if (o.status == Open && c == 1) { it->setForeground(QColor(0xd3, 0x5f, 0x00)); QFont f = it->font(); f.setBold(true); it->setFont(f); }
            m_orders->setItem(i, c, it);
        }
        if (o.id == sel) selRow = i;
    }
    m_orders->resizeColumnsToContents();
    if (selRow >= 0) m_orders->selectRow(selRow);
    fillItems();
    const bool anyOpen = std::any_of(m_orderList.begin(), m_orderList.end(), [](const Order &o) { return o.status == Open; });
    m_orderInfo->setText(anyOpen
        ? tr("<b>Eine Bestellliste ist noch nicht als bestellt bestätigt.</b> Rezept besorgen, bestellen und dann "
             "„Als bestellt bestätigen“ – bis dahin erinnert VitaParms daran.")
        : tr("<small>Bestelllisten bleiben hier gespeichert und lassen sich jederzeit erneut drucken oder als PDF speichern.</small>"));
    updateButtons();
}

void StockDialog::fillItems()
{
    m_items->setRowCount(0);
    const auto o = currentOrder();
    if (!o) return;
    for (int i = 0; i < o->items.size(); ++i) {
        const OrderItem &it = o->items[i];
        const QStringList cells = { it.name, it.strength, it.pzn, it.amount, it.reason,
                                    it.delivered ? tr("%1 (%2 Stück)").arg(d(it.deliveredAt), qtyText(it.deliveredQty)) : QString() };
        m_items->insertRow(i);
        for (int c = 0; c < cells.size(); ++c) m_items->setItem(i, c, new QTableWidgetItem(cells[c]));
    }
    m_items->resizeColumnsToContents();
}

void StockDialog::selectOrderRow(int row)
{
    m_orders->selectRow(row);
}

Medication *StockDialog::currentMedication()
{
    if (!m_stock) return nullptr;
    const auto rows = m_stock->selectionModel() ? m_stock->selectionModel()->selectedRows() : QModelIndexList{};
    if (rows.isEmpty()) return nullptr;
    const int i = m_stock->item(rows.first().row(), 0)->data(Qt::UserRole).toInt();
    return i >= 0 && i < m_meds.size() ? &m_meds[i] : nullptr;
}

std::optional<Order> StockDialog::currentOrder()
{
    if (!m_orders || !m_orders->selectionModel()) return std::nullopt;
    const auto rows = m_orders->selectionModel()->selectedRows();
    if (rows.isEmpty() || rows.first().row() >= m_orderList.size()) return std::nullopt;
    return m_orderList[rows.first().row()];
}

void StockDialog::updateButtons()
{
    const bool med = currentMedication() != nullptr;
    m_btnReceive->setEnabled(med);
    m_btnPackages->setEnabled(med);
    m_btnToOrder->setEnabled(med);
    const auto o = currentOrder();
    const int item = m_items->selectionModel() && !m_items->selectionModel()->selectedRows().isEmpty()
                         ? m_items->selectionModel()->selectedRows().first().row() : -1;
    const bool itemOpen = o && item >= 0 && item < o->items.size() && !o->items[item].delivered;
    m_btnConfirm->setEnabled(o && o->status == Open && !o->items.isEmpty());
    m_btnDeliver->setEnabled(o && o->status != Delivered && itemOpen);
    m_btnAmount->setEnabled(o && o->status == Open && itemOpen);
    m_btnRemoveItem->setEnabled(o && o->status == Open && itemOpen);
    m_btnDeleteOrder->setEnabled(o && o->status == Open);
    m_btnPrint->setEnabled(o && !o->items.isEmpty());
    m_btnPdf->setEnabled(o && !o->items.isEmpty());
}

// ------------------------------------------------------------------ Packungen

bool StockDialog::receivePackage(QWidget *parent, const Medication &m, double defaultQty)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Packung einbuchen"));
    auto *form = new QFormLayout(&dlg);
    form->addRow(new QLabel(QStringLiteral("<b>%1</b>").arg((m.name + QLatin1Char(' ') + m.strength).trimmed().toHtmlEscaped()), &dlg));
    auto *qty = new QDoubleSpinBox(&dlg);
    qty->setObjectName(QStringLiteral("qtySpin"));
    qty->setDecimals(1);
    qty->setRange(0.5, 100000);
    qty->setValue(defaultQty > 0 ? defaultQty : 100);
    qty->setSuffix(m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit);
    form->addRow(tr("&Menge (Stück)"), qty);
    auto *hasExp = new QCheckBox(tr("&Verfällt am"), &dlg);
    hasExp->setChecked(true);
    auto *exp = new QDateEdit(QDate::currentDate().addYears(2), &dlg);
    exp->setObjectName(QStringLiteral("expiryEdit"));
    exp->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    exp->setCalendarPopup(true);
    connect(hasExp, &QCheckBox::toggled, exp, &QWidget::setEnabled);
    form->addRow(hasExp, exp);
    auto *note = new QLineEdit(&dlg);
    note->setPlaceholderText(tr("z. B. Apotheke, Charge, N3"));
    form->addRow(tr("&Notiz"), note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Einbuchen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return false;
    Package p;
    p.quantity = qty->value();
    p.expiry = hasExp->isChecked() ? exp->date() : QDate();
    p.note = note->text().trimmed();
    if (p.expiry.isValid() && p.expiry < QDate::currentDate()
        && QMessageBox::question(parent, tr("Packung einbuchen"),
                                 tr("Das Verfallsdatum liegt in der Vergangenheit – die Packung zählt nicht zum Bestand. Trotzdem einbuchen?"))
               != QMessageBox::Yes)
        return false;
    QString err;
    if (!addPackage(m, p, p.note, &err)) {
        QMessageBox::warning(parent, tr("Packung einbuchen"), err);
        return false;
    }
    return true;
}

void StockDialog::showPackages()
{
    Medication *mp = currentMedication();
    if (!mp) return;
    const Medication m = *mp;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Packungen und Buchungen – %1").arg(m.name));
    auto *v = new QVBoxLayout(&dlg);
    auto *pt = makeTable(&dlg, { tr("Menge"), tr("Verfällt"), tr("Eingang"), tr("Notiz") });
    pt->setObjectName(QStringLiteral("packagesTable"));
    auto *mt = makeTable(&dlg, { tr("Zeitpunkt"), tr("Art"), tr("Menge"), tr("für den"), tr("von"), tr("Notiz") });
    QList<Package> pk;
    auto fill = [&] {
        pk = packages(m.id, false);
        pt->setRowCount(0);
        for (int i = 0; i < pk.size(); ++i) {
            const Package &p = pk[i];
            pt->insertRow(i);
            auto *q = new QTableWidgetItem(qtyText(p.quantity));
            auto *e = new QTableWidgetItem(p.expiry.isValid() ? d(p.expiry) + (p.expiredOn(QDate::currentDate()) ? tr(" – verfallen") : QString()) : tr("ohne"));
            if (p.expiredOn(QDate::currentDate())) e->setForeground(QColor(0xc6, 0x28, 0x28));
            pt->setItem(i, 0, q);
            pt->setItem(i, 1, e);
            pt->setItem(i, 2, new QTableWidgetItem(d(p.received)));
            pt->setItem(i, 3, new QTableWidgetItem(p.note));
        }
        pt->resizeColumnsToContents();
        const QStringList kinds = { tr("gestellt"), tr("Eingang"), tr("Korrektur"), tr("entsorgt") };
        const QList<Move> mv = moves(m.id);
        mt->setRowCount(0);
        for (int i = 0; i < mv.size(); ++i) {
            const Move &x = mv[i];
            mt->insertRow(i);
            const QStringList c = { QLocale().toString(x.at, QStringLiteral("dd.MM.yyyy HH:mm")), kinds.value(x.kind),
                                    (x.quantity > 0 ? QStringLiteral("+") : QString()) + qtyText(x.quantity), d(x.forDate), x.by, x.note };
            for (int k = 0; k < c.size(); ++k) mt->setItem(i, k, new QTableWidgetItem(c[k]));
        }
        mt->resizeColumnsToContents();
    };
    v->addWidget(new QLabel(tr("<b>Packungen</b> (zuerst verbraucht wird die oberste)"), &dlg));
    v->addWidget(pt, 1);
    auto *h = new QHBoxLayout;
    auto *add = new QPushButton(tr("Packung &einbuchen…"), &dlg);
    auto *edit = new QPushButton(tr("&Zählen/Korrigieren…"), &dlg);
    auto *disc = new QPushButton(tr("&Entsorgen"), &dlg);
    h->addWidget(add);
    h->addWidget(edit);
    h->addWidget(disc);
    h->addStretch();
    v->addLayout(h);
    v->addWidget(new QLabel(tr("<b>Buchungen</b>"), &dlg));
    v->addWidget(mt, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    bb->button(QDialogButtonBox::Close)->setText(tr("Schließen"));
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    auto current = [&]() -> std::optional<Package> {
        const auto r = pt->selectionModel()->selectedRows();
        if (r.isEmpty() || r.first().row() >= pk.size()) return std::nullopt;
        return pk[r.first().row()];
    };
    connect(add, &QPushButton::clicked, &dlg, [&] { if (receivePackage(&dlg, m)) fill(); });
    connect(edit, &QPushButton::clicked, &dlg, [&] {
        const auto p = current();
        if (!p) return;
        QDialog e(&dlg);
        e.setWindowTitle(tr("Bestand korrigieren"));
        auto *f = new QFormLayout(&e);
        auto *q = new QDoubleSpinBox(&e);
        q->setDecimals(1);
        q->setRange(0, 100000);
        q->setValue(p->quantity);
        f->addRow(tr("Gezählte &Menge"), q);
        auto *x = new QDateEdit(p->expiry.isValid() ? p->expiry : QDate::currentDate().addYears(2), &e);
        x->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        x->setCalendarPopup(true);
        auto *hx = new QCheckBox(tr("&Verfällt am"), &e);
        hx->setChecked(p->expiry.isValid());
        x->setEnabled(p->expiry.isValid());
        connect(hx, &QCheckBox::toggled, x, &QWidget::setEnabled);
        f->addRow(hx, x);
        auto *n = new QLineEdit(p->note, &e);
        f->addRow(tr("&Notiz"), n);
        auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &e);
        b->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
        connect(b, &QDialogButtonBox::accepted, &e, &QDialog::accept);
        connect(b, &QDialogButtonBox::rejected, &e, &QDialog::reject);
        f->addRow(b);
        if (e.exec() != QDialog::Accepted) return;
        QString err;
        if (!changePackage(*p, q->value(), hx->isChecked() ? x->date() : QDate(), n->text().trimmed(), &err))
            QMessageBox::warning(&dlg, dlg.windowTitle(), err);
        fill();
    });
    connect(disc, &QPushButton::clicked, &dlg, [&] {
        const auto p = current();
        if (!p) return;
        if (QMessageBox::question(&dlg, dlg.windowTitle(), tr("%1 Stück aus dieser Packung als entsorgt ausbuchen?").arg(qtyText(p->quantity)))
            != QMessageBox::Yes) return;
        QString err;
        if (!discardPackage(*p, &err)) QMessageBox::warning(&dlg, dlg.windowTitle(), err);
        fill();
    });
    fill();
    dlg.resize(760, 560);
    dlg.exec();
    reload();
}

// ------------------------------------------------------------------ Bestellungen

void StockDialog::addCurrentToOrder()
{
    Medication *m = currentMedication();
    if (!m) return;
    const Forecast f = forecast(*m);
    QString err;
    if (!addToOpenOrder(m_patient.id, *m, f.tracked ? f.reason(m->unit) : tr("von Hand hinzugefügt"), &err))
        QMessageBox::warning(this, windowTitle(), err);
    reload();
    m_tabs->setCurrentIndex(OrdersPage);
}

void StockDialog::createOrderList()
{
    const Order o = updateOpenOrder(m_patient.id);
    reload();
    if (!o.id)
        QMessageBox::information(this, windowTitle(), tr("Zurzeit muss kein Medikament nachbestellt werden."));
}

void StockDialog::confirmOrder()
{
    const auto o = currentOrder();
    if (!o || o->status != Open) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Bestellung bestätigen"));
    auto *f = new QFormLayout(&dlg);
    f->addRow(new QLabel(tr("Die Bestellung mit %1 Position(en) wurde aufgegeben. Danach erinnert VitaParms nicht mehr daran.")
                             .arg(o->items.size()), &dlg));
    auto *at = new QDateEdit(QDate::currentDate(), &dlg);
    at->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    at->setCalendarPopup(true);
    at->setMaximumDate(QDate::currentDate());
    f->addRow(tr("Bestellt &am"), at);
    auto *note = new QLineEdit(&dlg);
    note->setPlaceholderText(tr("z. B. Rezept bei Dr. … angefordert, bestellt bei … Apotheke, Abholung Mi"));
    note->setMinimumWidth(420);
    f->addRow(tr("&Bemerkung"), note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Bestätigen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    f->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    const QDateTime when(at->date(), at->date() == QDate::currentDate() ? QTime::currentTime() : QTime(12, 0));
    if (!confirmOrdered(o->id, when, Session::user(), note->text().trimmed(), &err))
        QMessageBox::warning(this, windowTitle(), err);
    reload();
}

void StockDialog::deliverItem()
{
    const auto o = currentOrder();
    const auto rows = m_items->selectionModel()->selectedRows();
    if (!o || rows.isEmpty()) return;
    const OrderItem it = o->items.value(rows.first().row());
    if (it.delivered) return;
    Medication m;
    for (const Medication &x : Database::instance().medications(m_patient.id, true)) if (x.id == it.medicationId) m = x;
    if (!m.id) {
        QMessageBox::information(this, windowTitle(), tr("Das Medikament steht nicht mehr im Medikamentenplan."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Lieferung einbuchen"));
    auto *f = new QFormLayout(&dlg);
    f->addRow(new QLabel(QStringLiteral("<b>%1</b> – %2").arg((it.name + QLatin1Char(' ') + it.strength).trimmed().toHtmlEscaped(),
                                                              it.amount.toHtmlEscaped()), &dlg));
    auto *qty = new QDoubleSpinBox(&dlg);
    qty->setDecimals(1);
    qty->setRange(0.5, 100000);
    qty->setValue(100);
    qty->setSuffix(m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit);
    f->addRow(tr("Gelieferte &Menge"), qty);
    auto *exp = new QDateEdit(QDate::currentDate().addYears(2), &dlg);
    exp->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    exp->setCalendarPopup(true);
    auto *hx = new QCheckBox(tr("&Verfällt am"), &dlg);
    hx->setChecked(true);
    connect(hx, &QCheckBox::toggled, exp, &QWidget::setEnabled);
    f->addRow(hx, exp);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Einbuchen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    f->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    QString err;
    if (!MedStock::deliverItem(it, qty->value(), hx->isChecked() ? exp->date() : QDate(), &err))
        QMessageBox::warning(this, windowTitle(), err);
    reload();
}

void StockDialog::editAmount()
{
    const auto o = currentOrder();
    const auto rows = m_items->selectionModel()->selectedRows();
    if (!o || rows.isEmpty()) return;
    const OrderItem it = o->items.value(rows.first().row());
    bool ok = false;
    const QString a = QInputDialog::getText(this, tr("Menge ändern"), tr("Menge für %1 (z. B. „1 Packung N3“, „100 Stück“):").arg(it.name),
                                            QLineEdit::Normal, it.amount, &ok);
    if (!ok || a.trimmed().isEmpty()) return;
    updateItemAmount(it.id, a.trimmed());
    // als Vorschlag für künftige Bestellungen merken
    for (Medication m : Database::instance().medications(m_patient.id, true))
        if (m.id == it.medicationId) {
            m.extra[QStringLiteral("orderAmount")] = a.trimmed();
            Database::instance().updateMedicationLeaflet(m.id, m.pzn, m.extra);
        }
    reload();
}

void StockDialog::removeItem()
{
    const auto o = currentOrder();
    const auto rows = m_items->selectionModel()->selectedRows();
    if (!o || rows.isEmpty()) return;
    MedStock::removeItem(o->items.value(rows.first().row()).id);
    reload();
}

void StockDialog::deleteOrder()
{
    const auto o = currentOrder();
    if (!o || o->status != Open) return;
    if (QMessageBox::question(this, windowTitle(), tr("Diese offene Bestellliste löschen?")) != QMessageBox::Yes) return;
    MedStock::deleteOrder(o->id);
    reload();
}

void StockDialog::printOrder(bool pdf)
{
    const auto o = currentOrder();
    if (!o) return;
    ReportPrinter::orderList(this, m_patient, *o, pdf ? ReportPrinter::Target::Pdf : ReportPrinter::Target::Preview);
}

// ------------------------------------------------------------------ Erinnerung

bool StockDialog::remind(QWidget *parent, const Patient &p, const QStringList &shortage)
{
    const Order open = updateOpenOrder(p.id);
    QStringList lines = reminderLines(open);
    QStringList extra;
    for (const QString &s : shortage)
        extra << tr("Medikament %1: Der Bestand reichte beim Stellen nicht – bitte Bestand prüfen.").arg(s);
    if (lines.isEmpty() && extra.isEmpty()) return false;
    QMessageBox box(QMessageBox::Warning, tr("Medikamente nachbestellen – %1").arg(p.displayName()),
                    (extra + lines).join(QLatin1Char('\n')), QMessageBox::NoButton, parent);
    box.setObjectName(QStringLiteral("stockReminder"));
    if (!lines.isEmpty())
        box.setInformativeText(tr("Die Bestellliste ist unter „Medikamentenbestand → Bestellungen“ gespeichert. "
                                  "Bis die Bestellung als bestellt bestätigt ist, erinnert VitaParms daran."));
    QPushButton *openBtn = box.addButton(tr("Bestellliste &öffnen…"), QMessageBox::AcceptRole);
    box.addButton(tr("&Später"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == openBtn) {
        StockDialog d(p, parent, OrdersPage);
        d.exec();
    }
    return true;
}
