// VitaParms – einen Kontakt bearbeiten (Stammblatt → Weitere Angaben: Kontakte)
// SPDX-License-Identifier: MIT
#pragma once

#include "contacts.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

class ContactDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ContactDialog(const Contacts::Contact &c, QWidget *parent = nullptr);
    Contacts::Contact contact() const;

public slots:
    void accept() override;

private:
    void updateKind();

    Contacts::Contact m_contact;
    QComboBox *m_kind = nullptr;
    QLineEdit *m_name = nullptr, *m_specialty = nullptr, *m_person = nullptr, *m_insuranceNo = nullptr;
    QLineEdit *m_street = nullptr, *m_zip = nullptr, *m_city = nullptr;
    QLineEdit *m_phone = nullptr, *m_mobile = nullptr, *m_email = nullptr, *m_website = nullptr;
    QPlainTextEdit *m_note = nullptr;
    QLabel *m_nameLabel = nullptr, *m_specialtyLabel = nullptr, *m_personLabel = nullptr, *m_insuranceLabel = nullptr;
    QLabel *m_msg = nullptr;
};
