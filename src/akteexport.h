// VitaParms – Krankenakte als ZIP packen (optional AES-256) und per E-Mail versenden
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;

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

class AkteExportDialog : public QDialog
{
    Q_OBJECT
public:
    AkteExportDialog(const Patient &p, const QList<qint64> &selected, const QList<qint64> &filtered,
                     const QList<qint64> &all, QWidget *parent = nullptr);

private slots:
    void saveZip();
    void sendMail();
    void generatePassword();
    void updateState();

private:
    bool collect(AkteExport::Options &o);
    bool buildTo(const QString &path, const AkteExport::Options &o);

    Patient m_patient;
    QList<qint64> m_selected, m_filtered, m_all;
    QRadioButton *m_rSelected = nullptr, *m_rFiltered = nullptr, *m_rAll = nullptr, *m_rNone = nullptr;
    QCheckBox *m_sheet = nullptr, *m_medPlan = nullptr, *m_vitals = nullptr, *m_encrypt = nullptr;
    QSpinBox *m_days = nullptr;
    QLineEdit *m_pw1 = nullptr, *m_pw2 = nullptr, *m_to = nullptr, *m_subject = nullptr;
    QPlainTextEdit *m_body = nullptr, *m_note = nullptr;
    QLabel *m_pwInfo = nullptr;
};
