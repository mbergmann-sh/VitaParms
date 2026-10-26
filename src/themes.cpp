// SPDX-License-Identifier: MIT
//
// Die Paletten und Stylesheets der Themes "Dark" und "Visual Studio Code Dark"
// entsprechen AmigaED 4.0 (mainwindow.cpp: darkApplicationPalette(),
// vscodeApplicationPalette(), applyApplicationStyle()). Wie dort wird für beide
// Themes der Style "Fusion" erzwungen, weil native Styles eigene Paletten
// teilweise ignorieren.
#include "themes.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace Themes {

namespace {

QString g_platformStyle;   // Style beim Programmstart (Plattform-Standard)

QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window,            QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::WindowText,        QColor(0xd4, 0xd4, 0xd4));
    palette.setColor(QPalette::Base,              QColor(0x23, 0x23, 0x23));
    palette.setColor(QPalette::AlternateBase,     QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::ToolTipBase,       QColor(0x35, 0x35, 0x35));
    palette.setColor(QPalette::ToolTipText,       QColor(0xd4, 0xd4, 0xd4));
    palette.setColor(QPalette::Text,              QColor(0xd4, 0xd4, 0xd4));
    palette.setColor(QPalette::Button,            QColor(0x3c, 0x3c, 0x3c));
    palette.setColor(QPalette::ButtonText,        QColor(0xd4, 0xd4, 0xd4));
    palette.setColor(QPalette::BrightText,        QColor(0xf4, 0x47, 0x47));
    palette.setColor(QPalette::Link,              QColor(0x56, 0x9c, 0xd6));
    palette.setColor(QPalette::LinkVisited,       QColor(0xb3, 0x92, 0xf0));
    palette.setColor(QPalette::Highlight,         QColor(0x26, 0x4f, 0x78));
    palette.setColor(QPalette::HighlightedText,   QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::PlaceholderText,   QColor(0x8a, 0x8a, 0x8a));
    palette.setColor(QPalette::Disabled, QPalette::WindowText,      QColor(0x7f, 0x7f, 0x7f));
    palette.setColor(QPalette::Disabled, QPalette::Text,            QColor(0x7f, 0x7f, 0x7f));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor(0x7f, 0x7f, 0x7f));
    palette.setColor(QPalette::Disabled, QPalette::Highlight,       QColor(0x50, 0x50, 0x50));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0x7f, 0x7f, 0x7f));
    return palette;
}

QPalette vscodePalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window,            QColor(0x25, 0x25, 0x26));   // sideBar.background
    palette.setColor(QPalette::WindowText,        QColor(0xcc, 0xcc, 0xcc));   // foreground
    palette.setColor(QPalette::Base,              QColor(0x1e, 0x1e, 0x1e));   // editor.background
    palette.setColor(QPalette::AlternateBase,     QColor(0x2d, 0x2d, 0x2d));
    palette.setColor(QPalette::ToolTipBase,       QColor(0x25, 0x25, 0x26));
    palette.setColor(QPalette::ToolTipText,       QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::Text,              QColor(0xd4, 0xd4, 0xd4));   // editor.foreground
    palette.setColor(QPalette::Button,            QColor(0x3c, 0x3c, 0x3c));
    palette.setColor(QPalette::ButtonText,        QColor(0xcc, 0xcc, 0xcc));
    palette.setColor(QPalette::BrightText,        QColor(0xf4, 0x84, 0x71));
    palette.setColor(QPalette::Link,              QColor(0x37, 0x94, 0xff));   // textLink.foreground
    palette.setColor(QPalette::LinkVisited,       QColor(0xb1, 0x80, 0xd7));
    palette.setColor(QPalette::Highlight,         QColor(0x09, 0x47, 0x71));   // list.activeSelectionBackground
    palette.setColor(QPalette::HighlightedText,   QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::PlaceholderText,   QColor(0x8a, 0x8a, 0x8a));
    palette.setColor(QPalette::Disabled, QPalette::WindowText,      QColor(0x6a, 0x6a, 0x6a));
    palette.setColor(QPalette::Disabled, QPalette::Text,            QColor(0x6a, 0x6a, 0x6a));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor(0x6a, 0x6a, 0x6a));
    palette.setColor(QPalette::Disabled, QPalette::Highlight,       QColor(0x3c, 0x3c, 0x3c));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0x6a, 0x6a, 0x6a));
    return palette;
}

const char *kVSCodeStyleSheet =
    "QStatusBar { background-color: #007ACC; color: #ffffff; }"
    "QStatusBar::item { border: none; }"
    "QStatusBar QLabel { color: #ffffff; }"
    "QMenuBar { background-color: #3c3c3c; color: #cccccc; }"
    "QMenuBar::item { background-color: #3c3c3c; color: #cccccc; }"
    "QMenuBar::item:selected, QMenuBar::item:pressed { background-color: #094771; color: #ffffff; }"
    "QMenu { background-color: #252526; color: #cccccc; border: 1px solid #454545; }"
    "QMenu::item:selected { background-color: #094771; color: #ffffff; }"
    "QToolBar { background-color: #333333; border: none; spacing: 2px; }"
    "QTabBar::tab { background-color: #2d2d2d; color: #969696; padding: 4px 10px; }"
    "QTabBar::tab:selected { background-color: #1e1e1e; color: #ffffff; }";

} // namespace

void init()
{
    if (g_platformStyle.isEmpty() && QApplication::style())
        g_platformStyle = QApplication::style()->name();
    apply(current());
}

QStringList available()
{
    QStringList l{ systemId() };
    for (const QString &k : QStyleFactory::keys())
        if (!l.contains(k, Qt::CaseInsensitive)) l << k;
    l << darkId() << vscodeId();
    return l;
}

QString displayName(const QString &id)
{
    if (id == systemId())
        return QCoreApplication::translate("Themes", "Systemstandard (%1)").arg(g_platformStyle);
    if (id == darkId())
        return QCoreApplication::translate("Themes", "Dark (AmigaED)");
    if (id == vscodeId())
        return QCoreApplication::translate("Themes", "Visual Studio Code Dark (AmigaED)");
    return id;   // Namen der Qt-Styles (Fusion, Windows, windowsvista, macOS …)
}

QString current()
{
    const QString t = QSettings().value(QStringLiteral("ui/theme"), systemId()).toString();
    return available().contains(t, Qt::CaseInsensitive) ? t : systemId();
}

bool isDark()
{
    const QString t = current();
    if (t == darkId() || t == vscodeId()) return true;
    return QApplication::palette().color(QPalette::Window).lightness() < 110;
}

void apply(const QString &id)
{
    QSettings().setValue(QStringLiteral("ui/theme"), id);
    if (id == darkId() || id == vscodeId()) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(id == darkId() ? darkPalette() : vscodePalette());
        qApp->setStyleSheet(id == vscodeId() ? QString::fromLatin1(kVSCodeStyleSheet) : QString());
        return;
    }
    const QString style = (id == systemId() || id.isEmpty()) ? g_platformStyle : id;
    if (!style.isEmpty())
        QApplication::setStyle(QStyleFactory::create(style));
    if (id == systemId() || id.isEmpty())
        QApplication::setPalette(QPalette());   // Palette der Plattform (inkl. System-Dunkelmodus)
    else if (QApplication::style())
        QApplication::setPalette(QApplication::style()->standardPalette());
    qApp->setStyleSheet(QString());
}

} // namespace Themes
