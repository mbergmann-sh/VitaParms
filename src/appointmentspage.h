// VitaParms – Reiter "Termine": Planer je Benutzer, Administratoren sehen alle Termine
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QAbstractTableModel>
#include <QWidget>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QTableView;

class AppointmentModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Col { ColWhen, ColPriority, ColTitle, ColPatient, ColLocation, ColOwner, ColReminder, ColCount };
    using QAbstractTableModel::QAbstractTableModel;
    void setAppointments(const QList<Appointment> &l);
    const Appointment *at(int row) const;
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : int(m_items.size()); }
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : ColCount; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    QList<Appointment> m_items;
    QHash<qint64, QString> m_patientNames;
    QHash<qint64, QString> m_seriesText;
};

class AppointmentsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AppointmentsPage(QWidget *parent = nullptr);
    void setCurrentPatient(qint64 id) { m_currentPatient = id; }
    AppointmentModel *model() const { return m_model; }

public slots:
    void reload();
    void newAppointment();
    void editAppointment();
    void editAppointmentId(qint64 id);
    void deleteAppointment();
    void toggleDone();
    void exportICal();

    void measureSelected();              // offener Tagesprofil-Termin: Messung erfassen

signals:
    void changed();
    void measureRequested(qint64 appointmentId);

private:
    void updateQuarterBar();
    void rangeFor(QDateTime &from, QDateTime &to) const;
    void markCalendar();
    const Appointment *selected() const;
    void updateButtons();

    qint64 m_currentPatient = 0;
    QCalendarWidget *m_cal = nullptr;
    QComboBox *m_view = nullptr, *m_patient = nullptr, *m_owner = nullptr;
    QCheckBox *m_showDone = nullptr;
    QTableView *m_table = nullptr;
    AppointmentModel *m_model = nullptr;
    QLabel *m_info = nullptr;
    class QFrame *m_quarterBar = nullptr;
    QLabel *m_quarterText = nullptr;
    QList<QWidget *> m_needSel;
    class QPushButton *m_measureBtn = nullptr;
    QList<QDate> m_marked;
};
