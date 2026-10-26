// VitaParms – Protokoll "Medikamente stellen": wann, von wem, was gestellt wurde
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

class MedLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MedLogDialog(const Patient &p, QWidget *parent = nullptr);
    int rowCount() const;
    static QString statusText(const MedPreparation &e);
    static QString itemsHtml(const MedPreparation &e);

private:
    void reload();
    void showDetails();
    void deleteSelected();
    QString periodText() const;
    QDate fromDate() const;

    Patient m_patient;
    QList<MedPreparation> m_log;
    QComboBox *m_period = nullptr;
    QLabel *m_count = nullptr;
    QTableWidget *m_table = nullptr;
    QTextBrowser *m_details = nullptr;
    QPushButton *m_delete = nullptr;
};
