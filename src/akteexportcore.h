// VitaParms – Krankenakte als ZIP packen (optional AES-256); ohne Oberfläche (Desktop und Tablet)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QList>
#include <QString>
#include <functional>

namespace AkteExport {

struct Options {
    QList<qint64> documentIds;       // in gewünschter Reihenfolge
    bool includeSheet = true;        // Stammblatt (inkl. Medikation, Aufnahmewerte)
    bool includeMedPlan = true;      // Medikamentenplan
    bool includeVitals = false;      // Verlauf + Grafik
    int  vitalsDays = 90;            // Zeitraum für Verlauf/Grafik (0 = alle)
    QString password;                // leer = unverschlüsselt
    QString note;                    // Hinweis im Inhaltsverzeichnis
};

QString defaultArchiveName(const Patient &p);

// Erstellt das ZIP-Archiv. 'progress' (optional) erhält (aktuell, gesamt).
bool build(const QString &zipPath, const Patient &p, const Options &o, QString *error = nullptr,
           const std::function<void(int, int)> &progress = {});

// Dateiname für ein Dokument im Archiv: "003_2026-09-30_Laborwerte_Blutbild.pdf"
QString archiveFileName(int index, const MedicalDocument &d);

} // namespace AkteExport
