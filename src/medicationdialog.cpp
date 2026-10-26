// SPDX-License-Identifier: MIT
#include "medicationdialog.h"
#include "formloader.h"
#include "leaflet.h"
#include "medschedule.h"
#include "drugclass.h"
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>

#include <QCheckBox>
#include <QDateEdit>
#include <QTimeEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>

MedicationDialog::MedicationDialog(const Medication &m, QWidget *parent, qint64 patientId)
    : QDialog(parent), m_med(m), m_patientId(patientId ? patientId : m.patientId)
{
    setWindowTitle(m.id || !m.name.isEmpty() ? tr("Medikament bearbeiten") : tr("Medikament hinzufügen"));
    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("medicationform"), this);
    // Formular in einem Rollbereich: passt auch auf kleine Bildschirme bzw. bei großer Skalierung
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setWidget(m_form);
    lay->addWidget(m_scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &MedicationDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &MedicationDialog::reject);
    lay->addWidget(buttons);

    FormIO::write(m_form, QStringLiteral("nameEdit"), m.name);
    FormIO::write(m_form, QStringLiteral("substanceEdit"), m.substance);
    FormIO::write(m_form, QStringLiteral("strengthEdit"), m.strength);
    FormIO::write(m_form, QStringLiteral("pznEdit"), m.pzn);
    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("leafletButton"))) {
        connect(b, &QPushButton::clicked, this, &MedicationDialog::showLeaflet);
        if (m.extra.contains(QLatin1String(Leaflet::kLeafletDocKey)))
            b->setText(tr("&Beipackzettel ✔…"));
    }
    FormIO::write(m_form, QStringLiteral("formCombo"), m.form);
    FormIO::write(m_form, QStringLiteral("morningEdit"), m.morning);
    FormIO::write(m_form, QStringLiteral("noonEdit"), m.noon);
    FormIO::write(m_form, QStringLiteral("eveningEdit"), m.evening);
    FormIO::write(m_form, QStringLiteral("nightEdit"), m.night);
    FormIO::write(m_form, QStringLiteral("unitCombo"), m.unit.isEmpty() && m.id == 0 ? QStringLiteral("Stück") : m.unit);
    FormIO::write(m_form, QStringLiteral("reasonEdit"), m.reason);
    FormIO::write(m_form, QStringLiteral("notesEdit"), m.notes);
    FormIO::write(m_form, QStringLiteral("startDateEdit"),
                  m.startDate.isValid() ? m.startDate : (m.id == 0 ? QDate::currentDate() : QDate()));
    FormIO::write(m_form, QStringLiteral("endDateCheck"), m.endDate.isValid());
    FormIO::write(m_form, QStringLiteral("endDateEdit"),
                  m.endDate.isValid() ? m.endDate : QDate::currentDate());
    if (auto *ed = m_form->findChild<QDateEdit *>(QStringLiteral("endDateEdit")))
        ed->setEnabled(m.endDate.isValid() || !FormIO::has(m_form, QStringLiteral("endDateCheck")));
    FormIO::write(m_form, QStringLiteral("archivedCheck"), m.archived);
    FormIO::write(m_form, QStringLiteral("stopReasonEdit"), MedSchedule::stopReason(m));
    if (auto *e = m_form->findChild<QWidget *>(QStringLiteral("stopReasonEdit"))) e->setEnabled(m.archived);
    FormIO::applyExtra(m_form, m.extra);
    setupRhythm();
    setupReminder();
    setupClass();
    setupPeriod();
    fitToScreen();
}

// Dialog so groß wie das Formular, höchstens so hoch wie der freie Bildschirmbereich
void MedicationDialog::fitToScreen()
{
    m_form->adjustSize();
    const QSize f = m_form->sizeHint();
    const int sb = m_scroll->verticalScrollBar()->sizeHint().width();
    const QMargins mg = layout()->contentsMargins();
    const auto *bb = findChild<QDialogButtonBox *>();
    const int chrome = mg.top() + mg.bottom() + layout()->spacing() + (bb ? bb->sizeHint().height() : 40);
    QScreen *scr = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
    const int maxH = avail.height() - 60;                                       // Fensterrahmen und Titel
    const int h = std::min(f.height() + chrome, maxH);
    const int w = std::min(f.width() + mg.left() + mg.right() + (f.height() + chrome > maxH ? sb : 0) + 4, avail.width() - 40);
    m_scroll->setMinimumWidth(std::min(f.width() + (f.height() + chrome > maxH ? sb : 0) + 2, avail.width() - 80));
    resize(w, h);
}

// Zeitraum von – bis: ohne Haken „bis“ gilt das Medikament dauerhaft (Standard)
void MedicationDialog::setupPeriod()
{
    auto *start = m_form->findChild<QDateEdit *>(QStringLiteral("startDateEdit"));
    auto *end = m_form->findChild<QDateEdit *>(QStringLiteral("endDateEdit"));
    auto *check = m_form->findChild<QCheckBox *>(QStringLiteral("endDateCheck"));
    if (!start || !end || !check) return;
    if (!m_med.endDate.isValid()) end->setDate(std::max(start->date(), QDate::currentDate()));
    connect(check, &QCheckBox::toggled, this, [start, end](bool on) {
        if (on && end->date() < start->date()) end->setDate(start->date());
    });
    auto update = [this, start, end, check] {
        QString t;
        if (!check->isChecked()) t = tr("dauerhaft (kein Enddatum)");
        else if (end->date() < start->date()) t = tr("Ende liegt vor dem Beginn");
        else {
            const qint64 n = start->date().daysTo(end->date()) + 1;
            t = n == 1 ? tr("1 Tag") : tr("%1 Tage").arg(n);
            if (MedSchedule::stopPlanned(m_med))
                t += QStringLiteral(" – ") + tr("wird am %1 abgesetzt")
                         .arg(QLocale().toString(end->date().addDays(1), QStringLiteral("dd.MM.yyyy")));
        }
        FormIO::write(m_form, QStringLiteral("periodInfoLabel"), QStringLiteral("<small>%1</small>").arg(t.toHtmlEscaped()));
    };
    connect(check, &QCheckBox::toggled, this, update);
    // „Abgesetzt“ ohne Enddatum: heute als Ende eintragen
    if (auto *arch = m_form->findChild<QCheckBox *>(QStringLiteral("archivedCheck")))
        connect(arch, &QCheckBox::toggled, this, [check, end, start](bool on) {
            if (on && !check->isChecked()) {
                end->setDate(std::max(start->date(), QDate::currentDate()));
                check->setChecked(true);
            }
        });
    connect(start, &QDateEdit::dateChanged, this, update);
    connect(end, &QDateEdit::dateChanged, this, update);
    update();
}

void MedicationDialog::accept()
{
    Medication m = m_med;
    m.name      = FormIO::readString(m_form, QStringLiteral("nameEdit"), m.name).trimmed();
    if (m.name.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte den Namen des Medikaments angeben."));
        return;
    }
    m.substance = FormIO::readString(m_form, QStringLiteral("substanceEdit"), m.substance).trimmed();
    m.strength  = FormIO::readString(m_form, QStringLiteral("strengthEdit"), m.strength).trimmed();
    {
        const QString raw = FormIO::readString(m_form, QStringLiteral("pznEdit"), m.pzn).trimmed();
        const QString pzn = Leaflet::normalizePzn(raw);
        if (!raw.isEmpty() && (pzn.isEmpty() || !Leaflet::isValidPzn(pzn))
            && QMessageBox::question(this, windowTitle(),
                   tr("Die PZN „%1“ ist ungültig (8 Ziffern mit Prüfziffer). Trotzdem übernehmen?").arg(raw))
                   != QMessageBox::Yes)
            return;
        m.pzn = Leaflet::isValidPzn(pzn) ? pzn : raw;
    }
    m.form      = FormIO::readString(m_form, QStringLiteral("formCombo"), m.form).trimmed();
    m.morning   = FormIO::readString(m_form, QStringLiteral("morningEdit"), m.morning).trimmed();
    m.noon      = FormIO::readString(m_form, QStringLiteral("noonEdit"), m.noon).trimmed();
    m.evening   = FormIO::readString(m_form, QStringLiteral("eveningEdit"), m.evening).trimmed();
    m.night     = FormIO::readString(m_form, QStringLiteral("nightEdit"), m.night).trimmed();
    m.unit      = FormIO::readString(m_form, QStringLiteral("unitCombo"), m.unit).trimmed();
    m.reason    = FormIO::readString(m_form, QStringLiteral("reasonEdit"), m.reason).trimmed();
    m.notes     = FormIO::readString(m_form, QStringLiteral("notesEdit"), m.notes).trimmed();
    if (const auto v = FormIO::read(m_form, QStringLiteral("startDateEdit")))
        m.startDate = v->toDate();

    bool hasEnd = true;
    if (const auto c = FormIO::read(m_form, QStringLiteral("endDateCheck")))
        hasEnd = c->toBool();
    if (const auto v = FormIO::read(m_form, QStringLiteral("endDateEdit")))
        m.endDate = hasEnd ? v->toDate() : QDate();
    // Vorgemerktes Absetzen folgt dem Feld „bis“: ohne Enddatum entfällt es, sonst Absetzdatum = Tag danach
    if (MedSchedule::stopPlanned(m)) {
        if (!m.endDate.isValid()) MedSchedule::cancelPlannedStop(m);
        else m.extra[QLatin1String(MedSchedule::kStopOnKey)] = m.endDate.addDays(1).toString(Qt::ISODate);
    }
    if (m.endDate.isValid() && m.startDate.isValid() && m.endDate < m.startDate) {
        QMessageBox::information(this, windowTitle(), tr("Das Enddatum liegt vor dem Beginn."));
        return;
    }

    if (const auto v = FormIO::read(m_form, QStringLiteral("archivedCheck"))) {
        const QString reason = FormIO::readString(m_form, QStringLiteral("stopReasonEdit"), MedSchedule::stopReason(m));
        if (v->toBool()) {
            // Absetzen: Enddatum = eingetragenes „bis“ (höchstens heute), sonst heute
            const QDate day = m.endDate.isValid() && m.endDate <= QDate::currentDate() ? m.endDate : QDate::currentDate();
            MedSchedule::discontinue(m, day, reason);
        } else if (m.archived) {
            const QDate keepEnd = hasEnd ? m.endDate : QDate();
            MedSchedule::reactivate(m);
            if (keepEnd.isValid() && keepEnd > QDate::currentDate()) m.endDate = keepEnd;
        }
    }
    m.extra = FormIO::collectExtra(m_form, m.extra);
    readRhythm(m);
    readReminder(m);
    // Kennzeichnung: nur Abweichungen von der automatischen Zuordnung speichern
    static const char *const boxes[] = { "btmCheck", "painCheck", "anticoagCheck", "bpCheck", "cortisoneCheck" };
    for (int g = 0; g < DrugClass::GroupCount; ++g)
        if (auto *cb = m_form->findChild<QCheckBox *>(QLatin1String(boxes[g]))) {
            const bool autoMatch = DrugClass::classifyAuto(m, DrugClass::Group(g)).match;
            DrugClass::setOverride(m, DrugClass::Group(g),
                                   cb->isChecked() == autoMatch ? std::nullopt : std::optional<bool>(cb->isChecked()));
        }
    if (MedSchedule::choiceOf(m) == MedSchedule::Weekdays && m.rhythm.weekdays == 0) {
        QMessageBox::information(this, windowTitle(), tr("Bitte mindestens einen Wochentag für die Einnahme wählen."));
        return;
    }
    if (const MedSchedule::Reminder r = MedSchedule::reminder(m); r.on && MedSchedule::canRemind(m)) {
        if (r.times.isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("Bitte mindestens eine Uhrzeit für die Erinnerung wählen."));
            return;
        }
        if (MedSchedule::remindByWeekdays(m) && r.days == 0) {
            QMessageBox::information(this, windowTitle(), tr("Bitte mindestens einen Tag für die Erinnerung wählen."));
            return;
        }
    }
    m_med = m;
    QDialog::accept();
}

void MedicationDialog::showLeaflet()
{
    // aktuelle Eingaben (Name, Stärke, Wirkstoff, PZN) verwenden
    Medication m = m_med;
    m.name = FormIO::readString(m_form, QStringLiteral("nameEdit"), m.name).trimmed();
    m.substance = FormIO::readString(m_form, QStringLiteral("substanceEdit"), m.substance).trimmed();
    m.strength = FormIO::readString(m_form, QStringLiteral("strengthEdit"), m.strength).trimmed();
    m.form = FormIO::readString(m_form, QStringLiteral("formCombo"), m.form).trimmed();
    m.pzn = FormIO::readString(m_form, QStringLiteral("pznEdit"), m.pzn).trimmed();
    if (m.name.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte zuerst den Namen des Medikaments angeben."));
        return;
    }
    LeafletDialog d(m, m_patientId, this);
    d.exec();
    if (!d.medicationChanged()) return;
    const Medication r = d.medication();
    m_med.pzn = r.pzn;
    for (const char *k : { Leaflet::kLeafletDocKey, Leaflet::kLeafletUrlKey }) {
        if (r.extra.contains(QLatin1String(k))) m_med.extra[QLatin1String(k)] = r.extra.value(QLatin1String(k));
        else m_med.extra.remove(QLatin1String(k));
    }
    FormIO::write(m_form, QStringLiteral("pznEdit"), r.pzn);
    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("leafletButton")))
        if (m_med.extra.contains(QLatin1String(Leaflet::kLeafletDocKey))) b->setText(tr("&Beipackzettel ✔…"));
}

// ============================================================ Einnahme-Rhythmus

void MedicationDialog::setupRhythm()
{
    auto *c = m_form->findChild<QComboBox *>(QStringLiteral("rhythmCombo"));
    if (!c) return;   // angepasstes Formular ohne Rhythmus
    c->addItem(tr("täglich"), MedSchedule::Daily);
    c->addItem(tr("alle … Tage"), MedSchedule::EveryNDays);
    c->addItem(tr("an bestimmten Wochentagen"), MedSchedule::Weekdays);
    c->addItem(tr("monatlich"), MedSchedule::Monthly);
    if (auto *cat = m_form->findChild<QComboBox *>(QStringLiteral("categoryCombo"))) {
        for (int k = 0; k < Medication::CategoryCount; ++k)
            cat->addItem(MedSchedule::categoryName(k), k);
        cat->setCurrentIndex(std::max(0, cat->findData(m_med.category)));
        connect(cat, &QComboBox::currentIndexChanged, this, &MedicationDialog::updateRhythmUi);
    }
    const QLocale loc;
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i))) {
            cb->setText(loc.dayName(i, QLocale::ShortFormat));
            cb->setChecked(m_med.rhythm.freq == Recurrence::Weekly && (m_med.rhythm.weekdays & (1 << (i - 1))));
            connect(cb, &QCheckBox::toggled, this, &MedicationDialog::updateRhythmUi);
        }
    {
        const int ch = m_med.category == Medication::Regular ? MedSchedule::choiceOf(m_med) : MedSchedule::Daily;
        c->setCurrentIndex(std::max(0, c->findData(ch)));
    }
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("rhythmIntervalSpin"))) {
        sp->setValue(std::max(1, m_med.rhythm.interval));
        connect(sp, &QSpinBox::valueChanged, this, &MedicationDialog::updateRhythmUi);
    }
    FormIO::write(m_form, QStringLiteral("rhythmStartEdit"), MedSchedule::anchor(m_med));
    FormIO::write(m_form, QStringLiteral("rhythmNoteEdit"), m_med.rhythmNote);
    connect(c, &QComboBox::currentIndexChanged, this, &MedicationDialog::updateRhythmUi);
    if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("rhythmStartEdit")))
        connect(d, &QDateEdit::dateChanged, this, &MedicationDialog::updateRhythmUi);
    updateRhythmUi();
}

void MedicationDialog::readRhythm(Medication &m) const
{
    auto *c = m_form->findChild<QComboBox *>(QStringLiteral("rhythmCombo"));
    if (!c) return;
    if (auto *cat = m_form->findChild<QComboBox *>(QStringLiteral("categoryCombo")))
        m.category = cat->currentData().toInt();
    const int choice = m.category == Medication::Regular ? c->currentData().toInt() : int(MedSchedule::Daily);
    m.rhythm = {};
    const int n = FormIO::read(m_form, QStringLiteral("rhythmIntervalSpin")).value_or(1).toInt();
    switch (choice) {
    case MedSchedule::EveryNDays: m.rhythm.freq = Recurrence::Daily; m.rhythm.interval = std::max(2, n); break;
    case MedSchedule::Weekdays:
        m.rhythm.freq = Recurrence::Weekly;
        m.rhythm.interval = std::max(1, n);
        for (int i = 1; i <= 7; ++i)
            if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("wd%1Check").arg(i)); cb && cb->isChecked())
                m.rhythm.weekdays |= 1 << (i - 1);
        break;
    case MedSchedule::Monthly: m.rhythm.freq = Recurrence::Monthly; m.rhythm.interval = std::max(1, n); break;
    default: break;
    }
    m.rhythmStart = choice == MedSchedule::Daily
        ? QDate() : FormIO::read(m_form, QStringLiteral("rhythmStartEdit")).value_or(QDate::currentDate()).toDate();
    m.rhythmNote = FormIO::readString(m_form, QStringLiteral("rhythmNoteEdit"), m.rhythmNote).trimmed();
}

void MedicationDialog::updateRhythmUi()
{
    auto *c = m_form->findChild<QComboBox *>(QStringLiteral("rhythmCombo"));
    if (!c) return;
    auto show = [this](const QString &n, bool on) {
        if (auto *w = m_form->findChild<QWidget *>(n)) w->setVisible(on);
    };
    int category = Medication::Regular;
    if (auto *cat = m_form->findChild<QComboBox *>(QStringLiteral("categoryCombo")))
        category = cat->currentData().toInt();
    FormIO::write(m_form, QStringLiteral("categoryHintLabel"),
                  QStringLiteral("<small>%1</small>").arg(MedSchedule::categoryHint(category).toHtmlEscaped()));
    const bool regular = category == Medication::Regular;
    show(QStringLiteral("rhythmLabel"), regular);
    show(QStringLiteral("rhythmCombo"), regular);
    if (auto *l = m_form->findChild<QLabel *>(QStringLiteral("rhythmNoteLabel")))
        l->setText(regular || category == Medication::Other ? tr("B&emerkung") : tr("Anlass / B&emerkung"));
    const int choice = regular ? c->currentData().toInt() : int(MedSchedule::AsNeeded);
    const bool interval = choice == MedSchedule::EveryNDays || choice == MedSchedule::Weekdays || choice == MedSchedule::Monthly;
    show(QStringLiteral("rhythmIntervalSpin"), interval);
    show(QStringLiteral("rhythmStartLabel"), interval);
    show(QStringLiteral("rhythmStartEdit"), interval);
    show(QStringLiteral("rhythmDaysLabel"), choice == MedSchedule::Weekdays);
    for (int i = 1; i <= 7; ++i) show(QStringLiteral("wd%1Check").arg(i), choice == MedSchedule::Weekdays);
    updateReminderUi(category, choice);
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("rhythmIntervalSpin"))) {
        const bool was = sp->blockSignals(true);
        sp->setPrefix(tr("alle "));
        switch (choice) {
        case MedSchedule::EveryNDays:
            sp->setMinimum(2); sp->setSuffix(tr(" Tage")); sp->setSpecialValueText(QString()); break;
        case MedSchedule::Weekdays:
            sp->setMinimum(1); sp->setSuffix(tr(" Wochen")); sp->setSpecialValueText(tr("jede Woche")); break;
        case MedSchedule::Monthly:
            sp->setMinimum(1); sp->setSuffix(tr(" Monate")); sp->setSpecialValueText(tr("jeden Monat")); break;
        default: break;
        }
        sp->blockSignals(was);
    }
    // Vorschau der nächsten Einnahmetage
    Medication m = m_med;
    readRhythm(m);
    QString info;
    if (interval) {
        QStringList days;
        QDate d = QDate::currentDate();
        for (int i = 0; i < 5; ++i) {
            const QDate n = MedSchedule::nextDue(m, d, 800);
            if (!n.isValid()) break;
            days << QLocale().toString(n, QStringLiteral("ddd dd.MM."));
            d = n.addDays(1);
        }
        info = tr("<small>%1 · nächste Einnahmetage: %2<br>Der Assistent „Medikamente stellen“ nennt das "
                  "Medikament nur an diesen Tagen.</small>")
                   .arg(MedSchedule::describe(m).toHtmlEscaped(),
                        days.isEmpty() ? tr("keine") : days.join(QStringLiteral(", ")) + QStringLiteral(" …"));
    } else if (category == Medication::AsNeeded) {
        info = tr("<small>Bedarfsmedikation: wird vom Assistenten nicht zum Stellen vorgelesen, aber als "
                  "Bedarfsmedikament angezeigt. Dosierung oben als Einzel- bzw. Höchstmenge, Anlass in die Bemerkung.</small>");
    } else if (category == Medication::Emergency) {
        info = tr("<small>Notfallmedikation: erscheint hervorgehoben im Kopf des Hauptfensters, im Medikamentenplan "
                  "und im Assistenten. Anlass und Anwendung in die Bemerkung, z. B. „bei Brustschmerz 1–2 Hübe "
                  "unter die Zunge, nach 10 Minuten ohne Besserung Notruf 112“.</small>");
    } else if (category == Medication::Other) {
        info = tr("<small>Sonstiges: z. B. Salben, Augentropfen, Nahrungsergänzung, Impfungen oder Hilfsmittel. "
                  "Erscheint im Medikamentenplan in einem eigenen Abschnitt, wird vom Assistenten nicht zum Stellen "
                  "vorgelesen, aber angezeigt. Anwendung in die Bemerkung.</small>");
    }
    readReminder(m);
    if (const QString rt = MedSchedule::reminderText(m); !rt.isEmpty())
        info += QStringLiteral("<small>%1</small>").arg((info.isEmpty() ? QString() : QStringLiteral("<br>"))
                                                       + tr("Erinnerung: %1").arg(rt).toHtmlEscaped());
    FormIO::write(m_form, QStringLiteral("rhythmInfoLabel"), info);
}

// ============================================================ Erinnerung an die Einnahme

void MedicationDialog::setupReminder()
{
    auto *rc = m_form->findChild<QCheckBox *>(QStringLiteral("remindCheck"));
    if (!rc) return;   // angepasstes Formular ohne Erinnerung
    const MedSchedule::Reminder r = MedSchedule::reminder(m_med);
    rc->setChecked(r.on);
    connect(rc, &QCheckBox::toggled, this, &MedicationDialog::updateRhythmUi);
    const QLocale loc;
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rd%1Check").arg(i))) {
            cb->setText(loc.dayName(i, QLocale::ShortFormat));
            cb->setChecked(r.days & (1 << (i - 1)));
            connect(cb, &QCheckBox::toggled, this, &MedicationDialog::updateRhythmUi);
        }
    const QList<MedSchedule::SlotTime> slotList = MedSchedule::reminderSlots(m_med);
    // Ohne gespeicherte Auswahl folgen die Tageszeiten dem Dosierschema
    m_slotsTouched = m_med.extra.contains(QLatin1String(MedSchedule::kRemindTimesKey));
    for (int s = 0; s < MedSchedule::SlotCount; ++s) {
        if (auto *te = m_form->findChild<QTimeEdit *>(QStringLiteral("rt%1Edit").arg(s))) {
            te->setTime(slotList[s].time);
            connect(te, &QTimeEdit::timeChanged, this, &MedicationDialog::updateRhythmUi);
        }
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rt%1Check").arg(s))) {
            cb->setChecked(slotList[s].on);
            connect(cb, &QCheckBox::clicked, this, [this] { m_slotsTouched = true; });
            connect(cb, &QCheckBox::toggled, this, &MedicationDialog::updateRhythmUi);
        }
    }
    for (const char *n : { "morningEdit", "noonEdit", "eveningEdit", "nightEdit" })
        if (auto *e = m_form->findChild<QLineEdit *>(QLatin1String(n)))
            connect(e, &QLineEdit::textChanged, this, &MedicationDialog::followDoses);
    if (!m_slotsTouched) followDoses();
    updateRhythmUi();
}

// Tageszeiten der Erinnerung nach dem Dosierschema vorbelegen, solange der Anwender sie nicht selbst wählt
void MedicationDialog::followDoses()
{
    if (m_slotsTouched) return;
    Medication m;
    m.morning = FormIO::readString(m_form, QStringLiteral("morningEdit"), QString());
    m.noon = FormIO::readString(m_form, QStringLiteral("noonEdit"), QString());
    m.evening = FormIO::readString(m_form, QStringLiteral("eveningEdit"), QString());
    m.night = FormIO::readString(m_form, QStringLiteral("nightEdit"), QString());
    bool any = false;
    for (int s = 0; s < MedSchedule::SlotCount; ++s) any |= !MedSchedule::slotDose(m, s).isEmpty();
    for (int s = 0; s < MedSchedule::SlotCount; ++s)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rt%1Check").arg(s))) {
            const bool was = cb->blockSignals(true);
            cb->setChecked(any ? !MedSchedule::slotDose(m, s).isEmpty() : s == MedSchedule::Morning);
            cb->blockSignals(was);
        }
    updateRhythmUi();
}

void MedicationDialog::readReminder(Medication &m) const
{
    auto *rc = m_form->findChild<QCheckBox *>(QStringLiteral("remindCheck"));
    if (!rc) return;
    MedSchedule::Reminder r;
    r.on = rc->isChecked();
    r.days = 0;
    for (int i = 1; i <= 7; ++i)
        if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rd%1Check").arg(i)); cb && cb->isChecked())
            r.days |= 1 << (i - 1);
    for (int s = 0; s < MedSchedule::SlotCount; ++s) {
        auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rt%1Check").arg(s));
        auto *te = m_form->findChild<QTimeEdit *>(QStringLiteral("rt%1Edit").arg(s));
        if (cb && te && cb->isChecked() && !r.times.contains(te->time())) r.times << te->time();
    }
    MedSchedule::setReminder(m, r);
}

void MedicationDialog::updateReminderUi(int category, int choice)
{
    auto *rc = m_form->findChild<QCheckBox *>(QStringLiteral("remindCheck"));
    if (!rc) return;
    auto show = [this](const QString &n, bool on) {
        if (auto *w = m_form->findChild<QWidget *>(n)) w->setVisible(on);
    };
    auto enable = [this](const QString &n, bool on) {
        if (auto *w = m_form->findChild<QWidget *>(n)) w->setEnabled(on);
    };
    const bool can = category == Medication::Regular || category == Medication::Other;
    const bool byWeekdays = category == Medication::Other || choice == MedSchedule::Daily;
    const bool on = can && rc->isChecked();
    show(QStringLiteral("remindCheck"), can);
    for (const char *n : { "remindDaysLabel", "remindTimesLabel", "remindDaysInfoLabel" })
        show(QLatin1String(n), can);
    for (int i = 1; i <= 7; ++i) {
        show(QStringLiteral("rd%1Check").arg(i), can && byWeekdays);
        enable(QStringLiteral("rd%1Check").arg(i), on);
    }
    show(QStringLiteral("remindDaysInfoLabel"), can && !byWeekdays);
    FormIO::write(m_form, QStringLiteral("remindDaysInfoLabel"), tr("den Einnahmetagen laut Rhythmus"));
    for (int s = 0; s < MedSchedule::SlotCount; ++s) {
        show(QStringLiteral("rt%1Check").arg(s), can);
        show(QStringLiteral("rt%1Edit").arg(s), can);
        enable(QStringLiteral("rt%1Check").arg(s), on);
        auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("rt%1Check").arg(s));
        enable(QStringLiteral("rt%1Edit").arg(s), on && cb && cb->isChecked());
    }
    enable(QStringLiteral("remindDaysLabel"), on);
    enable(QStringLiteral("remindTimesLabel"), on);
    enable(QStringLiteral("remindDaysInfoLabel"), on);
}

// ============================================================ Kennzeichnung (BtM, Schmerzmittel, Blutverdünner)

Medication MedicationDialog::currentBasics() const
{
    Medication m = m_med;
    m.name = FormIO::readString(m_form, QStringLiteral("nameEdit"), m.name).trimmed();
    m.substance = FormIO::readString(m_form, QStringLiteral("substanceEdit"), m.substance).trimmed();
    m.strength = FormIO::readString(m_form, QStringLiteral("strengthEdit"), m.strength).trimmed();
    m.form = FormIO::readString(m_form, QStringLiteral("formCombo"), m.form).trimmed();
    m.reason = FormIO::readString(m_form, QStringLiteral("reasonEdit"), m.reason).trimmed();
    return m;
}

void MedicationDialog::setupClass()
{
    static const char *const boxes[] = { "btmCheck", "painCheck", "anticoagCheck", "bpCheck", "cortisoneCheck" };
    bool any = false;
    for (int g = 0; g < DrugClass::GroupCount; ++g) {
        auto *cb = m_form->findChild<QCheckBox *>(QLatin1String(boxes[g]));
        if (!cb) continue;
        any = true;
        m_classTouched[g] = DrugClass::override(m_med, DrugClass::Group(g)).has_value();
        cb->setChecked(DrugClass::classify(m_med, DrugClass::Group(g)).match);
        connect(cb, &QCheckBox::clicked, this, [this, g] { m_classTouched[g] = true; refreshClass(); });
    }
    if (!any) return;
    for (const char *n : { "nameEdit", "substanceEdit", "strengthEdit", "reasonEdit" })
        if (auto *e = m_form->findChild<QLineEdit *>(QLatin1String(n)))
            connect(e, &QLineEdit::editingFinished, this, &MedicationDialog::refreshClass);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("formCombo")))
        connect(c, &QComboBox::currentTextChanged, this, &MedicationDialog::refreshClass);
    refreshClass();
}

void MedicationDialog::refreshClass()
{
    static const char *const boxes[] = { "btmCheck", "painCheck", "anticoagCheck", "bpCheck", "cortisoneCheck" };
    const Medication m = currentBasics();
    QStringList info;
    for (int g = 0; g < DrugClass::GroupCount; ++g) {
        auto *cb = m_form->findChild<QCheckBox *>(QLatin1String(boxes[g]));
        if (!cb) continue;
        const DrugClass::Hit h = DrugClass::classifyAuto(m, DrugClass::Group(g));
        if (!m_classTouched[g]) {
            const bool was = cb->blockSignals(true);
            cb->setChecked(h.match);
            cb->blockSignals(was);
        }
        if (h.match && !h.reason.isEmpty())
            info << DrugClass::groupName(DrugClass::Group(g)) + QStringLiteral(": ")
                        + (h.uncertain ? tr("ggf. ") : QString()) + h.reason;
        else if (!h.match && !h.reason.isEmpty())
            info << h.reason;   // z. B. „ausgenommene Zubereitung, kein BtM“
        if (m_classTouched[g] && cb->isChecked() != h.match)
            info << tr("%1: vom Anwender %2").arg(DrugClass::groupName(DrugClass::Group(g)),
                                                  cb->isChecked() ? tr("zugeordnet") : tr("ausgeschlossen"));
    }
    FormIO::write(m_form, QStringLiteral("classInfoLabel"),
                  info.isEmpty() ? tr("<small>Keine automatische Zuordnung erkannt.</small>")
                                 : QStringLiteral("<small>%1</small>").arg(info.join(QStringLiteral(" · ")).toHtmlEscaped()));
}
