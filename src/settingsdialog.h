// VitaParms – Einstellungen
// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QComboBox;
class QPlainTextEdit;
class QRadioButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    bool restartRequested() const { return m_restart; }

public slots:
    void accept() override;
    void reject() override;

private:
    QRadioButton   *m_mgdl = nullptr;
    QRadioButton   *m_mmol = nullptr;
    QComboBox      *m_primary[2] = { nullptr, nullptr };
    QPlainTextEdit *m_contexts = nullptr;
    QComboBox      *m_language = nullptr;
    QComboBox      *m_theme = nullptr;
    QComboBox      *m_autoLock = nullptr;
    QComboBox      *m_voice = nullptr;
    class QCheckBox *m_leafletOnline = nullptr;
    class QCheckBox *m_upcomingOnStart = nullptr;
    QCheckBox *m_quarter = nullptr;
    class QSlider  *m_rate = nullptr;
    QString         m_initialTheme;
    bool            m_restart = false;
};
