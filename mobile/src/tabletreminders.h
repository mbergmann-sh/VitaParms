// VitaParms Tablet – Termin-Erinnerungen als Android-Benachrichtigung
//
// Plant für die angemeldete Person (zuständiges Konto) die Erinnerungen der nächsten 14 Tage
// und übergibt sie Android (Java: de.mbergmann.vitaparms.Reminders → AlarmManager). Android
// zeigt sie pünktlich an – auch wenn VitaParms im Hintergrund ist, gesperrt oder beendet wurde.
// Neu geplant wird nach der Anmeldung, nach Änderungen und beim Zurückkehren in die App.
// Auf dem Sperrbildschirm erscheint nur „VitaParms – Termin-Erinnerung“.
// SPDX-License-Identifier: MIT
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

class TabletReminders : public QObject
{
    Q_OBJECT
public:
    struct Item {
        int id = 0;               // Kennung des Weckers (Termin-ID)
        QDateTime at;
        QString title;            // „Termin: Hausarzt“
        QString text;             // „heute 14:30 · Praxis Dr. Sommer · Erika Mustermann“
        bool sound = true;
    };
    explicit TabletReminders(QObject *parent = nullptr);

    // Erinnerungen ab 'now' für die nächsten 'days' Tage (höchstens 'max')
    static QList<Item> plan(const QDateTime &now, int days = 14, int max = 150);

    void scheduleSoon();          // nach Änderungen (gebündelt)
    void scheduleNow();
    void requestPermission();     // Android 13+: Erlaubnis für Benachrichtigungen erfragen
    bool permissionGranted() const;
    int lastCount() const { return m_count; }

private:
    QTimer m_debounce;
    int m_count = 0;
};
