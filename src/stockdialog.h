// VitaParms – Bestandsliste Medikamente und Bestellungen (Desktop)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "medstock.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

class StockDialog : public QDialog
{
    Q_OBJECT
public:
    enum Page { StockPage = 0, OrdersPage };
    explicit StockDialog(const Patient &p, QWidget *parent = nullptr, Page page = StockPage);

    // Packung einbuchen (auch aus anderen Dialogen); true = gebucht
    static bool receivePackage(QWidget *parent, const Medication &m, double defaultQty = 0);
    // Erinnerung nach dem Stellen bzw. beim Start: offene Bestellung zeigen. Liefert true,
    // wenn eine Erinnerung angezeigt wurde.
    static bool remind(QWidget *parent, const Patient &p, const QStringList &shortage = {});

    // für Tests
    QTableWidget *stockTable() const { return m_stock; }
    QTableWidget *orderTable() const { return m_orders; }
    QTableWidget *itemTable() const { return m_items; }
    void selectOrderRow(int row);

public slots:
    void reload();

private:
    void buildStockPage(QWidget *page);
    void buildOrdersPage(QWidget *page);
    void fillStock();
    void fillOrders();
    void fillItems();
    void updateButtons();
    Medication *currentMedication();
    std::optional<MedStock::Order> currentOrder();
    void showPackages();
    void addCurrentToOrder();
    void createOrderList();
    void confirmOrder();
    void deliverItem();
    void editAmount();
    void removeItem();
    void deleteOrder();
    void printOrder(bool pdf);

    Patient m_patient;
    QList<Medication> m_meds;
    QList<MedStock::Order> m_orderList;
    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_stock = nullptr, *m_orders = nullptr, *m_items = nullptr;
    QLabel *m_stockInfo = nullptr, *m_orderInfo = nullptr;
    QPushButton *m_btnReceive = nullptr, *m_btnPackages = nullptr, *m_btnToOrder = nullptr;
    QPushButton *m_btnConfirm = nullptr, *m_btnDeliver = nullptr, *m_btnAmount = nullptr, *m_btnRemoveItem = nullptr,
                *m_btnDeleteOrder = nullptr, *m_btnPrint = nullptr, *m_btnPdf = nullptr;
};
