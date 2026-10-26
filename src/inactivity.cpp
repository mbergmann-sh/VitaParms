// SPDX-License-Identifier: MIT
#include "inactivity.h"

#include <QCoreApplication>
#include <QEvent>

InactivityWatcher::InactivityWatcher(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_active && m_minutes > 0) emit idleTimeout();
    });
    QCoreApplication::instance()->installEventFilter(this);
}

void InactivityWatcher::setMinutes(int minutes)
{
    m_minutes = qMax(0, minutes);
    poke();
}

void InactivityWatcher::setActive(bool on)
{
    m_active = on;
    poke();
}

void InactivityWatcher::poke()
{
    if (m_active && m_minutes > 0)
        m_timer.start(m_minutes * 60 * 1000);
    else
        m_timer.stop();
}

bool InactivityWatcher::eventFilter(QObject *obj, QEvent *ev)
{
    switch (ev->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
        // Nur echte Benutzereingaben setzen die Frist zurück (nicht Timer, Repaints …)
        if (m_timer.isActive() && m_timer.remainingTime() < m_minutes * 60 * 1000 - 1000)
            poke();
        break;
    default:
        break;
    }
    return QObject::eventFilter(obj, ev);
}
