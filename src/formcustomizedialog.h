// VitaParms – Verwaltung der benutzerveränderlichen Formulare
// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QTreeWidget;
class QPushButton;
class QPlainTextEdit;

class FormCustomizeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FormCustomizeDialog(QWidget *parent = nullptr);

    static QString findDesigner();

private slots:
    void refresh();
    void customize();
    void openInDesigner();
    void checkForm();
    void preview();
    void resetForm();
    void openFolder();
    void updateButtons();

private:
    QString currentName() const;

    QTreeWidget    *m_list = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_customize = nullptr, *m_designer = nullptr, *m_check = nullptr,
                *m_preview = nullptr, *m_reset = nullptr;
};
