// VitaParms – Terminkollisions-Check (Dialog, nur VitaParms – nicht light)
// Personen und Zeitraum wählen, prüfen, Ergebnis mit Ausweichterminen anzeigen und drucken.
// SPDX-License-Identifier: MIT
#pragma once

#include "apptconflicts.h"

#include <QDialog>

class QCheckBox;
class QDateEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimeEdit;

class ApptConflictDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ApptConflictDialog(qint64 currentPatient, QWidget *parent = nullptr);

    void runCheck();                                   // „Prüfen“
    const ApptConflicts::Result &result() const { return m_result; }
    ApptConflicts::Options options() const;

private:
    void editMoved(int row);                           // Doppelklick: Termin zum Verschieben öffnen
    void updatePrintButtons();

    QListWidget *m_persons = nullptr;
    QDateEdit *m_from = nullptr, *m_to = nullptr;
    QSpinBox *m_buffer = nullptr;
    QTimeEdit *m_dayStart = nullptr, *m_dayEnd = nullptr;
    QCheckBox *m_weekdays = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_print = nullptr, *m_pdf = nullptr;
    ApptConflicts::Result m_result;
    ApptConflicts::Options m_lastOptions;
    bool m_checked = false;
};
