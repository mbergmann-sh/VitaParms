// VitaParms – Medikamente suchen: BtM, Schmerzmittel, Blutverdünner/Antikoagulanzien
// (oder Freitext) im aktuellen Medikamentenplan – mit Dosierung und Einnahmemenge
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "drugclass.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class MedSearchDialog : public QDialog
{
    Q_OBJECT
public:
    // group: DrugClass::Group oder -1 für Freitext
    MedSearchDialog(const Patient &p, const QList<Medication> &meds, int group = DrugClass::Btm, QWidget *parent = nullptr);

    struct Row { Medication med; DrugClass::Hit hit; DrugClass::DailyAmount amount; };
    static QList<Row> search(const QList<Medication> &meds, int group, const QString &text, bool archived);
    int resultCount() const { return int(m_rows.size()); }
    QString html() const;               // für Druck/PDF
    void setGroup(int group);

private:
    void run();
    QString criterionText() const;

    Patient m_patient;
    QList<Medication> m_meds;
    QList<Row> m_rows;
    QComboBox *m_group = nullptr;
    QLineEdit *m_text = nullptr;
    QCheckBox *m_archived = nullptr;
    QLabel *m_summary = nullptr;
    QTableWidget *m_table = nullptr;
};
