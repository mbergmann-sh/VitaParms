// VitaParms – Messwerte → BZ-Tagesprofil (siehe glucoseprofile.h)
// SPDX-License-Identifier: MIT
#include "glucoseprofiledialog.h"
#include "edition.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimeEdit>
#include <QVBoxLayout>

GlucoseProfileDialog::GlucoseProfileDialog(const Patient &patient, QWidget *parent, GlucoseProfile::Kind kind)
    : QDialog(parent), m_patient(patient), m_kind(kind), m_points(GlucoseProfile::points(kind))
{
    const bool rr = kind == GlucoseProfile::Kind::BloodPressure;
    setObjectName(rr ? QStringLiteral("BpProfileDialog") : QStringLiteral("GlucoseProfileDialog"));
    setWindowTitle(QStringLiteral("%1 – %2").arg(GlucoseProfile::subject(kind), patient.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(rr
        ? tr("%1 erinnert zu den gewählten Zeitpunkten an die Blutdruckmessung und öffnet auf "
             "Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Messen Sie nach 5 Minuten "
             "Ruhe im Sitzen und immer am selben Arm. Die Erinnerungen stehen als Termine im Terminplaner "
             "(und kommen mit dem Abgleich aufs Tablet).").arg(Edition::appName())
        : tr("%1 erinnert zu den gewählten Zeitpunkten an die Blutzuckermessung und öffnet auf "
             "Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Die Erinnerungen "
             "stehen als Termine im Terminplaner (und kommen mit dem Abgleich aufs Tablet).").arg(Edition::appName()), this);
    intro->setWordWrap(true);
    // Für wen? Deutlich oben anzeigen, damit eine Verwechslung vor dem Start auffällt
    auto *who = new QLabel(QStringLiteral("<span style=\"font-size:13pt;\"><b>%1</b></span>")
                               .arg(tr("Für: %1").arg(patient.displayName()).toHtmlEscaped()), this);
    who->setObjectName(QStringLiteral("profilePersonLabel"));
    lay->addWidget(who);
    lay->addWidget(intro);

    auto *dayRow = new QHBoxLayout;
    auto *dayLabel = new QLabel(tr("&Tag:"), this);
    m_date = new QDateEdit(this);
    m_date->setObjectName(QStringLiteral("profileDate"));
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat(QStringLiteral("dddd, dd.MM.yyyy"));
    // nach dem letzten Zeitpunkt des Tages: morgen vorschlagen
    QTime last;
    for (const auto &p : m_points) if (p.active && p.time > last) last = p.time;
    m_date->setDate(QTime::currentTime() > last ? QDate::currentDate().addDays(1) : QDate::currentDate());
    m_date->setMinimumDate(QDate::currentDate());
    dayLabel->setBuddy(m_date);
    dayRow->addWidget(dayLabel);
    dayRow->addWidget(m_date);
    dayRow->addStretch();
    lay->addLayout(dayRow);

    auto *box = new QGroupBox(tr("Zeitpunkte"), this);
    auto *grid = new QGridLayout(box);
    for (int i = 0; i < m_points.size(); ++i) {
        auto *c = new QCheckBox(m_points[i].label, box);
        c->setObjectName(QStringLiteral("profileCheck%1").arg(i));
        c->setChecked(m_points[i].active);
        auto *t = new QTimeEdit(m_points[i].time, box);
        t->setObjectName(QStringLiteral("profileTime%1").arg(i));
        t->setDisplayFormat(QStringLiteral("HH:mm"));
        t->setEnabled(c->isChecked());
        connect(c, &QCheckBox::toggled, t, &QWidget::setEnabled);
        connect(c, &QCheckBox::toggled, this, &GlucoseProfileDialog::updateInfo);
        connect(t, &QTimeEdit::timeChanged, this, &GlucoseProfileDialog::updateInfo);
        grid->addWidget(c, i, 0);
        grid->addWidget(t, i, 1);
        m_checks << c;
        m_times << t;
    }
    auto *reset = new QPushButton(tr("Standardzeiten"), box);
    connect(reset, &QPushButton::clicked, this, [this] {
        const QList<GlucoseProfile::Point> std = GlucoseProfile::defaultPoints(m_kind);
        for (int i = 0; i < m_times.size() && i < std.size(); ++i) { m_times[i]->setTime(std[i].time); m_checks[i]->setChecked(true); }
    });
    grid->addWidget(reset, int(m_points.size()), 0, 1, 2, Qt::AlignLeft);
    grid->setColumnStretch(0, 1);
    lay->addWidget(box);

    m_sound = new QCheckBox(tr("Erinnerung mit &Ton"), this);
    m_sound->setChecked(QSettings().value(GlucoseProfile::settingsKey(kind) + QStringLiteral("/sound"), true).toBool());
    lay->addWidget(m_sound);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("profileInfo"));
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    auto *buttons = new QDialogButtonBox(this);
    auto *start = buttons->addButton(tr("Tagesprofil &starten"), QDialogButtonBox::AcceptRole);
    start->setObjectName(QStringLiteral("profileStartButton"));
    auto *stop = buttons->addButton(tr("Tagesprofil &beenden"), QDialogButtonBox::ActionRole);
    stop->setObjectName(QStringLiteral("profileStopButton"));
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &GlucoseProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(stop, &QPushButton::clicked, this, [this] {
        const int n = GlucoseProfile::cancel(m_patient.id, m_date->date(), m_kind);
        m_info->setText(tr("%1 offene Erinnerung(en) für diesen Tag gelöscht.").arg(n));
    });
    connect(m_date, &QDateEdit::dateChanged, this, &GlucoseProfileDialog::updateInfo);
    lay->addWidget(buttons);
    updateInfo();
}

QList<GlucoseProfile::Point> GlucoseProfileDialog::collect() const
{
    QList<GlucoseProfile::Point> l = m_points;
    for (int i = 0; i < l.size(); ++i) {
        l[i].active = m_checks[i]->isChecked();
        l[i].time = m_times[i]->time();
    }
    return l;
}

void GlucoseProfileDialog::updateInfo()
{
    const auto have = GlucoseProfile::existing(m_patient.id, m_date->date(), m_kind);
    int open = 0;
    for (const Appointment &a : have) if (!a.done) ++open;
    QString t;
    if (open)
        t = tr("Für diesen Tag sind bereits %1 Erinnerung(en) gestellt – „Tagesprofil starten“ ersetzt sie.").arg(open);
    if (m_date->date() == QDate::currentDate()) {
        int past = 0;
        for (const auto &p : collect()) if (p.active && p.time < QTime::currentTime()) ++past;
        if (past) t += (t.isEmpty() ? QString() : QStringLiteral(" ")) + tr("%1 Zeitpunkt(e) liegen heute schon zurück und entfallen.").arg(past);
    }
    m_info->setText(t);
}

void GlucoseProfileDialog::accept()
{
    const QList<GlucoseProfile::Point> pts = collect();
    GlucoseProfile::savePoints(pts, m_kind);
    QSettings().setValue(GlucoseProfile::settingsKey(m_kind) + QStringLiteral("/sound"), m_sound->isChecked());
    QString err;
    const int n = GlucoseProfile::create(m_patient.id, m_date->date(), pts, m_sound->isChecked(), &err, m_kind);
    if (n < 0) {
        QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
        return;
    }
    if (n == 0) {
        QMessageBox::information(this, windowTitle(), tr("Für diesen Tag liegt kein gewählter Zeitpunkt in der Zukunft."));
        return;
    }
    m_created = n;
    QDialog::accept();
}
