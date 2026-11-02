// VitaParms – Verlauf drucken: Zeitraum und Wertegruppen wählen
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QDialog>
#include <QHash>

class QCheckBox;
class QDateEdit;
class QLabel;

class HistoryPrintDialog : public QDialog
{
    Q_OBJECT
public:
    // from/to: Vorschlag (einschließlich); preselect: z. B. die in der Grafik gewählten Bereiche
    HistoryPrintDialog(const Patient &p, const QDate &from, const QDate &to, const QStringList &preselect,
                       const QString &okText, QWidget *parent = nullptr);

    QDate from() const;
    QDate to() const;                         // einschließlich
    QStringList groups() const;               // gewählte Gruppen in fester Reihenfolge
    QList<VitalRecord> records() const;       // Messungen im gewählten Zeitraum
    QString periodText() const;

    void accept() override;

private:
    void updateGroups();

    Patient m_patient;
    QDateEdit *m_from = nullptr, *m_to = nullptr;
    QHash<QString, QCheckBox *> m_checks;
    QStringList m_order;
    QLabel *m_info = nullptr;
};
