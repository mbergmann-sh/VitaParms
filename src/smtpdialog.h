// VitaParms – Einrichtung des E-Mail-Versands (SMTP) für Erinnerungen
// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

class SmtpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SmtpDialog(QWidget *parent = nullptr);

public slots:
    void accept() override;

private:
    void sendTest();
    QLineEdit *m_host = nullptr, *m_user = nullptr, *m_pass = nullptr, *m_from = nullptr;
    QSpinBox *m_port = nullptr;
    QComboBox *m_sec = nullptr;
    QCheckBox *m_details = nullptr;
};
