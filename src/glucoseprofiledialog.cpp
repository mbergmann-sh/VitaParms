// VitaParms – Messwerte → BZ-Tagesprofil (siehe glucoseprofile.h)
// SPDX-License-Identifier: MIT
#include "glucoseprofiledialog.h"

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

GlucoseProfileDialog::GlucoseProfileDialog(const Patient &patient, QWidget *parent)
    : QDialog(parent), m_patient(patient), m_points(GlucoseProfile::points())
{
    setObjectName(QStringLiteral("GlucoseProfileDialog"));
    setWindowTitle(tr("BZ-Tagesprofil – %1").arg(patient.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("VitaParms erinnert zu den gewählten Zeitpunkten an die Blutzuckermessung und öffnet auf "
                                "Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Die Erinnerungen "
                                "stehen als Termine im Terminplaner (und kommen mit dem Abgleich aufs Tablet)."), this);
    intro->setWordWrap(true);
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
        const QTime std[] = { QTime(7, 0), QTime(9, 30), QTime(12, 0), QTime(14, 30), QTime(18, 0), QTime(20, 30), QTime(22, 0) };
        for (int i = 0; i < m_times.size() && i < 7; ++i) { m_times[i]->setTime(std[i]); m_checks[i]->setChecked(true); }
    });
    grid->addWidget(reset, int(m_points.size()), 0, 1, 2, Qt::AlignLeft);
    grid->setColumnStretch(0, 1);
    lay->addWidget(box);

    m_sound = new QCheckBox(tr("Erinnerung mit &Ton"), this);
    m_sound->setChecked(QSettings().value(QStringLiteral("bzprofile/sound"), true).toBool());
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
        const int n = GlucoseProfile::cancel(m_patient.id, m_date->date());
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
    const auto have = GlucoseProfile::existing(m_patient.id, m_date->date());
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
    GlucoseProfile::savePoints(pts);
    QSettings().setValue(QStringLiteral("bzprofile/sound"), m_sound->isChecked());
    QString err;
    const int n = GlucoseProfile::create(m_patient.id, m_date->date(), pts, m_sound->isChecked(), &err);
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
