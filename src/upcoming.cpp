// SPDX-License-Identifier: MIT
#include "upcoming.h"
#include "appointmentdialog.h"
#include "database.h"
#include "reportprinter.h"
#include "session.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace Upcoming {

static QString tr(const char *s) { return QCoreApplication::translate("Upcoming", s); }

bool isMedication(const Appointment &a)
{
    return a.extra.contains(QStringLiteral("medicationId"));
}

QList<Appointment> list(qint64 patientId, const QDate &from, int days, bool withMedication)
{
    Database &db = Database::instance();
    if (!db.isOpen() || !patientId) return {};
    const QDateTime start(from, QTime(0, 0));
    const QDateTime end = days > 0 ? QDateTime(from.addDays(days), QTime(0, 0)) : QDateTime();
    const QString owner = Session::isAdmin() ? QString() : Session::user();
    QList<Appointment> l;
    for (const Appointment &a : db.appointments(start, end, owner, patientId, false))
        if (ApptUi::canSee(a) && (withMedication || !isMedication(a))) l << a;
    return l;
}

QString periodText(const QDate &from, int days)
{
    const QLocale loc;
    if (days <= 0) return tr("alle kommenden Termine ab %1").arg(loc.toString(from, QStringLiteral("dd.MM.yyyy")));
    if (days == 1) return loc.toString(from, QStringLiteral("dddd, dd.MM.yyyy"));
    return tr("%1 bis %2").arg(loc.toString(from, QStringLiteral("dd.MM.yyyy")),
                               loc.toString(from.addDays(days - 1), QStringLiteral("dd.MM.yyyy")));
}

static QString dayHeading(const QDate &d)
{
    const QLocale loc;
    const QDate today = QDate::currentDate();
    QString prefix;
    if (d == today) prefix = tr("Heute") + QStringLiteral(", ");
    else if (d == today.addDays(1)) prefix = tr("Morgen") + QStringLiteral(", ");
    return prefix + loc.toString(d, QStringLiteral("dddd, dd.MM.yyyy"));
}

static QString timeText(const Appointment &a)
{
    if (a.allDay) return tr("ganztägig");
    QString s = a.start.toString(QStringLiteral("HH:mm"));
    if (a.end.isValid() && a.end > a.start) {
        s += QStringLiteral("–");
        if (a.end.date() != a.start.date())
            s += QLocale().toString(a.end, QStringLiteral("dd.MM. HH:mm"));
        else
            s += a.end.toString(QStringLiteral("HH:mm"));
    }
    return s;
}

static QString reminderText(const Appointment &a)
{
    if (!a.hasReminder()) return QString();
    QStringList ch;
    if (a.remindVisual) ch << tr("optisch");
    if (a.remindSound) ch << tr("akustisch");
    if (a.remindEmail) ch << tr("E-Mail");
    return ApptUi::leadText(a.remindMinutes) + (ch.isEmpty() ? QString() : QStringLiteral(" (") + ch.join(QStringLiteral(", ")) + QLatin1Char(')'));
}

QString html(const QList<Appointment> &appts, const QString &period)
{
    QString h = QStringLiteral("<p><b>%1</b> %2 &nbsp;·&nbsp; %3</p>")
                    .arg(tr("Zeitraum:"), period.toHtmlEscaped(), tr("%1 offene(r) Termin(e)").arg(appts.size()));
    if (appts.isEmpty())
        return h + QStringLiteral("<p><i>%1</i></p>").arg(tr("Keine anstehenden Termine.").toHtmlEscaped());
    h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" border=\"1\" "
                        "style=\"border-collapse: collapse; border-color: #8899aa;\"><tr>");
    const QList<QPair<QString, QString>> heads = {
        { tr("Zeit"), QStringLiteral("12%") }, { tr("Termin"), QString() }, { tr("Priorität"), QStringLiteral("10%") },
        { tr("Ort"), QStringLiteral("17%") }, { tr("Zuständig"), QStringLiteral("10%") }, { tr("Erinnerung"), QStringLiteral("16%") } };
    for (const auto &[t, w] : heads)
        h += QStringLiteral("<th bgcolor=\"#e8eef4\" align=\"left\"%2>%1</th>")
                 .arg(t.toHtmlEscaped(), w.isEmpty() ? QString() : QStringLiteral(" width=\"%1\"").arg(w));
    h += QStringLiteral("</tr>");
    QDate day;
    for (const Appointment &a : appts) {
        const QDate d = a.start.date() < QDate::currentDate() ? QDate::currentDate() : a.start.date();
        if (d != day) {
            day = d;
            h += QStringLiteral("<tr><td colspan=\"6\" bgcolor=\"#f3f6f9\"><b>%1</b></td></tr>").arg(dayHeading(d).toHtmlEscaped());
        }
        QString title = (a.isSeries() ? QStringLiteral("↻ ") : QString()) + a.title;
        QString cellTitle = a.priority == Appointment::Urgent ? QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped())
                                                               : title.toHtmlEscaped();
        if (!a.notes.trimmed().isEmpty())
            cellTitle += QStringLiteral("<br><small>%1</small>").arg(a.notes.trimmed().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td><font color=\"%3\">●</font> %4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
                 .arg(timeText(a).toHtmlEscaped(), cellTitle, ApptUi::priorityColor(a.priority).name(),
                      ApptUi::priorityName(a.priority).toHtmlEscaped(), a.location.toHtmlEscaped(),
                      a.owner.toHtmlEscaped(), reminderText(a).toHtmlEscaped());
    }
    h += QStringLiteral("</table>");
    return h;
}

bool showOnStart()
{
    return QSettings().value(QStringLiteral("appointments/showOnStart"), true).toBool();
}

void setShowOnStart(bool on)
{
    QSettings().setValue(QStringLiteral("appointments/showOnStart"), on);
}

} // namespace Upcoming

// ---------------------------------------------------------------- Druckdialog

RemindersPrintDialog::RemindersPrintDialog(const Patient &p, QWidget *parent, int presetDays)
    : QDialog(parent), m_patient(p)
{
    setWindowTitle(tr("Erinnerungen drucken – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *info = new QLabel(tr("Druckt die anstehenden, noch offenen Termine von <b>%1</b> – "
                               "mit Zeit, Ort, Priorität, Notiz und Erinnerung.").arg(p.displayName().toHtmlEscaped()), this);
    info->setWordWrap(true);
    lay->addWidget(info);
    auto *form = new QFormLayout;
    m_period = new QComboBox(this);
    m_period->addItem(tr("heute und morgen"), 2);
    m_period->addItem(tr("nächste 7 Tage"), 7);
    m_period->addItem(tr("nächste 14 Tage"), 14);
    m_period->addItem(tr("nächste 30 Tage"), 30);
    m_period->addItem(tr("nächste 90 Tage"), 90);
    m_period->addItem(tr("alle kommenden"), 0);
    m_period->setCurrentIndex(std::max(0, m_period->findData(presetDays)));
    m_period->setObjectName(QStringLiteral("periodCombo"));
    form->addRow(tr("&Zeitraum:"), m_period);
    m_meds = new QCheckBox(tr("&Medikamententermine (Einnahme-Erinnerungen) einbeziehen"), this);
    m_meds->setChecked(true);
    form->addRow(QString(), m_meds);
    lay->addLayout(form);
    auto *count = new QLabel(this);
    lay->addWidget(count);
    auto update = [this, count] {
        count->setText(tr("<small>%1 Termin(e) im gewählten Zeitraum.</small>").arg(appointments().size()));
    };
    connect(m_period, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
    connect(m_meds, &QCheckBox::toggled, this, update);
    update();

    auto *bb = new QDialogButtonBox(this);
    QPushButton *preview = bb->addButton(tr("&Vorschau / Drucken…"), QDialogButtonBox::AcceptRole);
    QPushButton *pdf = bb->addButton(tr("Als &PDF…"), QDialogButtonBox::ActionRole);
    bb->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    preview->setDefault(true);
    connect(preview, &QPushButton::clicked, this, [this] { output(false); });
    connect(pdf, &QPushButton::clicked, this, [this] { output(true); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

QList<Appointment> RemindersPrintDialog::appointments() const
{
    return Upcoming::list(m_patient.id, QDate::currentDate(), m_period->currentData().toInt(), m_meds->isChecked());
}

QString RemindersPrintDialog::periodText() const
{
    return Upcoming::periodText(QDate::currentDate(), m_period->currentData().toInt());
}

void RemindersPrintDialog::output(bool pdf)
{
    const QString title = tr("Anstehende Termine");
    const QString h = Upcoming::html(appointments(), periodText());
    ReportPrinter::htmlReport(this, m_patient, title, h,
                              pdf ? ReportPrinter::Target::Pdf : ReportPrinter::Target::Preview, false);
    accept();
}

// ---------------------------------------------------------------- Hinweis beim Start

StartupAppointmentsDialog::StartupAppointmentsDialog(const Patient &p, const QList<Appointment> &appts, QWidget *parent)
    : QDialog(parent), m_patient(p)
{
    setWindowTitle(tr("Anstehende Termine"));
    setMinimumWidth(520);
    auto *lay = new QVBoxLayout(this);
    auto *head = new QLabel(tr("<h3>Termine heute und morgen</h3><p>für <b>%1</b></p>").arg(p.displayName().toHtmlEscaped()), this);
    lay->addWidget(head);

    const QLocale loc;
    const QDate today = QDate::currentDate();
    QString h;
    QDate day;
    for (const Appointment &a : appts) {
        const QDate d = a.start.date() < today ? today : a.start.date();
        if (d != day) {
            if (day.isValid()) h += QStringLiteral("</table>");
            day = d;
            h += QStringLiteral("<p style=\"margin-top:8px\"><b>%1</b></p><table cellspacing=\"0\" cellpadding=\"3\">")
                     .arg((d == today ? tr("Heute") : tr("Morgen")) + QStringLiteral(", ")
                          + loc.toString(d, QStringLiteral("dddd, dd.MM.yyyy")));
        }
        const bool past = !a.allDay && (a.end.isValid() ? a.end : a.start) < QDateTime::currentDateTime();
        QString what = (a.isSeries() ? QStringLiteral("↻ ") : QString()) + a.title.toHtmlEscaped();
        if (a.priority >= Appointment::High) what = QStringLiteral("<b>%1</b>").arg(what);
        QStringList more;
        if (!a.location.isEmpty()) more << a.location.toHtmlEscaped();
        if (!a.notes.trimmed().isEmpty()) more << a.notes.trimmed().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral(" · "));
        QString time = a.allDay ? tr("ganztägig") : a.start.toString(QStringLiteral("HH:mm"));
        h += QStringLiteral("<tr><td valign=\"top\"><font color=\"%1\">●</font></td>"
                            "<td valign=\"top\" style=\"padding-right:10px\"%5>%2</td><td%5>%3%4</td></tr>")
                 .arg(ApptUi::priorityColor(a.priority).name(), time, what,
                      more.isEmpty() ? QString() : QStringLiteral("<br><small>%1</small>").arg(more.join(QStringLiteral(" – "))),
                      past ? QStringLiteral(" style=\"color:gray\"") : QString());
    }
    if (day.isValid()) h += QStringLiteral("</table>");

    auto *body = new QLabel(h, this);
    body->setObjectName(QStringLiteral("upcomingList"));
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(std::min(420, 40 + 44 * int(appts.size())));
    lay->addWidget(scroll, 1);

    auto *again = new QCheckBox(tr("Beim Programmstart &anzeigen"), this);
    again->setObjectName(QStringLiteral("showOnStartCheck"));
    again->setChecked(Upcoming::showOnStart());
    connect(again, &QCheckBox::toggled, this, [](bool on) { Upcoming::setShowOnStart(on); });
    lay->addWidget(again);

    auto *bb = new QDialogButtonBox(this);
    QPushButton *print = bb->addButton(tr("&Drucken…"), QDialogButtonBox::ActionRole);
    QPushButton *planner = bb->addButton(tr("Termin&planer öffnen"), QDialogButtonBox::ActionRole);
    QPushButton *ok = bb->addButton(tr("&Schließen"), QDialogButtonBox::RejectRole);
    ok->setDefault(true);
    connect(print, &QPushButton::clicked, this, [this] {
        RemindersPrintDialog d(m_patient, this, 2);
        d.exec();
    });
    connect(planner, &QPushButton::clicked, this, [this] { emit openPlannerRequested(); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}
