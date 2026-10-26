// SPDX-License-Identifier: MIT
#include "appointmentdialog.h"
#include <QCoreApplication>
#include "database.h"
#include "formloader.h"
#include "session.h"
#include "smtpclient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace ApptUi {

SeriesScope askSeriesScope(QWidget *parent, const Appointment &a, bool deleting, bool allowOnlyThis)
{
    QMessageBox mb(QMessageBox::Question,
                   deleting ? QCoreApplication::translate("ApptUi", "Termin löschen")
                            : QCoreApplication::translate("ApptUi", "Terminserie ändern"),
                   deleting ? QCoreApplication::translate("ApptUi", "„%1“ am %2 gehört zu einer Terminserie.\n"
                                                                    "Welche Termine sollen gelöscht werden?")
                                  .arg(a.title, QLocale().toString(a.seriesOrig.isValid() ? a.seriesOrig : a.start,
                                                                   QStringLiteral("dd.MM.yyyy")))
                            : QCoreApplication::translate("ApptUi", "„%1“ gehört zu einer Terminserie.\n"
                                                                    "Für welche Termine sollen die Änderungen gelten?").arg(a.title),
                   QMessageBox::NoButton, parent);
    if (!allowOnlyThis)
        mb.setInformativeText(QCoreApplication::translate("ApptUi",
            "Eine geänderte Wiederholung gilt immer für mehrere Termine."));
    QPushButton *only = allowOnlyThis
        ? mb.addButton(QCoreApplication::translate("ApptUi", "&Nur diesen Termin"), QMessageBox::AcceptRole) : nullptr;
    QPushButton *following = mb.addButton(QCoreApplication::translate("ApptUi", "&Diesen und alle folgenden"),
                                          QMessageBox::AcceptRole);
    QPushButton *all = mb.addButton(QCoreApplication::translate("ApptUi", "&Alle Termine der Serie"),
                                    QMessageBox::AcceptRole);
    QPushButton *cancel = mb.addButton(QCoreApplication::translate("ApptUi", "Abbrechen"), QMessageBox::RejectRole);
    mb.setDefaultButton(only ? only : following);
    mb.setEscapeButton(cancel);
    mb.exec();
    if (only && mb.clickedButton() == only) return SeriesScope::OnlyThis;
    if (mb.clickedButton() == following) return SeriesScope::Following;
    if (mb.clickedButton() == all) return SeriesScope::All;
    return SeriesScope::Cancel;
}

bool deleteWithPrompt(QWidget *parent, const Appointment &a)
{
    QString err;
    SeriesScope scope = SeriesScope::OnlyThis;
    if (a.isSeries()) {
        scope = askSeriesScope(parent, a, true);
        if (scope == SeriesScope::Cancel) return false;
    } else if (QMessageBox::question(parent, QCoreApplication::translate("ApptUi", "Termin löschen"),
                   QCoreApplication::translate("ApptUi", "Termin „%1“ am %2 löschen?").arg(a.title,
                       QLocale().toString(a.start, QStringLiteral("dd.MM.yyyy HH:mm")))) != QMessageBox::Yes) {
        return false;
    }
    const bool ok = deleteWithScope(a, scope, &err);
    if (!ok) QMessageBox::critical(parent, QCoreApplication::translate("ApptUi", "Termin löschen"), err);
    return ok;
}

} // namespace ApptUi

AppointmentDialog::AppointmentDialog(const Appointment &a, QWidget *parent)
    : QDialog(parent), m_appt(a)
{
    setWindowTitle(a.id ? tr("Termin bearbeiten") : tr("Neuer Termin"));
    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("appointmentform"), this);
    lay->addWidget(m_form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("&Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AppointmentDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AppointmentDialog::reject);
    lay->addWidget(buttons);

    // Auswahllisten
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("patientCombo"))) {
        c->clear();
        c->addItem(tr("— keine Person —"), qint64(0));
        for (const Patient &p : Database::instance().patients())
            c->addItem(p.displayName(), p.id);
    }
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("ownerCombo"))) {
        c->clear();
        for (const QString &n : ApptUi::accountNames()) c->addItem(n, n);
        if (c->findData(a.owner) < 0 && !a.owner.isEmpty()) c->addItem(a.owner, a.owner);
        c->setEnabled(Session::isAdmin());      // Anwender legen Termine nur für sich an
    }
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("priorityCombo"))) {
        c->clear();
        for (int p = Appointment::Low; p <= Appointment::Urgent; ++p) {
            QPixmap dot(12, 12);
            dot.fill(ApptUi::priorityColor(p));
            c->addItem(QIcon(dot), ApptUi::priorityName(p), p);
        }
    }
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("remindCombo"))) {
        c->clear();
        for (const auto &[m, text] : ApptUi::leadTimes()) c->addItem(text, m);
        if (c->findData(a.remindMinutes) < 0) c->addItem(ApptUi::leadText(a.remindMinutes), a.remindMinutes);
    }

    FormIO::write(m_form, QStringLiteral("titleEdit"), a.title);
    FormIO::write(m_form, QStringLiteral("startEdit"), a.start);
    FormIO::write(m_form, QStringLiteral("endEdit"), a.end.isValid() ? a.end : a.start.addSecs(3600));
    FormIO::write(m_form, QStringLiteral("allDayCheck"), a.allDay);
    FormIO::write(m_form, QStringLiteral("locationEdit"), a.location);
    FormIO::write(m_form, QStringLiteral("patientCombo"), a.patientId);
    FormIO::write(m_form, QStringLiteral("ownerCombo"), a.owner.isEmpty() ? Session::user() : a.owner);
    FormIO::write(m_form, QStringLiteral("priorityCombo"), a.priority);
    FormIO::write(m_form, QStringLiteral("notesEdit"), a.notes);
    FormIO::write(m_form, QStringLiteral("remindCombo"), a.remindMinutes);
    FormIO::write(m_form, QStringLiteral("remindVisualCheck"), a.remindVisual);
    FormIO::write(m_form, QStringLiteral("remindSoundCheck"), a.remindSound);
    FormIO::write(m_form, QStringLiteral("remindEmailCheck"), a.remindEmail);
    FormIO::write(m_form, QStringLiteral("emailToEdit"),
                  a.emailTo.isEmpty() ? QSettings().value(QStringLiteral("appointments/lastEmail")).toString() : a.emailTo);
    FormIO::write(m_form, QStringLiteral("doneCheck"), a.done);
    FormIO::applyExtra(m_form, a.extra);
    setupRecurrence();

    // Ende folgt dem Beginn; Felder je nach Auswahl aktivieren
    if (auto *st = m_form->findChild<QDateTimeEdit *>(QStringLiteral("startEdit"))) {
        connect(st, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &newStart) {
            auto *en = m_form->findChild<QDateTimeEdit *>(QStringLiteral("endEdit"));
            if (en && en->dateTime() <= newStart) en->setDateTime(newStart.addSecs(3600));
        });
    }
    for (const char *n : { "allDayCheck", "remindEmailCheck" })
        if (auto *c = m_form->findChild<QCheckBox *>(QLatin1String(n)))
            connect(c, &QCheckBox::toggled, this, &AppointmentDialog::updateEnabled);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("remindCombo")))
        connect(c, &QComboBox::currentIndexChanged, this, &AppointmentDialog::updateEnabled);

    const bool smtp = Smtp::loadConfig().isValid();
    FormIO::write(m_form, QStringLiteral("reminderHintLabel"), smtp
        ? tr("<small>Erinnerungen erscheinen, solange VitaParms läuft und das zuständige Konto angemeldet "
             "ist. E-Mails sind nicht verschlüsselt – vertrauliche Details besser in die Notiz statt in den "
             "Titel schreiben.</small>")
        : tr("<small>Erinnerungen erscheinen, solange VitaParms läuft und das zuständige Konto angemeldet "
             "ist. Für E-Mail-Erinnerungen muss ein Administrator unter <i>Extras → Einstellungen → "
             "E-Mail-Versand</i> den Postausgangsserver einrichten.</small>"));
    updateEnabled();
    if (auto *t = m_form->findChild<QWidget *>(QStringLiteral("titleEdit"))) t->setFocus();
    resize(620, 820);
}

void AppointmentDialog::updateEnabled()
{
    const bool allDay = FormIO::read(m_form, QStringLiteral("allDayCheck")).value_or(false).toBool();
    if (auto *st = m_form->findChild<QDateTimeEdit *>(QStringLiteral("startEdit")))
        st->setDisplayFormat(allDay ? QStringLiteral("dd.MM.yyyy") : QStringLiteral("dd.MM.yyyy HH:mm"));
    if (auto *en = m_form->findChild<QDateTimeEdit *>(QStringLiteral("endEdit"))) {
        en->setDisplayFormat(allDay ? QStringLiteral("dd.MM.yyyy") : QStringLiteral("dd.MM.yyyy HH:mm"));
    }
    const int lead = FormIO::read(m_form, QStringLiteral("remindCombo")).value_or(-1).toInt();
    for (const char *n : { "remindVisualCheck", "remindSoundCheck", "remindEmailCheck" })
        if (auto *w = m_form->findChild<QWidget *>(QLatin1String(n))) w->setEnabled(lead >= 0);
    const bool mail = lead >= 0 && FormIO::read(m_form, QStringLiteral("remindEmailCheck")).value_or(false).toBool();
    if (auto *w = m_form->findChild<QWidget *>(QStringLiteral("emailToEdit"))) w->setEnabled(mail);
}

void AppointmentDialog::accept()
{
    Appointment a = m_appt;
    a.title = FormIO::readString(m_form, QStringLiteral("titleEdit"), a.title).trimmed();
    if (a.title.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte einen Titel angeben."));
        return;
    }
    if (const auto v = FormIO::read(m_form, QStringLiteral("startEdit"))) a.start = v->toDateTime();
    if (const auto v = FormIO::read(m_form, QStringLiteral("endEdit"))) a.end = v->toDateTime();
    a.allDay = FormIO::read(m_form, QStringLiteral("allDayCheck")).value_or(a.allDay).toBool();
    if (a.allDay) {
        a.start = QDateTime(a.start.date(), QTime(0, 0));
        a.end = QDateTime(std::max(a.end.date(), a.start.date()), QTime(23, 59));
    }
    if (a.end.isValid() && a.end < a.start) {
        QMessageBox::information(this, windowTitle(), tr("Das Ende liegt vor dem Beginn."));
        return;
    }
    a.location = FormIO::readString(m_form, QStringLiteral("locationEdit"), a.location).trimmed();
    a.patientId = FormIO::read(m_form, QStringLiteral("patientCombo")).value_or(a.patientId).toLongLong();
    a.owner = FormIO::readString(m_form, QStringLiteral("ownerCombo"), a.owner);
    if (a.owner.isEmpty() || !Session::isAdmin()) a.owner = a.owner.isEmpty() ? Session::user() : a.owner;
    if (!Session::isAdmin()) a.owner = Session::user();
    a.priority = FormIO::read(m_form, QStringLiteral("priorityCombo")).value_or(a.priority).toInt();
    a.notes = FormIO::readString(m_form, QStringLiteral("notesEdit"), a.notes).trimmed();
    const int oldLead = a.remindMinutes;
    const QDateTime oldStart = m_appt.start;
    a.remindMinutes = FormIO::read(m_form, QStringLiteral("remindCombo")).value_or(a.remindMinutes).toInt();
    a.remindVisual = FormIO::read(m_form, QStringLiteral("remindVisualCheck")).value_or(a.remindVisual).toBool();
    a.remindSound = FormIO::read(m_form, QStringLiteral("remindSoundCheck")).value_or(a.remindSound).toBool();
    a.remindEmail = FormIO::read(m_form, QStringLiteral("remindEmailCheck")).value_or(a.remindEmail).toBool();
    a.emailTo = FormIO::readString(m_form, QStringLiteral("emailToEdit"), a.emailTo).trimmed();
    a.done = FormIO::read(m_form, QStringLiteral("doneCheck")).value_or(a.done).toBool();
    a.extra = FormIO::collectExtra(m_form, a.extra);

    if (a.remindMinutes >= 0 && a.remindEmail) {
        if (!Smtp::isValidAddress(a.emailTo)) {
            QMessageBox::information(this, windowTitle(), tr("Bitte eine gültige E-Mail-Adresse für die Erinnerung angeben."));
            return;
        }
        if (!Smtp::loadConfig().isValid())
            QMessageBox::information(this, windowTitle(),
                tr("Der E-Mail-Versand ist noch nicht eingerichtet. Die E-Mail-Erinnerung wird erst "
                   "versendet, wenn ein Administrator den Postausgangsserver hinterlegt hat."));
        QSettings().setValue(QStringLiteral("appointments/lastEmail"), a.emailTo);
    }
    if (a.remindMinutes >= 0 && !a.remindVisual && !a.remindSound && !a.remindEmail) {
        QMessageBox::information(this, windowTitle(), tr("Bitte mindestens eine Art der Erinnerung wählen."));
        return;
    }
    // Zeit oder Vorwarnung geändert → Erinnerung neu scharf schalten
    if (a.id == 0 || oldLead != a.remindMinutes || oldStart != a.start) {
        a.remindedAt = {};
        a.snoozeUntil = {};
    }
    if (a.id == 0) a.createdBy = Session::user();

    QString err;
    bool cancelled = false;
    if (!saveSeriesAware(a, &err, &cancelled)) {
        if (!cancelled)
            QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
        return;
    }
    m_appt = a;
    QDialog::accept();
}

// ============================================================ Wiederholung

namespace {
enum RepeatChoice { RepNone, RepDaily, RepWorkdays, RepWeekly, RepMonthly, RepYearly, RepEveryNWeeks, RepEveryNDays };
enum EndChoice { EndNever, EndUntil, EndCount };
}

bool AppointmentDialog::hasRecurrenceUi() const
{
    return m_form->findChild<QComboBox *>(QStringLiteral("repeatCombo")) != nullptr;
}

void AppointmentDialog::setupRecurrence()
{
    auto *rep = m_form->findChild<QComboBox *>(QStringLiteral("repeatCombo"));
    if (m_appt.isSeries()) {
        m_series = Database::instance().series(m_appt.seriesId);
        if (m_series) m_origRule = m_series->rule;
    }
    m_prevStart = m_appt.start.date();
    if (!rep) return;   // angepasstes Formular ohne Wiederholung

    m_updating = true;
    rep->clear();
    rep->addItem(tr("keine"), RepNone);
    rep->addItem(tr("täglich"), RepDaily);
    rep->addItem(tr("alle … Tage"), RepEveryNDays);
    rep->addItem(tr("werktags (Mo–Fr)"), RepWorkdays);
    rep->addItem(tr("wöchentlich"), RepWeekly);
    rep->addItem(tr("alle … Wochen (am Wochentag)"), RepEveryNWeeks);
    rep->addItem(tr("monatlich"), RepMonthly);
    rep->addItem(tr("jährlich"), RepYearly);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("repeatEndCombo"))) {
        c->clear();
        c->addItem(tr("ohne Ende"), EndNever);
        c->addItem(tr("bis einschließlich"), EndUntil);
        c->addItem(tr("nach Anzahl Terminen"), EndCount);
    }
    const QLocale loc;
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i))) {
            cb->setText(loc.dayName(i, QLocale::ShortFormat));
            connect(cb, &QCheckBox::toggled, this, &AppointmentDialog::updateRecurrenceUi);
        }
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("repeatCountSpin")))
        sp->setSuffix(tr(" Termine"));

    // Vorbelegen
    const Recurrence &r = m_origRule;
    int choice = RepNone;
    switch (r.freq) {
    case Recurrence::Daily:   choice = r.interval > 1 ? RepEveryNDays : RepDaily; break;
    case Recurrence::Weekly:  choice = (r.weekdays == 0x1f && r.interval == 1) ? RepWorkdays
                                     : r.interval > 1 ? RepEveryNWeeks : RepWeekly; break;
    case Recurrence::Monthly: choice = RepMonthly; break;
    case Recurrence::Yearly:  choice = RepYearly; break;
    default: break;
    }
    rep->setCurrentIndex(rep->findData(choice));
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("intervalSpin"))) sp->setValue(r.interval);
    const int mask = r.freq == Recurrence::Weekly ? r.weekdays : (1 << (m_appt.start.date().dayOfWeek() - 1));
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i)))
            cb->setChecked(mask & (1 << (i - 1)));
    const QDate base = m_series ? m_series->dtstart.date() : m_appt.start.date();
    fillMonthModes(base);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("monthModeCombo")))
        c->setCurrentIndex(std::max(0, c->findData(r.monthMode)));
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("repeatEndCombo")))
        c->setCurrentIndex(c->findData(r.count > 0 ? EndCount : r.until.isValid() ? EndUntil : EndNever));
    if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("repeatUntilEdit")))
        d->setDate(r.until.isValid() ? r.until : m_appt.start.date().addMonths(3));
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("repeatCountSpin")))
        if (r.count > 0) sp->setValue(r.count);
    m_updating = false;

    for (const char *n : { "repeatCombo", "monthModeCombo", "repeatEndCombo" })
        if (auto *c = m_form->findChild<QComboBox *>(QLatin1String(n)))
            connect(c, &QComboBox::currentIndexChanged, this, &AppointmentDialog::updateRecurrenceUi);
    for (const char *n : { "intervalSpin", "repeatCountSpin" })
        if (auto *sp = m_form->findChild<QSpinBox *>(QLatin1String(n)))
            connect(sp, &QSpinBox::valueChanged, this, &AppointmentDialog::updateRecurrenceUi);
    if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("repeatUntilEdit")))
        connect(d, &QDateEdit::dateChanged, this, &AppointmentDialog::updateRecurrenceUi);
    if (auto *st = m_form->findChild<QDateTimeEdit *>(QStringLiteral("startEdit")))
        connect(st, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &dt) { onStartDateChanged(dt.date()); });
    updateRecurrenceUi();
}

void AppointmentDialog::fillMonthModes(const QDate &start)
{
    auto *c = m_form->findChild<QComboBox *>(QStringLiteral("monthModeCombo"));
    if (!c) return;
    const int prev = c->currentData().isValid() ? c->currentData().toInt() : Recurrence::ByMonthDay;
    const QString day = QLocale().dayName(start.dayOfWeek());
    const bool was = m_updating;
    m_updating = true;
    c->clear();
    c->addItem(tr("am %1 des Monats").arg(Recurrence::ordinal(start.day())), Recurrence::ByMonthDay);
    if (Recurrence::nthOfMonth(start) <= 4)
        c->addItem(tr("am %1 %2").arg(Recurrence::ordinal(Recurrence::nthOfMonth(start)), day), Recurrence::ByNthWeekday);
    if (Recurrence::isLastOfMonth(start))
        c->addItem(tr("am letzten %1").arg(day), Recurrence::ByLastWeekday);
    int idx = c->findData(prev);
    if (idx < 0 && prev != Recurrence::ByMonthDay) idx = c->count() - 1;   // n-ter ↔ letzter
    c->setCurrentIndex(std::max(0, idx));
    m_updating = was;
}

void AppointmentDialog::onStartDateChanged(const QDate &newDate)
{
    if (!newDate.isValid() || newDate == m_prevStart) return;
    if (m_appt.isSeries()) {      // angezeigt wird die Regel der Serie – nicht verändern
        m_prevStart = newDate;
        updateRecurrenceUi();
        return;
    }
    // Wöchentlich mit genau einem Wochentag: dieser wandert mit dem Startdatum mit
    int mask = 0;
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i)); cb && cb->isChecked())
            mask |= 1 << (i - 1);
    const int oldBit = 1 << (m_prevStart.dayOfWeek() - 1);
    if (mask == oldBit || mask == 0) {
        m_updating = true;
        for (int i = 1; i <= 7; ++i)
            if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i)))
                cb->setChecked(i == newDate.dayOfWeek());
        m_updating = false;
    }
    m_prevStart = newDate;
    fillMonthModes(newDate);
    updateRecurrenceUi();
}

Recurrence AppointmentDialog::readRecurrence(const QDate &start) const
{
    Recurrence r;
    auto *rep = m_form->findChild<QComboBox *>(QStringLiteral("repeatCombo"));
    if (!rep) return m_origRule;
    switch (rep->currentData().toInt()) {
    case RepDaily:
    case RepEveryNDays: r.freq = Recurrence::Daily; break;
    case RepWorkdays: r.freq = Recurrence::Weekly; r.weekdays = 0x1f; break;
    case RepWeekly:
    case RepEveryNWeeks: r.freq = Recurrence::Weekly; break;
    case RepMonthly:  r.freq = Recurrence::Monthly; break;
    case RepYearly:   r.freq = Recurrence::Yearly; break;
    default:          return r;
    }
    if (rep->currentData().toInt() != RepWorkdays) {
        if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("intervalSpin"))) r.interval = sp->value();
    }
    if (r.freq == Recurrence::Weekly && r.weekdays == 0)
        for (int i = 1; i <= 7; ++i)
            if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i)); cb && cb->isChecked())
                r.weekdays |= 1 << (i - 1);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("monthModeCombo")))
        r.monthMode = c->currentData().toInt();
    const int end = FormIO::read(m_form, QStringLiteral("repeatEndCombo")).value_or(int(EndNever)).toInt();
    if (end == EndUntil) {
        if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("repeatUntilEdit"))) r.until = d->date();
    } else if (end == EndCount) {
        if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("repeatCountSpin"))) r.count = sp->value();
    }
    r.normalize(start);
    return r;
}

void AppointmentDialog::updateRecurrenceUi()
{
    if (m_updating) return;
    auto *rep = m_form->findChild<QComboBox *>(QStringLiteral("repeatCombo"));
    if (!rep) return;
    const int choice = rep->currentData().toInt();
    const bool on = choice != RepNone;
    auto show = [this](const char *name, bool v) {
        if (auto *w = m_form->findChild<QWidget *>(QLatin1String(name))) w->setVisible(v);
    };
    show("intervalSpin", on && choice != RepWorkdays);
    const bool weekly = choice == RepWeekly || choice == RepEveryNWeeks;
    for (int i = 1; i <= 7; ++i) show(qPrintable(QStringLiteral("wd%1Check").arg(i)), weekly);
    show("monthModeCombo", choice == RepMonthly);
    show("repeatOnLabel", weekly || choice == RepMonthly);
    show("repeatEndLabel", on);
    show("repeatEndCombo", on);
    const int end = FormIO::read(m_form, QStringLiteral("repeatEndCombo")).value_or(int(EndNever)).toInt();
    show("repeatUntilEdit", on && end == EndUntil);
    show("repeatCountSpin", on && end == EndCount);

    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("intervalSpin"))) {
        const bool was = sp->blockSignals(true);
        sp->setPrefix(tr("alle "));
        switch (choice) {
        case RepDaily:   sp->setSuffix(tr(" Tage"));   sp->setSpecialValueText(tr("jeden Tag")); break;
        case RepEveryNDays:
            sp->setSuffix(tr(" Tage")); sp->setSpecialValueText(QString());
            if (sp->value() < 2) sp->setValue(2);   // „alle 2 Tage“ als Vorschlag
            break;
        case RepWeekly:  sp->setSuffix(tr(" Wochen")); sp->setSpecialValueText(tr("jede Woche")); break;
        case RepEveryNWeeks:
            sp->setSuffix(tr(" Wochen")); sp->setSpecialValueText(QString());
            if (sp->value() < 2) sp->setValue(2);   // „alle 2 Wochen“ als Vorschlag
            break;
        case RepMonthly: sp->setSuffix(tr(" Monate")); sp->setSpecialValueText(tr("jeden Monat")); break;
        case RepYearly:  sp->setSuffix(tr(" Jahre"));  sp->setSpecialValueText(tr("jedes Jahr")); break;
        default: break;
        }
        sp->blockSignals(was);
    }

    // Vorschau bzw. Hinweis auf die Serie
    QDate start = m_appt.start.date();
    if (const auto v = FormIO::read(m_form, QStringLiteral("startEdit"))) start = v->toDateTime().date();
    QString info;
    if (on) {
        const Recurrence r = readRecurrence(start);
        const QList<QDate> next = r.dates(start, start.addYears(5), 6);
        QStringList ds;
        for (int i = 0; i < std::min<qsizetype>(5, next.size()); ++i)
            ds << QLocale().toString(next[i], QStringLiteral("ddd dd.MM."));
        info = tr("↻ %1<br>Termine: %2").arg(r.describe(start).toHtmlEscaped(),
                                             ds.join(QStringLiteral(", ")) + (next.size() > 5 ? QStringLiteral(" …") : QString()));
    }
    if (m_appt.isSeries()) {
        info += (info.isEmpty() ? QString() : QStringLiteral("<br>"))
              + tr("<i>Dieser Termin gehört zu einer Serie. Beim Speichern wird gefragt, ob die Änderung nur "
                   "für ihn, für alle folgenden oder für die ganze Serie gilt.</i>");
        if (m_appt.detached) info += tr("<br><i>Er wurde bereits einzeln geändert.</i>");
    }
    FormIO::write(m_form, QStringLiteral("seriesInfoLabel"),
                  info.isEmpty() ? QString() : QStringLiteral("<small>") + info + QStringLiteral("</small>"));
}

bool AppointmentDialog::saveSeriesAware(Appointment &a, QString *err, bool *cancelled)
{
    std::function<Recurrence(const QDate &)> rule;
    if (hasRecurrenceUi()) rule = [this](const QDate &d) { return readRecurrence(d); };
    // Ende der Serie vor dem Beginn: ohne Rückfrage ablehnen
    if (rule) {
        const Recurrence r = rule(a.start.date());
        if (r.until.isValid() && r.until < a.start.date()) {
            if (err) *err = tr("Das Ende der Serie liegt vor dem ersten Termin.");
            return false;
        }
    }
    ApptUi::SeriesScope scope = ApptUi::SeriesScope::OnlyThis;
    if (a.isSeries()) {
        scope = ApptUi::askSeriesScope(this, a, false, !ApptUi::ruleChanged(m_appt, rule));
        if (scope == ApptUi::SeriesScope::Cancel) {
            if (cancelled) *cancelled = true;
            return false;
        }
    }
    return ApptUi::saveWithScope(a, m_appt, rule, scope, err);
}
