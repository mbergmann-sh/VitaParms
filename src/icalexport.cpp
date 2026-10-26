// SPDX-License-Identifier: MIT
#include "icalexport.h"
#include "database.h"
#include "mailer.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ICal {

namespace {

const char *const kDayCodes[7] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

QString fmtOffset(int secs)
{
    const QChar sign = secs < 0 ? QLatin1Char('-') : QLatin1Char('+');
    secs = std::abs(secs);
    return QStringLiteral("%1%2%3").arg(sign).arg(secs / 3600, 2, 10, QLatin1Char('0'))
                                   .arg((secs % 3600) / 60, 2, 10, QLatin1Char('0'));
}

// Gleicher Tag laut Regel (n-ter/letzter Wochentag im Monat) im Jahr 1970 –
// so gilt die Zeitzonenregel auch für alle Termine vor dem Exportjahr
QDate sameRuleIn1970(const QDate &d, int ord)
{
    const QDate first(1970, d.month(), 1);
    if (ord < 0) {
        const QDate last(1970, d.month(), first.daysInMonth());
        return last.addDays(-((last.dayOfWeek() - d.dayOfWeek() + 7) % 7));
    }
    return first.addDays((d.dayOfWeek() - first.dayOfWeek() + 7) % 7 + 7 * (ord - 1));
}

int priorityValue(int p)
{
    switch (p) {
    case Appointment::Urgent: return 1;
    case Appointment::High:   return 3;
    case Appointment::Low:    return 9;
    default:                  return 5;
    }
}

struct Writer
{
    const Options &o;
    bool utc;
    QString tzid;
    QStringList lines;

    QString localValue(const QDateTime &d) const
    {
        if (utc) return formatUtc(d);
        return d.toTimeZone(o.timeZone).toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
    }
    QString tzParam() const { return utc ? QString() : QStringLiteral(";TZID=") + tzid; }

    void dateTimeProp(const QString &name, const QDateTime &d, bool allDay)
    {
        if (allDay) lines << name + QStringLiteral(";VALUE=DATE:") + d.date().toString(QStringLiteral("yyyyMMdd"));
        else lines << name + tzParam() + QLatin1Char(':') + localValue(d);
    }

    void times(const Appointment &a, const QDateTime &start, const QDateTime &end)
    {
        if (a.allDay) {
            dateTimeProp(QStringLiteral("DTSTART"), start, true);
            const QDate last = end.isValid() && end.date() > start.date() ? end.date() : start.date();
            lines << QStringLiteral("DTEND;VALUE=DATE:") + last.addDays(1).toString(QStringLiteral("yyyyMMdd"));
        } else {
            dateTimeProp(QStringLiteral("DTSTART"), start, false);
            if (end.isValid() && end > start) dateTimeProp(QStringLiteral("DTEND"), end, false);
        }
    }

    void common(const Appointment &a)
    {
        lines << QStringLiteral("SUMMARY:") + escapeText(a.title);
        if (o.details) {
            if (!a.location.isEmpty()) lines << QStringLiteral("LOCATION:") + escapeText(a.location);
            QStringList desc;
            if (a.patientId && o.patientName) {
                const QString n = o.patientName(a.patientId);
                if (!n.isEmpty()) desc << QCoreApplication::translate("ICal", "Person: %1").arg(n);
            }
            if (!a.notes.isEmpty()) desc << a.notes;
            if (!desc.isEmpty()) lines << QStringLiteral("DESCRIPTION:") + escapeText(desc.join(QLatin1Char('\n')));
        }
        lines << QStringLiteral("PRIORITY:%1").arg(priorityValue(a.priority));
        lines << QStringLiteral("CATEGORIES:VitaParms");
        lines << QStringLiteral("CLASS:PRIVATE");
        lines << QStringLiteral("STATUS:CONFIRMED");
        if (o.alarms && a.remindMinutes >= 0) {
            lines << QStringLiteral("BEGIN:VALARM") << QStringLiteral("ACTION:DISPLAY")
                  << QStringLiteral("DESCRIPTION:") + escapeText(a.title)
                  << (a.remindMinutes > 0 ? QStringLiteral("TRIGGER:-PT%1M").arg(a.remindMinutes)
                                          : QStringLiteral("TRIGGER:PT0S"))
                  << QStringLiteral("END:VALARM");
        }
    }
};

} // namespace

QString formatUtc(const QDateTime &dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

QString escapeText(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char(';'), QStringLiteral("\\;"));
    t.replace(QLatin1Char(','), QStringLiteral("\\,"));
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\\n"));
    t.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    t.remove(QLatin1Char('\r'));
    return t;
}

QByteArray foldLine(const QString &line)
{
    const QByteArray u = line.toUtf8();
    QByteArray out;
    int pos = 0;
    int limit = 75;
    while (u.size() - pos > limit) {
        int cut = pos + limit;
        while (cut > pos && (uchar(u[cut]) & 0xC0) == 0x80) --cut;   // kein UTF-8-Zeichen zerteilen
        out += u.mid(pos, cut - pos) + "\r\n ";
        pos = cut;
        limit = 74;   // Folgezeilen beginnen mit einem Leerzeichen
    }
    out += u.mid(pos) + "\r\n";
    return out;
}

QString vtimezone(const QTimeZone &tz, int year)
{
    const QString id = QString::fromLatin1(tz.id());
    QStringList l;
    l << QStringLiteral("BEGIN:VTIMEZONE") << QStringLiteral("TZID:") + id
      << QStringLiteral("X-LIC-LOCATION:") + id;
    const QDateTime y0(QDate(year, 1, 1), QTime(0, 0), QTimeZone::utc());
    const auto t1 = tz.nextTransition(y0);
    const auto t2 = t1.atUtc.isValid() ? tz.nextTransition(t1.atUtc) : t1;
    if (!tz.hasDaylightTime() || !t1.atUtc.isValid() || !t2.atUtc.isValid()
        || t2.atUtc > y0.addYears(1).addDays(1)) {
        const QDateTime mid(QDate(year, 1, 1), QTime(12, 0), tz);
        const int off = tz.offsetFromUtc(mid);
        l << QStringLiteral("BEGIN:STANDARD") << QStringLiteral("DTSTART:19700101T000000")
          << QStringLiteral("TZOFFSETFROM:") + fmtOffset(off) << QStringLiteral("TZOFFSETTO:") + fmtOffset(off)
          << QStringLiteral("TZNAME:") + tz.abbreviation(mid)
          << QStringLiteral("END:STANDARD");
    } else {
        for (const auto &t : { t1, t2 }) {
            const int to = t.offsetFromUtc;
            const int from = tz.offsetFromUtc(t.atUtc.addSecs(-60));
            // Beginn in der bis dahin gültigen Ortszeit
            const QDateTime onset = t.atUtc.addSecs(from);
            const QDate d = onset.date();
            const int ord = d.day() + 7 > d.daysInMonth() ? -1 : (d.day() - 1) / 7 + 1;
            const QString comp = t.daylightTimeOffset != 0 ? QStringLiteral("DAYLIGHT") : QStringLiteral("STANDARD");
            l << QStringLiteral("BEGIN:") + comp
              << QStringLiteral("DTSTART:") + QDateTime(sameRuleIn1970(d, ord), onset.time())
                                                  .toString(QStringLiteral("yyyyMMdd'T'HHmmss"))
              << QStringLiteral("RRULE:FREQ=YEARLY;BYMONTH=%1;BYDAY=%2%3").arg(d.month()).arg(ord)
                     .arg(QLatin1String(kDayCodes[d.dayOfWeek() - 1]))
              << QStringLiteral("TZOFFSETFROM:") + fmtOffset(from) << QStringLiteral("TZOFFSETTO:") + fmtOffset(to)
              << QStringLiteral("TZNAME:") + t.abbreviation
              << QStringLiteral("END:") + comp;
        }
    }
    l << QStringLiteral("END:VTIMEZONE");
    return l.join(QLatin1Char('\n'));
}

Result build(const QList<Appointment> &appointments, const Options &o)
{
    Result r;
    Database &db = Database::instance();
    Writer w{ o, !o.timeZone.isValid() || o.timeZone.id() == "UTC" || o.timeZone.id() == "Etc/UTC", {}, {} };
    w.tzid = QString::fromLatin1(o.timeZone.id());
    const QString uidBase = QStringLiteral("vitaparms-") + (db.isOpen() ? db.databaseUuid() : QStringLiteral("local"));
    const QString stamp = QStringLiteral("DTSTAMP:") + formatUtc(QDateTime::currentDateTime());

    QList<Appointment> singles;
    QMap<qint64, bool> seriesIds;
    int minYear = QDate::currentDate().year();
    for (const Appointment &a : appointments) {
        if (a.isSeries()) { seriesIds.insert(a.seriesId, true); continue; }
        if (a.done && !o.includeDone) continue;
        singles << a;
        minYear = std::min(minYear, a.start.date().year());
    }
    QList<AppointmentSeries> series;
    for (auto it = seriesIds.cbegin(); it != seriesIds.cend(); ++it)
        if (const auto s = db.series(it.key())) {
            series << *s;
            minYear = std::min(minYear, s->dtstart.date().year());
        }

    w.lines << QStringLiteral("BEGIN:VCALENDAR") << QStringLiteral("VERSION:2.0")
            << (QStringLiteral("PRODID:-//VitaParms//VitaParms %1//DE").arg(QCoreApplication::applicationVersion())
                    .replace(QStringLiteral(" //"), QStringLiteral("//")))
            << QStringLiteral("CALSCALE:GREGORIAN") << QStringLiteral("METHOD:PUBLISH")
            << QStringLiteral("X-WR-CALNAME:VitaParms");
    if (!w.utc && (!singles.isEmpty() || !series.isEmpty()))
        w.lines << vtimezone(o.timeZone, minYear).split(QLatin1Char('\n'));

    for (const Appointment &a : singles) {
        w.lines << QStringLiteral("BEGIN:VEVENT")
                << QStringLiteral("UID:%1-a%2@vitaparms").arg(uidBase).arg(a.id) << stamp;
        w.times(a, a.start, a.end);
        w.common(a);
        w.lines << QStringLiteral("END:VEVENT");
        ++r.events;
    }

    for (const AppointmentSeries &s : series) {
        const QList<Appointment> rows = db.seriesAppointments(s.id);
        if (rows.isEmpty()) continue;
        Appointment tmpl = rows.first();
        for (const Appointment &a : rows)
            if (!a.detached) { tmpl = a; break; }
        QList<QDate> ex = s.exdates;
        QList<Appointment> overrides;
        for (const Appointment &a : rows) {
            if (a.done && !o.includeDone) { ex << a.seriesOrig.date(); continue; }
            if (a.detached) overrides << a;
        }
        std::sort(ex.begin(), ex.end());
        ex.erase(std::unique(ex.begin(), ex.end()), ex.end());

        const QString uid = QStringLiteral("UID:%1-s%2@vitaparms").arg(uidBase).arg(s.id);
        const qint64 dur = tmpl.end.isValid() ? tmpl.start.secsTo(tmpl.end) : -1;
        w.lines << QStringLiteral("BEGIN:VEVENT") << uid << stamp;
        w.times(tmpl, s.dtstart, dur >= 0 ? s.dtstart.addSecs(dur) : QDateTime());
        QString until;
        if (s.rule.until.isValid())
            until = tmpl.allDay ? s.rule.until.toString(QStringLiteral("yyyyMMdd"))
                                : formatUtc(QDateTime(s.rule.until, s.dtstart.time()));
        w.lines << QStringLiteral("RRULE:") + s.rule.rrule(s.dtstart.date(), until);
        if (!ex.isEmpty()) {
            QStringList v;
            for (const QDate &d : ex)
                v << (tmpl.allDay ? d.toString(QStringLiteral("yyyyMMdd")) : w.localValue(QDateTime(d, s.dtstart.time())));
            w.lines << (tmpl.allDay ? QStringLiteral("EXDATE;VALUE=DATE:") : QStringLiteral("EXDATE") + w.tzParam() + QLatin1Char(':'))
                       + v.join(QLatin1Char(','));
        }
        w.common(tmpl);
        w.lines << QStringLiteral("END:VEVENT");
        ++r.events;
        ++r.series;

        for (const Appointment &a : overrides) {
            w.lines << QStringLiteral("BEGIN:VEVENT") << uid << stamp;
            w.dateTimeProp(QStringLiteral("RECURRENCE-ID"), a.seriesOrig, tmpl.allDay);
            w.times(a, a.start, a.end);
            w.common(a);
            w.lines << QStringLiteral("END:VEVENT");
            ++r.events;
        }
    }
    w.lines << QStringLiteral("END:VCALENDAR");
    for (const QString &l : std::as_const(w.lines)) r.data += foldLine(l);
    return r;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace ICal

// ============================================================ Dialog

ICalExportDialog::ICalExportDialog(const QList<Scope> &scopes, int defaultScope, QWidget *parent)
    : QDialog(parent), m_scopes(scopes)
{
    setWindowTitle(tr("Termine exportieren (iCalendar)"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Exportiert Termine als <b>.ics-Datei</b> für Kalender-Programme und Smartphones "
                                "(Outlook, Thunderbird, Apple/Google Kalender …). Serien werden mit ihrer "
                                "Wiederholungsregel vollständig übernommen."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *grp = new QGroupBox(tr("Umfang"), this);
    auto *gl = new QVBoxLayout(grp);
    for (int i = 0; i < scopes.size(); ++i) {
        auto *rb = new QRadioButton(tr("%1 (%2)").arg(scopes[i].label).arg(scopes[i].items.size()), grp);
        rb->setEnabled(!scopes[i].items.isEmpty());
        rb->setChecked(i == defaultScope && rb->isEnabled());
        m_radios << rb;
        gl->addWidget(rb);
    }
    bool any = false;
    for (auto *rb : m_radios) any |= rb->isChecked();
    if (!any)
        for (auto *rb : m_radios)
            if (rb->isEnabled()) { rb->setChecked(true); break; }
    lay->addWidget(grp);

    auto *opt = new QGroupBox(tr("Optionen"), this);
    auto *ol = new QVBoxLayout(opt);
    QSettings st;
    m_done = new QCheckBox(tr("&Erledigte Termine einschließen"), opt);
    m_done->setChecked(st.value(QStringLiteral("ical/includeDone"), false).toBool());
    m_alarms = new QCheckBox(tr("&Vorwarnungen als Kalender-Erinnerung übernehmen"), opt);
    m_alarms->setChecked(st.value(QStringLiteral("ical/alarms"), true).toBool());
    m_details = new QCheckBox(tr("&Ort, Person und Notiz übernehmen"), opt);
    m_details->setChecked(st.value(QStringLiteral("ical/details"), false).toBool());
    ol->addWidget(m_done);
    ol->addWidget(m_alarms);
    ol->addWidget(m_details);
    auto *warn = new QLabel(tr("<small>⚠ Die .ics-Datei ist <b>nicht verschlüsselt</b>. Ohne die letzte Option "
                               "enthält sie nur Titel, Zeit, Priorität und Wiederholung.</small>"), opt);
    warn->setWordWrap(true);
    ol->addWidget(warn);
    lay->addWidget(opt);

    auto *bb = new QDialogButtonBox(this);
    auto *save = bb->addButton(tr("&Speichern…"), QDialogButtonBox::AcceptRole);
    auto *mail = bb->addButton(tr("Per &E-Mail senden…"), QDialogButtonBox::ActionRole);
    bb->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    connect(save, &QPushButton::clicked, this, &ICalExportDialog::save);
    connect(mail, &QPushButton::clicked, this, &ICalExportDialog::mail);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    save->setDefault(true);
    resize(520, sizeHint().height());
}

QList<Appointment> ICalExportDialog::chosen() const
{
    for (int i = 0; i < m_radios.size(); ++i)
        if (m_radios[i]->isChecked()) return m_scopes[i].items;
    return {};
}

ICal::Options ICalExportDialog::options() const
{
    QSettings st;
    st.setValue(QStringLiteral("ical/includeDone"), m_done->isChecked());
    st.setValue(QStringLiteral("ical/alarms"), m_alarms->isChecked());
    st.setValue(QStringLiteral("ical/details"), m_details->isChecked());
    ICal::Options o;
    o.includeDone = m_done->isChecked();
    o.alarms = m_alarms->isChecked();
    o.details = m_details->isChecked();
    o.patientName = [](qint64 id) {
        const auto p = Database::instance().patient(id);
        return p ? p->displayName() : QString();
    };
    return o;
}

void ICalExportDialog::save()
{
    const auto items = chosen();
    if (items.isEmpty()) return;
    QSettings st;
    const QString dir = st.value(QStringLiteral("ical/lastDir"),
                                 QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString path = QFileDialog::getSaveFileName(this, tr("Termine exportieren"),
        dir + QStringLiteral("/VitaParms-Termine.ics"), tr("iCalendar (*.ics)"));
    if (path.isEmpty()) return;
    st.setValue(QStringLiteral("ical/lastDir"), QFileInfo(path).absolutePath());
    const ICal::Result r = ICal::build(items, options());
    QString err;
    if (!ICal::writeFile(path, r.data, &err)) {
        QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
        return;
    }
    QMessageBox::information(this, windowTitle(),
        tr("%1 Kalendereinträge (davon %2 Serien) gespeichert:\n%3")
            .arg(r.events).arg(r.series).arg(QDir::toNativeSeparators(path)));
    accept();
}

void ICalExportDialog::mail()
{
    const auto items = chosen();
    if (items.isEmpty()) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/VitaParms");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/VitaParms-Termine.ics");
    const ICal::Result r = ICal::build(items, options());
    QString err;
    if (!ICal::writeFile(path, r.data, &err)) {
        QMessageBox::critical(this, windowTitle(), err);
        return;
    }
    const auto res = Mailer::composeWithAttachment(QString(), tr("Termine"),
        tr("Im Anhang %1 Termin(e) als iCalendar-Datei.\n").arg(r.events), path);
    if (res == Mailer::Result::OpenedWithoutAttachment)
        QMessageBox::information(this, windowTitle(),
            tr("Das Mailprogramm wurde ohne Anhang geöffnet. Bitte die Datei anhängen:\n%1")
                .arg(QDir::toNativeSeparators(path)));
    else if (res == Mailer::Result::Failed)
        QMessageBox::warning(this, windowTitle(), tr("Es konnte kein Mailprogramm geöffnet werden."));
    accept();
}
