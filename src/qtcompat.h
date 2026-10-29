// VitaParms – Hilfen, damit derselbe Code mit Qt 5.15 und Qt 6 übersetzt.
//
// Qt 5.15 ist die letzte Qt-Version für ältere macOS-Versionen (10.13–10.15). Unterschiede, die
// hier ausgeglichen werden:
//   * QVariant::typeId() (Qt 6)                → vpTypeId()
//   * QVariant::isNull(): Qt 5 meldet auch einen leeren QString als „null“ → vpIsNull()
//   * QMouseEvent::position()/globalPosition() → vpPos()/vpGlobalPos()
//   * QList(n, wert) (Qt 6)                     → vpFilledList()
//   * QLibraryInfo::path() (Qt 6)               → vpLibraryPath()
//   * Qt::SingleShotConnection (Qt 6)           → vpConnectOnce()
// Überladene Signale (QComboBox::currentIndexChanged, QSpinBox::valueChanged …) werden im Code
// mit qOverload<…>() eindeutig gemacht – das gilt in beiden Versionen.
// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>
#include <QLibraryInfo>
#include <QList>
#include <QMouseEvent>
#include <QObject>
#include <QPointF>
#include <QVariant>

#include <memory>

inline int vpTypeId(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.typeId();
#else
    return v.userType();
#endif
}

// SQL-NULL bzw. „kein Wert“ – mit der Bedeutung von Qt 6: nur ein ungültiger QVariant (oder
// nullptr) ist null. Unter Qt 5 gälte sonst auch ein leerer QString als NULL (→ NOT NULL-Fehler).
inline bool vpIsNull(const QVariant &v)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return v.isNull();
#else
    return !v.isValid();
#endif
}

inline QPointF vpPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

inline QPointF vpGlobalPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->globalPosition();
#else
    return e->screenPos();
#endif
}

// Liste mit n gleichen Einträgen (Qt 6: QList(n, wert); Qt 5 kennt diesen Konstruktor nicht)
template <typename T>
QList<T> vpFilledList(qsizetype n, const T &value = T())
{
    QList<T> l;
    l.reserve(int(n));
    for (qsizetype i = 0; i < n; ++i) l.append(value);
    return l;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using VpLibraryPath = QLibraryInfo::LibraryPath;
#else
using VpLibraryPath = QLibraryInfo::LibraryLocation;
#endif
inline QString vpLibraryPath(VpLibraryPath p)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QLibraryInfo::path(p);
#else
    return QLibraryInfo::location(p);
#endif
}

// Verbindung, die sich nach dem ersten Aufruf selbst löst (Qt 6: Qt::SingleShotConnection)
template <typename Sender, typename Signal, typename Functor>
QMetaObject::Connection vpConnectOnce(Sender *sender, Signal signal, QObject *context, Functor f)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QObject::connect(sender, signal, context, f, Qt::SingleShotConnection);
#else
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = QObject::connect(sender, signal, context, [conn, f]() mutable {
        QObject::disconnect(*conn);
        f();
    });
    return *conn;
#endif
}
