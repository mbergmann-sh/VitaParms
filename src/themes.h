// VitaParms – Erscheinungsbild (Qt-Styles der Plattform + Dark Themes aus AmigaED 4.0)
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

namespace Themes {

// Kennungen der beiden aus AmigaED 4.0 übernommenen Themes
inline QString darkId()   { return QStringLiteral("Dark"); }
inline QString vscodeId() { return QStringLiteral("Visual Studio Code Dark"); }
inline QString systemId() { return QStringLiteral("System"); }

void init();                  // beim Start: Plattform-Standard merken, gespeichertes Theme anwenden
QStringList available();      // "System", alle QStyleFactory::keys(), "Dark", "Visual Studio Code Dark"
QString displayName(const QString &id);
QString current();
void apply(const QString &id);   // sofort wirksam und gespeichert
bool isDark();                   // aktuelles Theme dunkel?

} // namespace Themes
