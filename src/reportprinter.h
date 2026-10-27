// VitaParms – Ausgaben: Druck (DIN A4), PDF und Excel
// SPDX-License-Identifier: MIT
#pragma once

#include "chartwidget.h"
#include "datatypes.h"
#include "medstock.h"

class QWidget;
class JournalModel;

namespace ReportPrinter {

enum class Target { Printer, Preview, Pdf };

// Verlauf (alle Werte, aktuellste Messung zuerst) – A4 quer
void journal(QWidget *parent, const Patient &p, const JournalModel &model,
             const QString &periodText, Target t);
bool journalExcel(QWidget *parent, const Patient &p, const JournalModel &model,
                  const QString &periodText);

// Verlaufsgrafik – A4 hoch, mit Legende und Kennwerten (Min/Max/Mittel)
void charts(QWidget *parent, const Patient &p, const ChartSpec &spec, Target t);
bool chartsExcel(QWidget *parent, const Patient &p, const ChartSpec &spec);

// Medikationsliste für die Apotheke: alle aktuellen Medikamente mit Wirkstoff, PZN und den
// Angaben, die eine Apotheke für die Prüfung auf Wechselwirkungen braucht (Gewicht, Allergien,
// Diagnosen …), dazu Platz für Ergänzungen und Notizen. VitaParms prüft selbst nicht.
void pharmacyList(QWidget *parent, const Patient &p, const QList<Medication> &meds, Target t);
bool pharmacyListToPdf(const QString &path, const Patient &p, const QList<Medication> &meds);
// Bestellliste (Bestandsführung) – A4 hoch
void orderList(QWidget *parent, const Patient &p, const MedStock::Order &o, Target t);
bool orderListToPdf(const QString &path, const Patient &p, const MedStock::Order &o);
int  longTermMedicationCount(const QList<Medication> &meds);   // Regelmedikation (für den Hinweis auf die Beratung)

// Medikamentenplan und Stammblatt
void medicationPlan(QWidget *parent, const Patient &p, const QList<Medication> &meds, Target t);
void patientSheet(QWidget *parent, const Patient &p, const QList<Medication> &meds,
                  const std::optional<VitalRecord> &admission, Target t);

// Für Tests/Automatisierung: direkt in Datei schreiben, ohne Dialoge
bool chartsToPdf(const QString &path, const Patient &p, const ChartSpec &spec);
bool journalToPdf(const QString &path, const Patient &p, const JournalModel &model, const QString &periodText);
bool chartsToXlsx(const QString &path, const Patient &p, const ChartSpec &spec, QString *error = nullptr);
bool journalToXlsx(const QString &path, const Patient &p, const JournalModel &model,
                   const QString &periodText, QString *error = nullptr);
bool medicationPlanToPdf(const QString &path, const Patient &p, const QList<Medication> &meds);
bool patientSheetToPdf(const QString &path, const Patient &p, const QList<Medication> &meds,
                       const std::optional<VitalRecord> &admission);

// Digitale Krankenakte: Inhaltsverzeichnis (Dokumente + mitgelieferte Berichte)
bool documentIndexToPdf(const QString &path, const Patient &p, const QList<MedicalDocument> &docs,
                        const QList<QPair<QString, QString>> &reports, const QString &note = {});
void documentIndex(QWidget *parent, const Patient &p, const QList<MedicalDocument> &docs, Target t);

// Protokoll "Medikamente stellen" (wann, von wem, was)
void preparationLog(QWidget *parent, const Patient &p, const QList<MedPreparation> &log,
                    const QString &periodText, Target t);
bool preparationLogToPdf(const QString &path, const Patient &p, const QList<MedPreparation> &log,
                         const QString &periodText);
QString slotText(int slot);   // -1 alle Zeitpunkte, 0 morgens … 3 zur Nacht
QString preparationForText(const MedPreparation &e);   // "Mi 21.10. · morgens"

// Freier HTML-Bericht mit Kopf/Fuß (z. B. Suchergebnisse)
void htmlReport(QWidget *parent, const Patient &p, const QString &title, const QString &html, Target t,
                bool landscape = true);
bool htmlReportToPdf(const QString &path, const Patient &p, const QString &title, const QString &html,
                     bool landscape = true);

QString disclaimerShort();

// Speicherort für einen Export erfragen (Vorschlag: <base>_<Name>_<Datum>.<ext>)
QString askExportFile(QWidget *parent, const QString &caption, const QString &base, const Patient &p,
                      const QString &ext, const QString &filter);

} // namespace ReportPrinter
