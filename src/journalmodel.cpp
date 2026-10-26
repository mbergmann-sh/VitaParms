// SPDX-License-Identifier: MIT
#include "journalmodel.h"
#include "formloader.h"

#include <QBrush>
#include <QGuiApplication>
#include <QPalette>
#include <QFont>
#include <QJsonValue>
#include <QLocale>
#include <algorithm>
#include <cmath>

JournalModel::JournalModel(QObject *parent) : QAbstractTableModel(parent)
{
    rebuildColumns();
}

void JournalModel::rebuildColumns()
{
    m_cols.clear();
    m_cols.append({ ColKind::DateTime, VitalKey::Pulse, {}, tr("Datum / Uhrzeit") });
    m_cols.append({ ColKind::Context, VitalKey::Pulse, {}, tr("Messzeitpunkt") });
    m_cols.append({ ColKind::BloodPressure, VitalKey::Systolic, {}, tr("RR\n[mmHg]") });
    for (VitalKey k : { VitalKey::Pulse, VitalKey::Temperature, VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose,
                        VitalKey::Weight, VitalKey::Height, VitalKey::Bmi })
        m_cols.append({ ColKind::Vital, k, {},
                        VitalDefs::shortLabel(k) + QStringLiteral("\n[") + VitalDefs::unit(k) + QLatin1Char(']') });
    for (const QString &e : m_extraKeys)
        m_cols.append({ ColKind::Extra, VitalKey::Pulse, e, FormIO::fieldLabel(e) });
    m_cols.append({ ColKind::Observations, VitalKey::Pulse, {}, tr("Beobachtungen") });
}

void JournalModel::setRecords(const QList<VitalRecord> &records, const QStringList &extraKeys)
{
    beginResetModel();
    m_records = records;
    // Absteigend nach Datum – aktuellste Messung zuerst
    std::stable_sort(m_records.begin(), m_records.end(),
                     [](const VitalRecord &a, const VitalRecord &b) { return a.measuredAt > b.measuredAt; });
    m_extraKeys = extraKeys;
    // Körpergröße je Messung: eigene Angabe, sonst die zuletzt davor gemessene, sonst die zuletzt bekannte
    m_heights.clear();
    std::optional<double> h;
    QList<std::optional<double>> rev;
    for (int i = m_records.size() - 1; i >= 0; --i) {
        if (m_records[i].height) h = m_records[i].height;
        rev.prepend(h ? h : m_fallbackHeight);
    }
    m_heights = rev;
    rebuildColumns();
    endResetModel();
}

PeakFlow::Assessment JournalModel::peakFlowAt(int row) const
{
    const VitalRecord *r = recordAt(row);
    if (!r || !r->peakFlow || !m_hasPatient) return {};
    return PeakFlow::assess(m_patient, *r->peakFlow, r->measuredAt.date(), m_heights.value(row));
}

const VitalRecord *JournalModel::recordAt(int row) const
{
    if (row < 0 || row >= m_records.size()) return nullptr;
    return &m_records[row];
}

int JournalModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

int JournalModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_cols.size();
}

QString JournalModel::extraText(const QJsonValue &v)
{
    if (v.isBool()) return v.toBool() ? tr("ja") : tr("nein");
    if (v.isDouble()) {
        const double d = v.toDouble();
        return d == std::floor(d) ? QString::number(qint64(d)) : QLocale().toString(d, 'g', 6);
    }
    return v.toString();
}

QString JournalModel::cellText(int row, int col) const
{
    const VitalRecord *r = recordAt(row);
    if (!r || col < 0 || col >= m_cols.size()) return {};
    const Column &c = m_cols[col];
    const QLocale loc;
    switch (c.kind) {
    case ColKind::DateTime:
        return loc.toString(r->measuredAt, QStringLiteral("ddd dd.MM.yyyy HH:mm"));
    case ColKind::Context:
        return r->isAdmission ? (r->context.isEmpty() ? tr("Aufnahme") : tr("Aufnahme – %1").arg(r->context))
                              : r->context;
    case ColKind::BloodPressure:
        if (!r->systolic && !r->diastolic) return {};
        return QStringLiteral("%1/%2")
            .arg(r->systolic ? QString::number(*r->systolic) : QStringLiteral("–"),
                 r->diastolic ? QString::number(*r->diastolic) : QStringLiteral("–"));
    case ColKind::Vital:
        return VitalDefs::formatted(*r, c.key);
    case ColKind::Extra:
        return r->extra.contains(c.extraKey) ? extraText(r->extra.value(c.extraKey)) : QString();
    case ColKind::Observations:
        return r->observations;
    }
    return {};
}

QVariant JournalModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    const VitalRecord *r = recordAt(index.row());
    if (!r) return {};
    const Column &c = m_cols[index.column()];

    switch (role) {
    case Qt::DisplayRole: {
        QString t = cellText(index.row(), index.column());
        if (c.kind == ColKind::Observations) t = t.simplified();
        return t;
    }
    case Qt::ToolTipRole:
        if (c.kind == ColKind::Observations && !r->observations.isEmpty())
            return r->observations;
        if (c.kind == ColKind::Vital && c.key == VitalKey::PeakFlow && r->peakFlow) {
            const PeakFlow::Assessment a = peakFlowAt(index.row());
            if (a.zone != PeakFlow::Zone::None)
                return a.text + (a.advice.isEmpty() ? QString() : QStringLiteral("\n") + a.advice)
                       + QStringLiteral("\n") + tr("Bezug: %1").arg(a.ref.description);
        }
        return {};
    case Qt::ForegroundRole:
        if (c.kind == ColKind::Vital && c.key == VitalKey::PeakFlow && r->peakFlow) {
            const PeakFlow::Zone z = peakFlowAt(index.row()).zone;
            if (z == PeakFlow::Zone::Yellow || z == PeakFlow::Zone::Red) return QBrush(PeakFlow::zoneColor(z, QGuiApplication::palette().base().color().lightness() < 128));
        }
        return {};
    case Qt::TextAlignmentRole:
        if (c.kind == ColKind::Vital || c.kind == ColKind::BloodPressure)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::FontRole:
        if (r->isAdmission
            || (c.kind == ColKind::Vital && c.key == VitalKey::PeakFlow && r->peakFlow
                && peakFlowAt(index.row()).zone == PeakFlow::Zone::Red)) { QFont f; f.setBold(true); return f; }
        return {};
    case Qt::UserRole:
        return r->id;
    }
    return {};
}

QVariant JournalModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || section < 0 || section >= m_cols.size()) return {};
    if (role == Qt::DisplayRole) return m_cols[section].header;
    return {};
}
