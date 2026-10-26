// VitaParms – Programmeinstellungen (QSettings-Wrapper)
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

class AppSettings
{
public:
    enum class GlucoseUnit { MgDl, MmolL };

    static QString defaultDatabasePath();
    static QString databasePath();
    static void    setDatabasePath(const QString &p);

    static GlucoseUnit glucoseUnit();
    static void        setGlucoseUnit(GlucoseUnit u);

    // Hauptbenutzer 1 und 2 (Patienten-ID, 0 = nicht belegt)
    static qint64 primaryUser(int slot);
    static void   setPrimaryUser(int slot, qint64 patientId);

    static qint64 lastPatient();
    static void   setLastPatient(qint64 id);

    static bool disclaimerAccepted();
    static void setDisclaimerAccepted(bool b);

    // Messzeitpunkte für Tagesprofile (frei editierbar)
    static QStringList profileContexts();
    static void        setProfileContexts(const QStringList &l);
    static QStringList defaultProfileContexts();

    static QStringList chartSeries();      // ausgewählte Diagramm-Bereiche
    static void        setChartSeries(const QStringList &l);
    static QStringList chartHiddenKeys();  // einzeln ausgeblendete Kurven (VitalDefs::key)
    static void        setChartHiddenKeys(const QStringList &l);
    static bool        chartSeparatePages();   // Druck: je Bereich eine Seite
    static void        setChartSeparatePages(bool b);
    static int         chartScale();           // 0 Tag … 3 Jahr, 4 freier Zeitraum
    static void        setChartScale(int s);

    // Formularordner für benutzerangepasste .ui-Dateien
    static QString userFormsDir();

    // GUI-Sprache: "system", "de" oder "en"
    static QString language();
    static void    setLanguage(const QString &l);

    // Automatische Sperre nach Minuten ohne Benutzung (0 = aus)
    static int  autoLockMinutes();
    static void setAutoLockMinutes(int m);

    // Medikamenten-Assistent
    static QString voiceGender();             // "female" / "male"
    static void    setVoiceGender(const QString &g);
    static double  speechRate();              // -1 … 1
    static void    setSpeechRate(double r);

    static QByteArray windowState(const QString &key);
    static void       setWindowState(const QString &key, const QByteArray &s);
};
