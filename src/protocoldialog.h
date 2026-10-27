// VitaParms – Ausgabe von Stuhlprotokoll und BZ-Tagesprofilen der aktiven Person
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "protocols.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QSpinBox;
class QTextBrowser;

class ProtocolDialog : public QDialog
{
    Q_OBJECT
public:
    ProtocolDialog(Protocols::Kind kind, const Patient &p, QWidget *parent = nullptr);
    const Protocols::Table &table() const { return m_table; }
    Protocols::Options options() const;
    void setPeriodDays(int days);       // 0 = von – bis

private:
    void reload();

    Protocols::Kind  m_kind;
    Patient          m_patient;
    Protocols::Table m_table;
    QComboBox   *m_period = nullptr;
    QDateEdit   *m_from = nullptr;
    QDateEdit   *m_to = nullptr;
    QCheckBox   *m_emptyDays = nullptr;
    QSpinBox    *m_minValues = nullptr;
    QLabel      *m_summary = nullptr;
    QTextBrowser *m_view = nullptr;
};
