// VitaParms – automatische Sperre bei Nichtbenutzung
// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QTimer>

class InactivityWatcher : public QObject
{
    Q_OBJECT
public:
    explicit InactivityWatcher(QObject *parent = nullptr);
    void setMinutes(int minutes);      // 0 = aus
    int minutes() const { return m_minutes; }
    void setActive(bool on);           // z. B. während der Anmeldung aus
    void poke();                       // Aktivität melden

signals:
    void idleTimeout();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    QTimer m_timer;
    int m_minutes = 0;
    bool m_active = true;
};
