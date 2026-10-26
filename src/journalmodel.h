// VitaParms – Tabellenmodell für Journal / Verlauf
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "vitaldefs.h"
#include "peakflow.h"

#include <QAbstractTableModel>

class JournalModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum class ColKind { DateTime, Context, BloodPressure, Vital, Extra, Observations };
    struct Column {
        ColKind  kind;
        VitalKey key = VitalKey::Pulse;   // für ColKind::Vital
        QString  extraKey;                // für ColKind::Extra
        QString  header;
    };

    explicit JournalModel(QObject *parent = nullptr);

    void setRecords(const QList<VitalRecord> &records, const QStringList &extraKeys);
    // Person für die Peak-Flow-Bewertung (vor setRecords setzen); fallbackHeight = zuletzt bekannte Größe
    void setPatient(const Patient &p, std::optional<double> fallbackHeight) { m_patient = p; m_hasPatient = p.id != 0; m_fallbackHeight = fallbackHeight; }
    PeakFlow::Assessment peakFlowAt(int row) const;
    const QList<VitalRecord> &records() const { return m_records; }
    const VitalRecord *recordAt(int row) const;
    const QList<Column> &columns() const { return m_cols; }
    QStringList extraKeys() const { return m_extraKeys; }

    // Zellinhalt als Text (auch für Druck/Export)
    QString cellText(int row, int col) const;
    static QString extraText(const QJsonValue &v);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    void rebuildColumns();
    QList<VitalRecord> m_records;
    QStringList        m_extraKeys;
    QList<Column>      m_cols;
    Patient            m_patient;
    bool               m_hasPatient = false;
    std::optional<double> m_fallbackHeight;
    QList<std::optional<double>> m_heights;   // je Zeile: Größe zum Messzeitpunkt (für den Sollwert)
};
