// VitaParms – Messwerte → BZ- bzw. RR-Tagesprofil: Zeitpunkte wählen und Erinnerungen für einen Tag stellen
// SPDX-License-Identifier: MIT
#pragma once

#include "glucoseprofile.h"

#include <QDialog>

class QCheckBox;
class QDateEdit;
class QLabel;
class QTimeEdit;

class GlucoseProfileDialog : public QDialog
{
    Q_OBJECT
public:
    GlucoseProfileDialog(const Patient &patient, QWidget *parent = nullptr,
                         GlucoseProfile::Kind kind = GlucoseProfile::Kind::Glucose);
    GlucoseProfile::Kind kind() const { return m_kind; }
    int created() const { return m_created; }

public slots:
    void accept() override;

private:
    void updateInfo();
    QList<GlucoseProfile::Point> collect() const;

    Patient m_patient;
    GlucoseProfile::Kind m_kind;
    QList<GlucoseProfile::Point> m_points;
    QList<QCheckBox *> m_checks;
    QList<QTimeEdit *> m_times;
    QDateEdit *m_date = nullptr;
    QCheckBox *m_sound = nullptr;
    QLabel *m_info = nullptr;
    int m_created = 0;
};
