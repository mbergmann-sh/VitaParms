// VitaParms – Dialog für einen Eintrag im Medikamentenplan (medicationform.ui)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "drugclass.h"
#include <QDialog>

class MedicationDialog : public QDialog
{
    Q_OBJECT
public:
    // patientId: für das Speichern des Beipackzettels in der Krankenakte (0 = noch nicht gespeichert)
    explicit MedicationDialog(const Medication &m, QWidget *parent = nullptr, qint64 patientId = 0);
    Medication medication() const { return m_med; }

public slots:
    void accept() override;

private:
    void showLeaflet();
    void setupRhythm();
    void updateRhythmUi();
    void readRhythm(Medication &m) const;
    void setupPeriod();
    void fitToScreen();
    class QScrollArea *m_scroll = nullptr;
    void setupReminder();
    void followDoses();
    void readReminder(Medication &m) const;
    void updateReminderUi(int category, int choice);
    bool m_slotsTouched = false;
    void setupClass();
    void refreshClass();
    Medication currentBasics() const;
    bool m_classTouched[DrugClass::GroupCount] = {};
    Medication m_med;
    qint64 m_patientId = 0;
    QWidget   *m_form = nullptr;
};
