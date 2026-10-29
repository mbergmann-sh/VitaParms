// VitaParms – Wunddokumentation
//
// Aufbau nach dem Expertenstandard „Pflege von Menschen mit chronischen Wunden“ (DNQP) und den
// üblichen Kriterien der Wunddokumentation (ICW):
//  * Wunde (Wundanamnese): Wundart mit Klassifikation (Dekubitus-Kategorie nach EPUAP/NPIAP,
//    Wagner-Grad beim diabetischen Fußsyndrom, Verbrennungsgrad), medizinische Diagnose,
//    Lokalisation, Entstehung (Datum, Ort, „bei Aufnahme vorhanden“), Rezidive, bisherige
//    Maßnahmen, wund- und therapiebedingte Einschränkungen, Selbstmanagement, Therapieziel,
//    Einwilligung zur Fotodokumentation, Status (offen / abgeheilt).
//  * Chronik: Wundbeurteilung (vollständiges Assessment: Größe L×B×T, Taschen/Unterminierung nach
//    der Uhrmethode, Wundgrund in Prozent, Exsudat, Geruch, Wundrand, Wundumgebung,
//    Infektionszeichen, Schmerz, Heilungsphase und -tendenz, Versorgung, Foto) und
//    Verbandwechsel (Kurzdokumentation: Handzeichen, Beschreibung, Tendenz, Versorgung).
//  * Hinweise: Beurteilung spätestens nach 4 Wochen, Infektionszeichen, Verschlechterung,
//    fälliger Verbandwechsel.
// VitaParms ist kein Medizinprodukt – die Hinweise ersetzen keine fachliche Beurteilung.
// Übersetzungskontext „Wounds“.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace Wounds {

constexpr int kAssessmentDays = 28;     // vollständige Wundbeurteilung spätestens nach 4 Wochen

enum Kind { Pressure = 0, VenousUlcer, ArterialUlcer, MixedUlcer, DiabeticFoot, Surgical, Traumatic, Burn, Tumor, OtherKind, KindCount };
enum Status { Open = 0, Healed, Closed };            // Closed: nicht mehr betreut (z. B. verlegt)

struct Wound {
    qint64    id = 0;
    qint64    patientId = 0;
    QString   label;               // „Wunde 1“, frei änderbar
    int       kind = Pressure;
    int       grade = -1;          // Klassifikation je Wundart (-1 = ohne)
    QString   diagnosis;           // medizinische Diagnose / Grunderkrankung
    QString   site;                // Körperstelle
    QString   side;                // links / rechts / mittig
    QString   siteDetail;          // genauer, z. B. „2 cm oberhalb des Innenknöchels“
    QDate     since;               // entstanden am
    QString   origin;              // Entstehungsort (zu Hause, Krankenhaus …)
    bool      onAdmission = false; // bei Aufnahme vorhanden
    QDate     admissionDate;
    int       recurrences = 0;     // Anzahl Rezidive
    QString   recurrenceFree;      // rezidivfreie Zeit (Text)
    QString   previousCare;        // bisherige Diagnostik und Therapie
    QString   physician;           // behandelnde Ärztin/Arzt, Wundexpertin
    int       goal = 0;            // 0 Abheilung, 1 Verbesserung, 2 Symptomkontrolle (palliativ)
    QStringList limitations;       // wund- und therapiebedingte Einschränkungen
    QString   selfManagement;      // Wissen, Fähigkeiten, Unterstützung
    bool      photoConsent = false;
    int       status = Open;
    QDate     closedAt;            // abgeheilt bzw. beendet am
    QString   notes;
    QDateTime created;
    QString   createdBy;
    QString title() const;         // „Wunde 1 – Dekubitus Kat. 2, Kreuzbein“
};

enum EntryKind { Assessment = 0, DressingChange };

struct Entry {
    qint64    id = 0;
    qint64    woundId = 0;
    qint64    patientId = 0;
    QDateTime at;
    int       kind = Assessment;
    QString   by;                  // Handzeichen (Anmeldekonto)
    // Größe in cm
    std::optional<double> length, width, depth;
    QString   pockets;             // Taschen, Unterminierung, Fisteln mit Länge und Uhrzeit
    // Wundgrund in Prozent
    int granulation = 0, epithel = 0, fibrin = 0, necrosis = 0;
    QStringList exposed;           // freiliegend: Sehne, Knochen, Muskel, Faszie
    int       exudate = -1;        // 0 kein, 1 wenig, 2 mäßig, 3 viel (-1 = ohne Angabe)
    QString   exudateKind;         // serös, blutig, eitrig …
    int       odor = -1;           // 0 kein, 1 leicht, 2 stark
    QStringList edge, surroundings, infection;
    std::optional<int> pain, painChange;   // NRS 0–10 (in Ruhe, beim Verbandwechsel)
    int       grade = -1;          // aktuelle Klassifikation (Dekubitus-Kategorie …)
    int       phase = -1;          // 0 Exsudation/Reinigung, 1 Granulation, 2 Epithelisierung
    int       trend = -1;          // 0 besser, 1 unverändert, 2 schlechter
    QString   description;         // Kurzbeschreibung (Verbandwechsel)
    QString   cleansing, dressing, fixation, compression;   // Versorgung
    int       intervalDays = 0;    // Verbandwechsel alle … Tage
    QDate     nextChange;
    QByteArray photo;              // JPEG, verkleinert (bis 1024 px)
    QString   notes;
    std::optional<double> area() const;   // L × B in cm²
};

// ---- Auswahllisten (übersetzt)
QStringList kindNames();
QString     kindName(int kind);
QStringList gradeNames(int kind);          // leer = keine Klassifikation
QString     gradeSystem(int kind);         // „Kategorie (EPUAP/NPIAP)“, „Wagner-Grad“ …
QString     gradeName(int kind, int grade);
QStringList sites();
QStringList sides();
QStringList origins();
QStringList goals();
QStringList limitationChoices();
QStringList exposedChoices();
QStringList exudateAmounts();              // kein, wenig, mäßig, viel
QStringList exudateKinds();
QStringList odors();
QStringList edgeChoices();
QStringList surroundingChoices();
QStringList infectionChoices();
QStringList phases();
QStringList trends();
QStringList statusNames();
QString     entryKindName(int kind);

// ---- Daten
QList<Wound> wounds(qint64 patientId, bool includeClosed = true);
std::optional<Wound> wound(qint64 id);
bool saveWound(Wound &w, QString *error = nullptr);       // neu (id 0) oder ändern
bool deleteWound(qint64 id, QString *error = nullptr);    // samt Chronik
QList<Entry> entries(qint64 woundId, bool withPhotos = true);   // neueste zuerst
std::optional<Entry> entry(qint64 id);
bool saveEntry(Entry &e, QString *error = nullptr);
bool deleteEntry(qint64 id, QString *error = nullptr);
int  nextNumber(qint64 patientId);                         // für „Wunde n“

// ---- Auswertung
QString woundSummary(const Wound &w);                      // Zeile für Stammblatt/Übersicht
QString entrySummary(const Entry &e);                      // Zeile für die Chronik
QString sizeText(const Entry &e);                          // „4,0 × 2,5 × 0,5 cm (10,0 cm²)“
std::optional<Entry> lastAssessment(const QList<Entry> &list);
QDate nextAssessmentDue(const Wound &w, const QList<Entry> &list);
struct Notice { bool warn = false; QString text; };
// Hinweise einer offenen Wunde: Beurteilung fällig, Infektionszeichen, Verschlechterung,
// Verbandwechsel fällig
QList<Notice> notices(const Wound &w, const QList<Entry> &list, const QDate &today = QDate::currentDate());
QList<Notice> patientNotices(qint64 patientId, const QDate &today = QDate::currentDate());
// Foto: auf höchstens 1024 px verkleinern, als JPEG
QByteArray preparePhoto(const QByteArray &imageData, QString *error = nullptr);

// ---- Ausgabe
QImage areaChart(const QList<Entry> &list);                // Verlauf der Wundfläche (leer bei < 2 Werten)
QString reportHtml(const Patient &p, const Wound &w, const QList<Entry> &list, bool withPhotos = true);
bool toPdf(const Patient &p, const Wound &w, const QString &path);
bool toXlsx(const Patient &p, const Wound &w, const QString &path, QString *error = nullptr);
QString admissionHtml(qint64 patientId);                   // Abschnitt für den Stammblatt-Ausdruck

} // namespace Wounds
