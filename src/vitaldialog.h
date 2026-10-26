// VitaParms – Dialog zur Erfassung von Vitalwerten (Formular: vitalform.ui)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include <QDialog>
#include <QElapsedTimer>

class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

class VitalDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode {
        Journal,    // speichert direkt in die Datenbank, "weitere Messung" möglich
        Admission   // Aufnahmewerte für das Stammblatt, gibt nur den Datensatz zurück
    };

    VitalDialog(const Patient &patient, const VitalRecord &record, Mode mode,
                QWidget *parent = nullptr);

    VitalRecord record() const { return m_record; }
    int savedCount() const { return m_savedCount; }

public slots:
    void accept() override;

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private slots:
    void updateBmi();
    void updatePeakFlow();
    void saveAndNext();

private:
    void loadRecord(const VitalRecord &r);
    bool collect(VitalRecord &r, bool interactive);
    bool store();
    void configureGlucose();
    void setupPeakFlow();              // Hinweis zum Sollwert/Bestwert unter „Peak Flow“
    void warnPeakFlow(const VitalRecord &r);
    void setupBreathCounter();         // 1-Minuten-Zähluhr neben "Atemfrequenz"
    void startBreathCount();
    void stopBreathCount(bool finished);
    void updateBreathCount();

    Patient     m_patient;
    VitalRecord m_record;
    Mode        m_mode;
    QWidget    *m_form = nullptr;
    QPushButton *m_nextButton = nullptr;
    std::optional<double> m_fallbackHeight;
    bool m_heightPrefilled = false;
    int m_savedCount = 0;
    QLabel      *m_peakFlowInfo = nullptr;
    QSpinBox    *m_respSpin = nullptr;
    QPushButton *m_countButton = nullptr;
    QTimer      *m_countTimer = nullptr;
    QElapsedTimer m_countClock;
};
