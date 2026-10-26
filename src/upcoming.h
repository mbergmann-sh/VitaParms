// VitaParms – anstehende Termine einer Person:
//  * Druck "Erinnerungen" (Benutzer → Drucken → Erinnerungen…)
//  * Hinweisfenster beim Programmstart (Termine heute und morgen)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace Upcoming {

// Offene Termine der Person ab Tagesbeginn von `from` für `days` Tage (0 = alle kommenden).
// Berücksichtigt die Sichtbarkeit (Anwender: nur eigene Termine).
QList<Appointment> list(qint64 patientId, const QDate &from, int days, bool withMedication = true);
bool isMedication(const Appointment &a);

// HTML-Tabelle für den Druck, nach Tagen gegliedert
QString html(const QList<Appointment> &appts, const QString &periodText);
QString periodText(const QDate &from, int days);

// Hinweisfenster beim Start anzeigen? (Einstellung, Standard: ja)
bool showOnStart();
void setShowOnStart(bool on);

} // namespace Upcoming

// Benutzer → Drucken → Erinnerungen…
class RemindersPrintDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RemindersPrintDialog(const Patient &p, QWidget *parent = nullptr, int presetDays = 30);
    QList<Appointment> appointments() const;
    QString periodText() const;

private:
    void output(bool pdf);

    Patient m_patient;
    QComboBox *m_period = nullptr;
    QCheckBox *m_meds = nullptr;
};

// Beim Programmstart: Termine heute und morgen
class StartupAppointmentsDialog : public QDialog
{
    Q_OBJECT
public:
    StartupAppointmentsDialog(const Patient &p, const QList<Appointment> &appts, QWidget *parent = nullptr);

signals:
    void openPlannerRequested();

private:
    Patient m_patient;
};
