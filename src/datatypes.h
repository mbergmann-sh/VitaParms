// VitaParms – Datentypen
// SPDX-License-Identifier: MIT
#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "recurrence.h"
#include <optional>

// Amputationsstufen je Gliedmaße (für die BMI-Korrektur, siehe bmi.h)
enum class ArmLevel { None = 0, Hand, ForearmWithHand, WholeArm };
enum class LegLevel { None = 0, Foot, LowerLegWithFoot, WholeLeg };

struct Amputations {
    ArmLevel armLeft  = ArmLevel::None;
    ArmLevel armRight = ArmLevel::None;
    LegLevel legLeft  = LegLevel::None;
    LegLevel legRight = LegLevel::None;

    bool any() const {
        return armLeft != ArmLevel::None || armRight != ArmLevel::None
            || legLeft != LegLevel::None || legRight != LegLevel::None;
    }
};

struct Patient {
    qint64      id = 0;
    QString     lastName;
    QString     firstName;
    QDate       birthDate;
    QString     sex;          // "m", "w", "d"
    QString     street;
    QString     zip;
    QString     city;
    QString     phone;
    QString     mobile;
    QString     diagnoses;
    QString     notes;
    Amputations amputations;
    // Pflegegrad (0 = keiner, 1–5) und nächste Überprüfung durch den Medizinischen Dienst
    int         careLevel = 0;
    QDate       careReview;
    // Schwerbehindertenausweis: Grad der Behinderung (0 = kein Ausweis), Merkzeichen, Gültigkeit
    int         disabilityDegree = 0;
    QStringList disabilityMarks;      // z. B. "G", "aG", "B", "H"
    QDate       disabilityValidUntil; // ungültig = unbefristet
    QJsonObject extra;        // Zusatzfelder aus benutzerdefinierten Formularen

    QString displayName() const {
        if (lastName.isEmpty() && firstName.isEmpty())
            return QStringLiteral("(ohne Namen)");
        if (firstName.isEmpty()) return lastName;
        return lastName + QStringLiteral(", ") + firstName;
    }
    int ageAt(const QDate &d) const {
        if (!birthDate.isValid() || !d.isValid()) return -1;
        int a = d.year() - birthDate.year();
        if (d.month() < birthDate.month()
            || (d.month() == birthDate.month() && d.day() < birthDate.day()))
            --a;
        return a;
    }
};

struct VitalRecord {
    qint64    id = 0;
    qint64    patientId = 0;
    QDateTime measuredAt;
    QString   context;        // Tagesprofil-Zeitpunkt, z. B. "nüchtern"
    bool      isAdmission = false;

    std::optional<int>    pulse;        // 1/min
    std::optional<int>    systolic;     // mmHg
    std::optional<int>    diastolic;    // mmHg
    std::optional<double> temperature;  // °C
    std::optional<int>    spo2;         // %
    std::optional<int>    peakFlow;     // l/min (exspiratorischer Spitzenfluss)
    std::optional<double> glucose;      // mg/dl (intern immer mg/dl)
    std::optional<double> weight;       // kg
    std::optional<double> height;       // cm
    std::optional<double> bmi;          // kg/m², bei Speicherung berechnet

    QString     observations;
    QJsonObject extra;

    bool hasAnyValue() const {
        return pulse || systolic || diastolic || temperature || spo2 || peakFlow
            || glucose || weight || height || !observations.trimmed().isEmpty()
            || !extra.isEmpty();
    }
};

struct Medication {
    qint64  id = 0;
    qint64  patientId = 0;
    QString name;           // Handelsname
    QString substance;      // Wirkstoff
    QString strength;       // z. B. "5 mg"
    QString form;           // Darreichungsform
    QString pzn;            // Pharmazentralnummer (8-stellig), für den Beipackzettel
    // Einnahme-Rhythmus: freq None = täglich; sonst alle N Tage / Wochentage / monatlich,
    // gerechnet ab rhythmStart (nur Regelmedikation).
    Recurrence rhythm;
    QDate   rhythmStart;
    // Art: Regelmedikation (fester Plan), Bedarfs-, Notfallmedikation, Sonstiges
    enum Category { Regular = 0, AsNeeded = 1, Emergency = 2, Other = 3 };   // Other = Sonstiges
    static constexpr int CategoryCount = 4;
    int     category = Regular;
    QString rhythmNote;     // Bemerkung zur Einnahme, z. B. "21 Tage Einnahme, 7 Tage Pause"
    QString morning, noon, evening, night;
    QString unit;           // z. B. "Stück", "IE", "ml"
    QString reason;         // Indikation
    QString notes;
    QDate   startDate;
    QDate   endDate;        // optional
    bool    archived = false;
    QDateTime archivedAt;
    QJsonObject extra;

    QString dosageScheme() const {
        auto v = [](const QString &s) { return s.trimmed().isEmpty() ? QStringLiteral("0") : s.trimmed(); };
        return v(morning) + QLatin1Char('-') + v(noon) + QLatin1Char('-')
             + v(evening) + QLatin1Char('-') + v(night);
    }
};

// Dokument der digitalen Krankenakte (Befund, Laborwerte, Entlassbrief …)
struct MedicalDocument {
    qint64    id = 0;
    qint64    patientId = 0;
    QDate     docDate;        // Datum des Dokuments (nicht des Imports)
    QString   category;       // z. B. "Befund", "Laborwerte", "Entlassbrief"
    QString   title;
    QString   issuer;         // Praxis, Klinik, Labor
    QString   notes;
    QString   fileName;       // ursprünglicher Dateiname
    QByteArray sha256;        // Prüfsumme (Dubletten-Erkennung)
    qint64    size = 0;       // Bytes
    int       pages = 0;
    QString   fullText;       // aus dem PDF gelesener Text (für die Suche)
    QDateTime addedAt;
    QJsonObject extra;

    bool hasText() const { return !fullText.trimmed().isEmpty(); }
};

// Termin im Planer
struct Appointment {
    enum Priority { Low = 0, Normal = 1, High = 2, Urgent = 3 };

    qint64    id = 0;
    qint64    patientId = 0;      // 0 = keiner Person zugeordnet
    QString   owner;              // zuständiges Anmeldekonto
    QString   title;
    QString   location;
    QDateTime start;
    QDateTime end;
    bool      allDay = false;
    int       priority = Normal;
    QString   notes;
    int       remindMinutes = -1; // Vorlauf der Erinnerung in Minuten, -1 = keine
    bool      remindVisual = true;
    bool      remindSound = true;
    bool      remindEmail = false;
    QString   emailTo;
    QDateTime remindedAt;         // wann die Erinnerung ausgelöst wurde
    QDateTime snoozeUntil;        // "später erinnern"
    bool      done = false;
    QString   createdBy;
    QJsonObject extra;
    // Terminserie: Zeile gehört zur Serie seriesId; seriesOrig = ursprünglicher Zeitpunkt
    // laut Regel (RECURRENCE-ID), detached = einzeln geändert
    qint64    seriesId = 0;
    QDateTime seriesOrig;
    bool      detached = false;

    bool isSeries() const { return seriesId != 0; }
    bool hasReminder() const { return remindMinutes >= 0 || snoozeUntil.isValid(); }
    QDateTime reminderTime() const
    {
        if (snoozeUntil.isValid()) return snoozeUntil;
        if (remindMinutes < 0 || !start.isValid()) return {};
        return start.addSecs(-qint64(remindMinutes) * 60);
    }
};

// Protokoll des Medikamenten-Assistenten: wann, von wem, was gestellt wurde
struct MedPreparation {
    qint64    id = 0;
    qint64    patientId = 0;
    QDateTime preparedAt;
    QString   preparedBy;          // Anmeldekonto
    int       slot = -1;           // -1 = alle Zeitpunkte, 0 morgens … 3 zur Nacht
    QDate     forDate;             // für welchen Tag gestellt wurde (wöchentlich: erster Tag)
    int       days = 1;            // 1 = Tagesdosierer, 7 = Wochendosierer
    int       total = 0;           // Medikamente in der Liste
    int       confirmed = 0;       // davon mit „Weiter“ bestätigt
    bool      complete = false;    // bis „Medikamente sind gerichtet“ durchlaufen
    QJsonArray items;              // [{name, strength, form, dose}]
    QString   note;
};

