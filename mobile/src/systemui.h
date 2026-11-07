// VitaParms mobile – Anpassung an die Systemleisten (Android 15+: Darstellung bis an den Rand)
//
// Ab Ziel-API 35 zeichnet Android jede App bis unter Statusleiste, Navigationsleiste und
// Kameraaussparung. Diese Klasse meldet die Ränder, die frei bleiben müssen (QWindow::safeAreaMargins,
// ab Qt 6.9), und stellt die Symbole der Systemleisten passend zum hellen/dunklen Design ein.
//
// Qt ab 6.9 polstert den Inhalt eines ApplicationWindow selbst um diese Ränder; dann meldet
// windowPads = true, und die QML-Seite rechnet die Ränder nicht ein zweites Mal ein. Überlagerungen
// (Popups, Hinweise) liegen außerhalb dieser Polsterung und nutzen top/bottom/left/right direkt.
//
// Autopilot: VP_AUTOPILOT_SAFE="oben,rechts,unten,links" (Pixel) täuscht Ränder vor.
// SPDX-License-Identifier: MIT
#pragma once

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QWindow>

class SystemUi : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int top READ top NOTIFY marginsChanged)
    Q_PROPERTY(int bottom READ bottom NOTIFY marginsChanged)
    Q_PROPERTY(int left READ left NOTIFY marginsChanged)
    Q_PROPERTY(int right READ right NOTIFY marginsChanged)
    Q_PROPERTY(bool windowPads READ windowPads CONSTANT)   // Qt polstert den Fensterinhalt schon selbst
public:
    explicit SystemUi(QObject *parent = nullptr);

    void attach(QWindow *window);
    int top() const { return m_margins.top(); }
    int bottom() const { return m_margins.bottom(); }
    int left() const { return m_margins.left(); }
    int right() const { return m_margins.right(); }
    bool windowPads() const;

    // Symbole der Status- und Navigationsleiste: dunkel auf hellem Grund (light = true) oder hell
    Q_INVOKABLE void setLightBars(bool light);
    // Anzahl der Parameter des Signals 'name' von 'obj' (-1: kein solches Signal) – für „Zurück“
    Q_INVOKABLE int signalArity(QObject *obj, const QString &name) const;

signals:
    void marginsChanged();

private:
    void update();

    QPointer<QWindow> m_window;
    QMargins m_margins;
    QMargins m_fake;
    bool m_hasFake = false;
};
