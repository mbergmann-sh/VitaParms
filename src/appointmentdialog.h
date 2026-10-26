// VitaParms – Termin anlegen/bearbeiten (Formular: appointmentform.ui)
// SPDX-License-Identifier: MIT
#pragma once

#include "apptcore.h"

#include <QColor>
#include <QDialog>
#include <optional>

namespace ApptUi {
// Rückfragen (Widgets); die übrige Termin-Logik steht in apptcore.h
SeriesScope askSeriesScope(QWidget *parent, const Appointment &a, bool deleting, bool allowOnlyThis = true);
// Löschen mit Rückfrage (auch Serien); true = gelöscht
bool deleteWithPrompt(QWidget *parent, const Appointment &a);
}

class AppointmentDialog : public QDialog
{
    Q_OBJECT
public:
    AppointmentDialog(const Appointment &a, QWidget *parent = nullptr);
    Appointment appointment() const { return m_appt; }

public slots:
    void accept() override;

private:
    void updateEnabled();
    void setupRecurrence();
    void updateRecurrenceUi();
    void fillMonthModes(const QDate &start);
    void onStartDateChanged(const QDate &newDate);
    Recurrence readRecurrence(const QDate &start) const;
    bool hasRecurrenceUi() const;
    bool saveSeriesAware(Appointment &a, QString *err, bool *cancelled);

    Appointment m_appt;
    QWidget *m_form = nullptr;
    std::optional<AppointmentSeries> m_series;
    Recurrence m_origRule;
    QDate m_prevStart;
    bool m_updating = false;
};
