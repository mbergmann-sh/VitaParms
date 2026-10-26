// VitaParms – Demo-Datenbank (Hilfe → Demo-Datenbank starten)
//
// Die Demo läuft als eigener Programmstart mit "--demo": eigene Einstellungen
// (Anwendungsname "VitaParms-Demo"), eigene verschlüsselte Datenbank im Datenordner,
// automatische Anmeldung mit dem Konto "demo". Bei jedem Start wird die Datenbank neu
// erzeugt – 365 Tage Messwerte bis heute, Medikamentenplan, Termine, Krankenakte –,
// Änderungen in der Demo gehen also nie in die eigenen Daten über.
// SPDX-License-Identifier: MIT
#pragma once

#include <QCoreApplication>
#include <QString>
#include <functional>

class DemoData
{
    Q_DECLARE_TR_FUNCTIONS(DemoData)
public:
    static constexpr int kDays = 365;

    static bool isActive();                     // läuft das Programm als Demo?
    static void setActive(bool on);

    static QString accountName() { return QStringLiteral("demo"); }
    static QString password() { return QStringLiteral("demo1234"); }

    // Schaltet die Einstellungen auf den Demo-Bereich um und übernimmt Sprache,
    // Erscheinungsbild, Einheiten usw. aus den eigenen Einstellungen.
    static void enterDemoSettings();
    static QString databasePath();              // nach enterDemoSettings() aufrufen

    // Legt die Demo-Datenbank neu an, meldet das Demo-Konto an und füllt sie.
    static bool create(const QString &path, qint64 *patientId, QString *error = nullptr,
                       const std::function<void(int)> &progress = {});

    // Füllt die geöffnete Datenbank mit der Demo-Person (für create() und Tests).
    // 'today' ist der letzte Tag der Messreihe.
    static bool populate(qint64 *patientId, const QDate &today, QString *error = nullptr,
                         const std::function<void(int)> &progress = {});

    // Startet die Demo als zweites Programmfenster
    static bool launch();
};
