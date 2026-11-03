// VitaParms – eine Impfung im Impfpass eintragen oder ändern
// SPDX-License-Identifier: MIT
#pragma once

#include "vaccinations.h"

#include <QDialog>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;

class VaccinationDialog : public QDialog
{
    Q_OBJECT
public:
    // existing: die übrigen Einträge (Vorschläge für Impfung gegen / Präparat / Arzt)
    VaccinationDialog(const Vaccinations::Vaccination &v, const QList<Vaccinations::Vaccination> &existing,
                      const QStringList &doctors, QWidget *parent = nullptr);
    Vaccinations::Vaccination vaccination() const;
    void accept() override;

private:
    Vaccinations::Vaccination m_v;
    QDateEdit *m_date = nullptr;
    QComboBox *m_against = nullptr;
    QComboBox *m_product = nullptr;
    QLineEdit *m_batch = nullptr;
    QComboBox *m_doctor = nullptr;
    QLineEdit *m_note = nullptr;
    QLabel *m_msg = nullptr;
};
