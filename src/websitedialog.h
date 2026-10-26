// VitaParms – Einstellungen der Website (Adresse, Master-Passwort, Upload-Schlüssel)
//              und des Datenbank-Abgleichs mit anderen Geräten
// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class WebsiteDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WebsiteDialog(QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    bool applyToConfig(bool showErrors);
    void updateState();
    void setBusy(bool busy, const QString &text = {});

    QCheckBox *m_enabled = nullptr;
    QLineEdit *m_url = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_repeat = nullptr;
    QCheckBox *m_show = nullptr;
    QLineEdit *m_key = nullptr;
    QCheckBox *m_auto = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_test = nullptr;
    QPushButton *m_publish = nullptr;
    QPushButton *m_open = nullptr;
    // Abgleich mit anderen Geräten (Tablet)
    QCheckBox *m_syncEnabled = nullptr;
    QCheckBox *m_syncAuto = nullptr;
    QPushButton *m_syncNow = nullptr;
    QLabel *m_syncStatus = nullptr;
};
