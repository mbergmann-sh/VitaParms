// SPDX-License-Identifier: MIT
#include "reminderservice.h"
#include "appointmentdialog.h"
#include "database.h"
#include "glucoseprofile.h"
#include "quarterreminder.h"
#include "session.h"
#include "smtpclient.h"
#include "sounds.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

// ============================================================ Popup

ReminderPopup::ReminderPopup(const Appointment &a, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint), m_id(a.id)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Terminerinnerung"));
    const QColor pc = ApptUi::priorityColor(a.priority);
    auto *lay = new QVBoxLayout(this);

    auto *bar = new QFrame(this);
    bar->setFixedHeight(6);
    bar->setStyleSheet(QStringLiteral("background-color: %1;").arg(pc.name()));
    lay->addWidget(bar);

    const QLocale loc;
    const qint64 mins = QDateTime::currentDateTime().secsTo(a.start) / 60;
    QString when = a.allDay ? loc.toString(a.start.date(), QStringLiteral("dddd, dd.MM.yyyy")) + tr(" (ganztägig)")
                            : loc.toString(a.start, QStringLiteral("dddd, dd.MM.yyyy HH:mm"));
    QString rel;
    if (!a.allDay) {
        if (mins > 1) rel = tr("in %1 Minuten").arg(mins);
        else if (mins >= -1) rel = tr("jetzt");
        else rel = tr("vor %1 Minuten").arg(-mins);
    }
    auto *title = new QLabel(QStringLiteral("<span style=\"font-size:15pt;\"><b>%1</b></span>").arg(a.title.toHtmlEscaped()), this);
    title->setWordWrap(true);
    lay->addWidget(title);
    QString info = QStringLiteral("<b>%1</b>").arg(when.toHtmlEscaped());
    if (!rel.isEmpty()) info += QStringLiteral(" – ") + rel;
    info += QStringLiteral("<br>") + tr("Priorität: <span style=\"color:%1;\"><b>%2</b></span>")
                                         .arg(pc.name(), ApptUi::priorityName(a.priority));
    if (!a.location.isEmpty()) info += QStringLiteral("<br>") + tr("Ort: %1").arg(a.location.toHtmlEscaped());
    if (a.patientId)
        if (const auto p = Database::instance().patient(a.patientId))
            info += QStringLiteral("<br>") + tr("Person: %1").arg(p->displayName().toHtmlEscaped());
    if (!a.notes.isEmpty()) info += QStringLiteral("<br><i>%1</i>").arg(a.notes.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    auto *infoLabel = new QLabel(info, this);
    infoLabel->setWordWrap(true);
    lay->addWidget(infoLabel);

    auto *row = new QHBoxLayout;
    m_snooze = new QComboBox(this);
    for (int m : { 5, 10, 15, 30, 60 }) m_snooze->addItem(tr("in %1 Minuten").arg(m), m);
    auto *snooze = new QPushButton(tr("&Später erinnern"), this);
    auto *open = new QPushButton(tr("Ö&ffnen…"), this);
    auto *done = new QPushButton(tr("&Erledigt"), this);
    QPushButton *measure = nullptr;
    if (GlucoseProfile::isProfileAppointment(a)) {
        measure = new QPushButton(tr("&Messung erfassen…"), this);
        measure->setObjectName(QStringLiteral("measureButton"));
        connect(measure, &QPushButton::clicked, this, [this] {
            emit measureRequested(m_id);
            close();
        });
    }
    auto *ok = new QPushButton(tr("&OK"), this);
    ok->setDefault(true);
    row->addWidget(snooze);
    row->addWidget(m_snooze);
    row->addStretch();
    row->addWidget(open);
    row->addWidget(done);
    if (measure) {
        row->addWidget(measure);
        measure->setDefault(true);
        ok->setDefault(false);
    }
    row->addWidget(ok);
    lay->addLayout(row);

    connect(ok, &QPushButton::clicked, this, &QWidget::close);
    connect(snooze, &QPushButton::clicked, this, [this] {
        Database::instance().snoozeAppointment(m_id, QDateTime::currentDateTime().addSecs(m_snooze->currentData().toInt() * 60));
        close();
    });
    connect(done, &QPushButton::clicked, this, [this] {
        Database::instance().setAppointmentDone(m_id, true);
        close();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        emit openRequested(m_id);
        close();
    });

    resize(460, sizeHint().height());
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        move(g.right() - width() - 24, g.bottom() - height() - 48);
    }
}

// ============================================================ Dienst

ReminderService::ReminderService(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(20000);
    connect(&m_timer, &QTimer::timeout, this, &ReminderService::checkNow);
    connect(&Database::instance(), &Database::appointmentsChanged, this, [this] {
        // Erinnerungen geänderter/gelöschter Termine nicht stehen lassen
        for (auto it = m_popups.begin(); it != m_popups.end(); ++it)
            if (it.value()) {
                const auto a = Database::instance().appointment(it.key());
                if (!a || a->done) it.value()->close();
            }
    });
}

void ReminderService::setEnabled(bool on)
{
    if (on) {
        m_timer.start();
        QTimer::singleShot(1500, this, &ReminderService::checkNow);   // verpasste Erinnerungen nach dem Start
    } else {
        m_timer.stop();
        closeAllPopups();
    }
}

void ReminderService::closeAllPopups()
{
    for (auto &p : m_popups) if (p) p->close();
    m_popups.clear();
    if (m_quarter) m_quarter->close();
}

int ReminderService::openPopups() const
{
    int n = 0;
    for (const auto &p : m_popups) if (p) ++n;
    return n;
}

void ReminderService::checkNow()
{
    if (!Session::isLoggedIn() || !Database::instance().isOpen()) return;
    const QDateTime now = QDateTime::currentDateTime();
    // Unbegrenzte Terminserien stündlich fortschreiben
    if (!m_lastExtend.isValid() || m_lastExtend.secsTo(now) > 3600) {
        m_lastExtend = now;
        Database::instance().extendSeries();
    }
    for (const Appointment &a : Database::instance().pendingReminders(Session::user(), now))
        fire(a);
    checkQuarter();
}

void ReminderService::checkQuarter()
{
    QDate q;
    const QDate today = QDate::currentDate();
    if (!Session::isLoggedIn() || !QuarterReminder::due(today, &q)) return;
    const QString shown = QuarterReminder::key(q) + QLatin1Char('@') + today.toString(Qt::ISODate);
    if (m_quarter || m_quarterShown == shown) return;      // je Sitzung und Tag einmal
    m_quarterShown = shown;
    m_quarter = new QuarterPopup(q);
    m_quarter->show();
    m_quarter->raise();
    QApplication::alert(m_quarter);
    Sounds::play(Sounds::Kind::Reminder);
}

// ============================================================ Quartalswechsel

QuarterPopup::QuarterPopup(const QDate &q, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("QuarterPopup"));
    setWindowTitle(tr("Quartalswechsel"));
    auto *lay = new QVBoxLayout(this);
    auto *bar = new QFrame(this);
    bar->setFixedHeight(6);
    bar->setStyleSheet(QStringLiteral("background-color: %1;").arg(ApptUi::priorityColor(Appointment::High).name()));
    lay->addWidget(bar);
    auto *title = new QLabel(QStringLiteral("<span style=\"font-size:15pt;\"><b>%1</b></span>")
                                 .arg(QuarterReminder::title(q).toHtmlEscaped()), this);
    title->setWordWrap(true);
    lay->addWidget(title);
    auto *text = new QLabel(QuarterReminder::message(q, QDate::currentDate()), this);
    text->setObjectName(QStringLiteral("quarterText"));
    text->setWordWrap(true);
    lay->addWidget(text);
    auto *hint = new QLabel(tr("<small>Die Erinnerung erscheint zwei Tage vor bis zwei Tage nach dem Quartalswechsel, "
                               "bis Sie sie bestätigen. Abschalten: Extras → Einstellungen.</small>"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *row = new QHBoxLayout;
    auto *later = new QPushButton(tr("&Morgen wieder erinnern"), this);
    auto *done = new QPushButton(tr("&Erledigt – nicht mehr erinnern"), this);
    done->setObjectName(QStringLiteral("quarterDoneButton"));
    done->setDefault(true);
    row->addWidget(later);
    row->addStretch();
    row->addWidget(done);
    lay->addLayout(row);
    connect(later, &QPushButton::clicked, this, [this] {
        QuarterReminder::snoozeUntil(QDate::currentDate().addDays(1));
        close();
    });
    connect(done, &QPushButton::clicked, this, [this, q] {
        QuarterReminder::confirm(q);
        close();
    });
    resize(480, sizeHint().height());
    if (QScreen *s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        move(g.right() - width() - 24, g.bottom() - height() - 48);
    }
}

QString ReminderService::mailSubject(const Appointment &a)
{
    const QString prio = a.priority >= Appointment::High ? QStringLiteral(" [") + ApptUi::priorityName(a.priority) + QLatin1Char(']')
                                                          : QString();
    return tr("VitaParms-Erinnerung: %1 am %2").arg(a.title, QLocale().toString(a.start,
               a.allDay ? QStringLiteral("dd.MM.yyyy") : QStringLiteral("dd.MM.yyyy HH:mm"))) + prio;
}

QString ReminderService::mailBody(const Appointment &a, bool details)
{
    const QLocale loc;
    QString b = tr("Erinnerung an einen Termin:\n\n");
    b += tr("Termin:    %1\n").arg(a.title);
    b += tr("Beginn:    %1\n").arg(loc.toString(a.start, a.allDay ? QStringLiteral("dddd, dd.MM.yyyy") : QStringLiteral("dddd, dd.MM.yyyy HH:mm")));
    b += tr("Priorität: %1\n").arg(ApptUi::priorityName(a.priority));
    if (details) {
        if (!a.location.isEmpty()) b += tr("Ort:       %1\n").arg(a.location);
        if (a.patientId)
            if (const auto p = Database::instance().patient(a.patientId))
                b += tr("Person:    %1\n").arg(p->displayName());
        if (!a.notes.isEmpty()) b += tr("\nNotiz:\n%1\n").arg(a.notes);
    }
    b += tr("\n-- \nAutomatisch gesendet von VitaParms.\n");
    return b;
}

void ReminderService::fire(const Appointment &a)
{
    // Sofort als erinnert markieren – verhindert Doppelungen
    Database::instance().markReminded(a.id, QDateTime::currentDateTime());

    if (a.remindVisual || (!a.remindSound && !a.remindEmail)) {
        if (m_popups.value(a.id)) m_popups.value(a.id)->close();
        auto *p = new ReminderPopup(a);
        connect(p, &ReminderPopup::openRequested, this, &ReminderService::openRequested);
        connect(p, &ReminderPopup::measureRequested, this, &ReminderService::measureRequested);
        m_popups.insert(a.id, p);
        p->show();
        p->raise();
        p->activateWindow();
        QApplication::alert(p);
    }
    if (a.remindSound)
        Sounds::play(Sounds::Kind::Reminder);
    if (a.remindEmail) {
        const Smtp::Config cfg = Smtp::loadConfig();
        if (!cfg.isValid()) {
            emit statusMessage(tr("E-Mail-Erinnerung für „%1“ nicht gesendet: E-Mail-Versand nicht eingerichtet.").arg(a.title));
            return;
        }
        const QString title = a.title;
        Smtp::sendAsync(cfg, a.emailTo, mailSubject(a), mailBody(a, cfg.includeDetails), this,
                        [this, title](bool ok, const QString &err) {
            emit statusMessage(ok ? tr("E-Mail-Erinnerung für „%1“ gesendet.").arg(title)
                                  : tr("E-Mail-Erinnerung für „%1“ fehlgeschlagen: %2").arg(title, err));
        });
    }
}
