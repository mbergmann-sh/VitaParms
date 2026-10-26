// VitaParms – Handbuch und „Erste Schritte“ öffnen (Hilfe → Handbuch, F1; Hilfe → Erste Schritte)
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

class QWidget;

namespace Manual {

enum class Format { Pdf, Chm };
enum class Doc { Manual, GettingStarted };   // Handbuch bzw. Heft „Erste Schritte“

// Dateiname für die Sprache: Handbuch "de" → VitaParms-Handbuch, sonst VitaParms-Manual;
// Erste Schritte "de" → VitaParms-ErsteSchritte, sonst VitaParms-GettingStarted
QString fileName(const QString &lang, Format format, Doc doc = Doc::Manual);
QString title(Doc doc);                      // „Handbuch“ / „Erste Schritte“ (übersetzt)

// Ordner, in denen nach dem Handbuch gesucht wird – zuerst <Programmordner>/docs
QStringList searchDirs();

// Bevorzugtes Format der Plattform: Windows → CHM (Windows-Hilfe), sonst PDF
Format preferredFormat();

// Sucht das Handbuch: erst bevorzugtes Format in der gewünschten Sprache, dann PDF,
// dann die jeweils andere Sprache. Leer, wenn nichts gefunden wurde.
QString locate(const QString &lang, Format preferred, const QStringList &dirs = searchDirs(), Doc doc = Doc::Manual);

// Öffnet das Dokument in der Programmsprache (CHM mit der Windows-Hilfe bzw. PDF im
// Standardanzeiger). Meldet fehlende Dateien mit einem Hinweisfenster. Braucht keine Anmeldung.
bool open(QWidget *parent, Doc doc = Doc::Manual);

} // namespace Manual
