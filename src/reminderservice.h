// VitaParms – Termin-Erinnerungen (optisch, akustisch, per E-Mail)
// Prüft alle 20 Sekunden die fälligen Erinnerungen des angemeldeten Kontos.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QComboBox;

class ReminderPopup : public QWidget
{
    Q_OBJECT
public:
    explicit ReminderPopup(const Appointment &a, QWidget *parent = nullptr);
    qint64 appointmentId() const { return m_id; }

signals:
    void openRequested(qint64 id);
    void measureRequested(qint64 id);     // BZ-Tagesprofil: Messung erfassen

private:
    qint64 m_id;
    QComboBox *m_snooze = nullptr;
};

// Erinnerung an den Quartalswechsel (KV-Karte einlesen lassen) – siehe quarterreminder.h
class QuarterPopup : public QWidget
{
    Q_OBJECT
public:
    QuarterPopup(const QDate &quarterBegin, QWidget *parent = nullptr);
};

class ReminderService : public QObject
{
    Q_OBJECT
public:
    explicit ReminderService(QObject *parent = nullptr);
    void setEnabled(bool on);
    void checkNow();
    void closeAllPopups();
    int  openPopups() const;

    static QString mailSubject(const Appointment &a);
    static QString mailBody(const Appointment &a, bool details);

    void checkQuarter();                 // Quartalswechsel (auch beim Programmstart)

signals:
    void openRequested(qint64 id);
    void measureRequested(qint64 id);
    void statusMessage(const QString &text);

private:
    void fire(const Appointment &a);
    QTimer m_timer;
    QDateTime m_lastExtend;
    QHash<qint64, QPointer<ReminderPopup>> m_popups;
    QPointer<QuarterPopup> m_quarter;
    QString m_quarterShown;              // in dieser Sitzung schon gezeigt (Schlüssel + Tag)
};
