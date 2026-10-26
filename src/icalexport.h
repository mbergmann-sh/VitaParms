// VitaParms – Export von Terminen als iCalendar (RFC 5545, .ics)
// Einzeltermine als VEVENT; Serien als ein VEVENT mit RRULE, EXDATE (gelöschte Vorkommen)
// und eigenen VEVENTs mit RECURRENCE-ID für einzeln geänderte Vorkommen. Zeiten in der
// Systemzeitzone mit TZID und passendem VTIMEZONE, ganztägige Termine als DATE.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "recurrence.h"

#include <QDialog>
#include <QTimeZone>
#include <functional>

class QCheckBox;
class QRadioButton;

namespace ICal {

struct Options {
    bool includeDone = false;       // erledigte Termine mit exportieren
    bool alarms = true;             // Vorwarnung als VALARM
    bool details = false;           // Ort, Person und Notiz übernehmen
    QTimeZone timeZone = QTimeZone::systemTimeZone();
    std::function<QString(qint64)> patientName;   // optional, für Details
};

struct Result {
    QByteArray data;
    int events = 0;                 // VEVENT-Blöcke
    int series = 0;                 // davon Serien
};

Result build(const QList<Appointment> &appointments, const Options &o);
bool writeFile(const QString &path, const QByteArray &data, QString *error = nullptr);

// Hilfen (für Tests öffentlich)
QString escapeText(const QString &s);
QByteArray foldLine(const QString &line);           // UTF-8, max. 75 Oktette je Zeile, CRLF
QString vtimezone(const QTimeZone &tz, int year);
QString formatUtc(const QDateTime &dt);

} // namespace ICal

// Dialog: Umfang und Optionen wählen, als Datei speichern oder per E-Mail senden
class ICalExportDialog : public QDialog
{
    Q_OBJECT
public:
    struct Scope { QString label; QList<Appointment> items; };
    ICalExportDialog(const QList<Scope> &scopes, int defaultScope, QWidget *parent = nullptr);

private:
    QList<Appointment> chosen() const;
    ICal::Options options() const;
    void save();
    void mail();

    QList<Scope> m_scopes;
    QList<QRadioButton *> m_radios;
    QCheckBox *m_done = nullptr, *m_alarms = nullptr, *m_details = nullptr;
};
