// SPDX-License-Identifier: MIT
#include "medassistant.h"
#include "medstock.h"
#include "medprep.h"
#include <QSignalBlocker>
#include "appsettings.h"
#include "database.h"
#include "medlogdialog.h"
#include "leaflet.h"
#include "medschedule.h"
#include <QDateEdit>
#include "session.h"
#include "sounds.h"
#include "speech.h"
#include "reportprinter.h"

#include <algorithm>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QTimer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString slotName(int s) { return MedPrep::slotName(s); }
QString doseOf(const Medication &m, int s) { return MedPrep::doseOf(m, s); }
} // namespace

MedicationAssistant::MedicationAssistant(const Patient &p, const QList<Medication> &meds, QWidget *parent)
    : QDialog(parent), m_patient(p), m_source(meds)
{
    setWindowTitle(tr("Medikamente stellen – %1").arg(p.displayName()));
    m_speech = new Speech(this);
    m_speech->setRate(AppSettings::speechRate());

    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Stellen:"), this));
    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("prepModeCombo"));
    m_mode->addItem(tr("täglich"), 1);
    m_mode->addItem(tr("wöchentlich (7 Tage)"), 7);
    m_mode->setToolTip(tr("Tagesdosierer: ein Tag – Wochendosierer: sieben Tage ab dem gewählten Tag"));
    top->addWidget(m_mode);
    top->addSpacing(8);
    top->addWidget(new QLabel(tr("Zeitpunkt:"), this));
    m_slot = new QComboBox(this);
    m_slot->setObjectName(QStringLiteral("slotCombo"));
    m_slot->addItem(tr("alle Zeitpunkte"), -1);
    for (int s = 0; s < 4; ++s) m_slot->addItem(slotName(s), s);
    top->addWidget(m_slot);
    m_forLabel = new QLabel(tr("für"), this);
    top->addWidget(m_forLabel);
    m_date = new QDateEdit(QDate::currentDate(), this);
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat(QStringLiteral("ddd dd.MM.yyyy"));
    m_date->setToolTip(tr("Tag, für den gestellt wird – z. B. Tag für Tag beim Befüllen eines Wochendosierers"));
    top->addWidget(m_date);
    m_rangeLabel = new QLabel(this);
    top->addWidget(m_rangeLabel);
    top->addSpacing(16);
    top->addWidget(new QLabel(tr("Stimme:"), this));
    m_voice = new QComboBox(this);
    for (const auto &[k, label] : Speech::voiceChoices()) m_voice->addItem(label, k);
    m_voice->setCurrentIndex(std::max(0, m_voice->findData(AppSettings::voiceGender())));
    m_voice->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    top->addWidget(m_voice);
    m_autoRead = new QCheckBox(tr("automatisch vorlesen"), this);
    m_autoRead->setChecked(true);
    top->addWidget(m_autoRead);
    top->addStretch();
    lay->addLayout(top);
    m_voiceInfo = new QLabel(this);
    m_voiceInfo->setWordWrap(true);
    lay->addWidget(m_voiceInfo);
    m_lastLabel = new QLabel(this);
    m_lastLabel->setWordWrap(true);
    lay->addWidget(m_lastLabel);
    m_dayInfo = new QLabel(this);
    m_dayInfo->setWordWrap(true);
    lay->addWidget(m_dayInfo);

    m_stack = new QStackedWidget(this);
    // ---- Seite 0: Medikament
    auto *card = new QWidget(m_stack);
    auto *cl = new QVBoxLayout(card);
    m_progress = new QLabel(card);
    cl->addWidget(m_progress);
    m_name = new QLabel(card);
    m_name->setWordWrap(true);
    m_name->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: bold;"));
    cl->addWidget(m_name);
    m_details = new QLabel(card);
    m_details->setWordWrap(true);
    m_details->setStyleSheet(QStringLiteral("font-size: 13pt;"));
    cl->addWidget(m_details);
    m_doses = new QTableWidget(2, 4, card);
    m_doses->horizontalHeader()->hide();
    m_doses->verticalHeader()->hide();
    m_doses->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_doses->setSelectionMode(QAbstractItemView::NoSelection);
    m_doses->setFocusPolicy(Qt::NoFocus);
    m_doses->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_doses->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_doses->setMinimumHeight(110);
    m_doses->setMaximumHeight(130);
    cl->addWidget(m_doses);
    m_hint = new QLabel(card);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("font-size: 12pt;"));
    cl->addWidget(m_hint);
    cl->addStretch();
    m_stack->addWidget(card);
    // ---- Seite 1: fertig
    auto *done = new QWidget(m_stack);
    auto *dl = new QVBoxLayout(done);
    m_doneLabel = new QLabel(done);
    m_doneLabel->setAlignment(Qt::AlignCenter);
    m_doneLabel->setWordWrap(true);
    dl->addStretch();
    dl->addWidget(m_doneLabel);
    m_loggedLabel = new QLabel(done);
    m_loggedLabel->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_loggedLabel);
    auto *noteRow = new QHBoxLayout;
    noteRow->addStretch();
    noteRow->addWidget(new QLabel(tr("Bemerkung:"), done));
    m_note = new QLineEdit(done);
    m_note->setObjectName(QStringLiteral("prepNoteEdit"));
    m_note->setPlaceholderText(tr("optional, z. B. „Wochendosierer KW 43“"));
    m_note->setMinimumWidth(320);
    m_note->setClearButtonEnabled(true);
    noteRow->addWidget(m_note);
    noteRow->addStretch();
    dl->addLayout(noteRow);
    dl->addStretch();
    connect(m_note, &QLineEdit::editingFinished, this, &MedicationAssistant::saveNote);
    m_stack->addWidget(done);
    lay->addWidget(m_stack, 1);

    auto *row = new QHBoxLayout;
    m_prev = new QPushButton(tr("◀ &Zurück"), this);
    m_repeat = new QPushButton(tr("↻ &Wiederholen"), this);
    m_next = new QPushButton(tr("&Weiter ▶"), this);
    auto *close = new QPushButton(tr("&Beenden"), this);
    auto *logBtn = new QPushButton(tr("&Protokoll…"), this);
    logBtn->setToolTip(tr("Wann wurde von wem welches Medikament gestellt?"));
    logBtn->setAutoDefault(false);
    close->setAutoDefault(false);
    for (QPushButton *b : { m_prev, m_repeat, m_next }) {
        b->setMinimumHeight(48);
        b->setStyleSheet(QStringLiteral("font-size: 14pt; padding: 4px 18px;"));
        b->setAutoDefault(false);
    }
    m_next->setDefault(true);
    m_prev->setToolTip(tr("Vorheriges Medikament (Pfeil links)"));
    m_repeat->setToolTip(tr("Dasselbe Medikament nochmals vorlesen (Taste R)"));
    m_next->setToolTip(tr("Nächstes Medikament (Leertaste, Eingabe oder Pfeil rechts)"));
    row->addWidget(m_prev);
    row->addStretch();
    row->addWidget(m_repeat);
    row->addWidget(m_next);
    row->addStretch();
    m_leafletBtn = new QPushButton(tr("Beipackzette&l…"), this);
    m_leafletBtn->setToolTip(tr("Beipackzettel des angezeigten Medikaments anzeigen bzw. im Internet abrufen"));
    m_leafletBtn->setAutoDefault(false);
    row->addWidget(m_leafletBtn);
    row->addWidget(logBtn);
    row->addWidget(close);
    lay->addLayout(row);

    connect(m_prev, &QPushButton::clicked, this, &MedicationAssistant::previous);
    connect(m_repeat, &QPushButton::clicked, this, &MedicationAssistant::repeat);
    connect(m_next, &QPushButton::clicked, this, &MedicationAssistant::next);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_leafletBtn, &QPushButton::clicked, this, [this] {
        if (m_finished || m_index < 0 || m_index >= m_list.size()) return;
        m_speech->stop();
        Medication &cur = m_list[m_index];
        LeafletDialog d(cur, m_patient.id, this);
        d.exec();
        if (!d.medicationChanged() || !cur.id) return;
        cur = d.medication();
        Database::instance().updateMedicationLeaflet(cur.id, cur.pzn, cur.extra);
        for (Medication &m : m_source)
            if (m.id == cur.id) m = cur;
    });
    connect(logBtn, &QPushButton::clicked, this, [this] {
        saveNote();
        MedLogDialog d(m_patient, this);
        d.exec();
    });
    connect(m_slot, &QComboBox::activated, this, &MedicationAssistant::restart);
    {   // Stell-Rhythmus der Person vorbelegen (zuletzt verwendet)
        const MedStock::PrepPlan plan = MedStock::prepPlan(m_patient.id);
        if (plan.weekly()) {
            m_mode->setCurrentIndex(1);
            m_date->setDate(MedStock::nextPrepStart(plan, QDate::currentDate()));
        }
    }
    connect(m_mode, &QComboBox::activated, this, [this] {
        if (m_mode->currentData().toInt() >= 7) {   // Wochendosierer: ab dem gewohnten Wochentag
            MedStock::PrepPlan plan = MedStock::prepPlan(m_patient.id);
            if (!plan.weekly()) plan.weekday = QDate::currentDate().dayOfWeek();
            const QSignalBlocker b(m_date);
            m_date->setDate(MedStock::nextPrepStart(MedStock::PrepPlan{ 7, plan.weekday }, QDate::currentDate()));
        }
        restart();
    });
    connect(m_date, &QDateEdit::dateChanged, this, &MedicationAssistant::restart);
    connect(m_voice, &QComboBox::activated, this, [this] {
        m_speech->setChoice(m_voice->currentData().toString());
        AppSettings::setVoiceGender(m_voice->currentData().toString());   // Wahl merken
        updateVoiceInfo();
        repeat();
    });
    m_speech->setChoice(m_voice->currentData().toString());
    updateVoiceInfo();

    resize(780, 600);
    updateLastLabel();
    restart();
}

QString MedicationAssistant::doseSummary(const Medication &m, int slot)
{
    return MedPrep::doseSummary(m, slot);
}

void MedicationAssistant::updateLastLabel()
{
    const auto last = Database::instance().lastPreparation(m_patient.id);
    if (!last) {
        m_lastLabel->setText(tr("<small>Bisher wurden für %1 keine Medikamente mit dem Assistenten gestellt.</small>")
                                 .arg(m_patient.displayName().toHtmlEscaped()));
        return;
    }
    m_lastLabel->setText(tr("<small>Zuletzt gestellt: <b>%1</b> von %2 (%3) – %4</small>")
        .arg(QLocale().toString(last->preparedAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")).toHtmlEscaped(),
             last->preparedBy.toHtmlEscaped(), tr("für %1").arg(ReportPrinter::preparationForText(*last)).toHtmlEscaped(),
             MedLogDialog::statusText(*last).toHtmlEscaped()));
}

void MedicationAssistant::logPreparation(bool complete)
{
    if (m_prepId) return;   // pro Durchgang nur ein Eintrag
    const MedPrep::LogResult r = MedPrep::log(m_patient, Session::user(), m_list, m_confirmed, complete,
                                              m_slot->currentData().toInt(), m_date->date(), days(),
                                              m_note ? m_note->text() : QString());
    if (!r.ok) {
        qWarning("Stellprotokoll: %s", qPrintable(r.error));
        return;
    }
    m_prepId = r.id;
    m_shortage = r.shortage;
    updateLastLabel();
}

void MedicationAssistant::saveNote()
{
    if (m_prepId && m_note) Database::instance().updatePreparationNote(m_prepId, m_note->text().trimmed());
}

void MedicationAssistant::done(int r)
{
    m_speech->stop();
    if (m_finished) saveNote();
    else if (!m_confirmed.isEmpty()) logPreparation(false);   // abgebrochener Durchgang
    QDialog::done(r);
}

void MedicationAssistant::updateVoiceInfo()
{
    if (!m_speech->available()) {
        m_voiceInfo->setText(tr("<small>⚠ Keine Sprachausgabe verfügbar – der Assistent zeigt die Angaben nur an. "
                                "(Unter Linux z. B. speech-dispatcher oder flite installieren.)</small>"));
        return;
    }
    QString t = tr("<small>Stimme: %1</small>").arg(m_speech->voiceDescription().toHtmlEscaped());
    if (!m_speech->hint().isEmpty())
        t += QStringLiteral("<br><small>⚠ ") + m_speech->hint().toHtmlEscaped() + QStringLiteral("</small>");
    else if (!m_speech->languageMatches())
        t += tr("<small> – ⚠ für die eingestellte Sprache ist keine Systemstimme installiert, "
                "die Aussprache kann fremd klingen.</small>");
    m_voiceInfo->setText(t);
}

QList<Medication> MedicationAssistant::currentMedications(const QList<Medication> &all, const QDate &day)
{
    return MedPrep::currentMedications(all, day);
}

QString MedicationAssistant::speechText(const Medication &m, int slot)
{
    return MedPrep::speechText(m, slot);
}

void MedicationAssistant::applyFilter()
{
    const int slot = m_slot->currentData().toInt();
    const QDate day = m_date ? m_date->date() : QDate::currentDate();
    const int nd = days();
    if (m_forLabel) m_forLabel->setText(nd > 1 ? tr("ab") : tr("für"));
    if (m_rangeLabel)
        m_rangeLabel->setText(nd > 1 ? tr("bis %1").arg(QLocale().toString(day.addDays(nd - 1), QStringLiteral("ddd dd.MM.yyyy"))) : QString());
    const MedPrep::Selection sel = MedPrep::select(m_source, day, nd, slot);
    m_list = sel.list;
    auto join = [](const QStringList &l) {
        QStringList e;
        for (const QString &x : l) e << x.toHtmlEscaped();
        return e.join(QStringLiteral(" · "));
    };
    QStringList info;
    if (!sel.notDue.isEmpty())
        info << (nd > 1 ? tr("<b>In diesen Tagen nicht fällig:</b> %1") : tr("<b>An diesem Tag nicht fällig:</b> %1")).arg(join(sel.notDue));
    if (!sel.asNeeded.isEmpty()) info << tr("<b>Bedarfsmedikation:</b> %1").arg(join(sel.asNeeded));
    if (!sel.other.isEmpty()) info << tr("<b>Sonstiges:</b> %1").arg(join(sel.other));
    if (!sel.emergency.isEmpty())
        info << tr("<span style=\"color:#D52B1E;\"><b>Notfallmedikation:</b> %1</span>").arg(join(sel.emergency));
    if (m_dayInfo) m_dayInfo->setText(info.isEmpty() ? QString() : QStringLiteral("<small>") + info.join(QStringLiteral("<br>")) + QStringLiteral("</small>"));
}

void MedicationAssistant::restart()
{
    if (!m_finished && !m_confirmed.isEmpty()) logPreparation(false);   // Wechsel mitten im Durchgang
    saveNote();
    applyFilter();
    m_finished = false;
    m_index = -1;
    m_confirmed.clear();
    m_prepId = 0;
    if (m_note) m_note->clear();
    if (m_list.isEmpty()) {
        m_stack->setCurrentIndex(1);
        m_doneLabel->setText(days() > 1 ? tr("<p style=\"font-size:16pt;\">Für diese Tage und diesen Zeitpunkt ist nichts zu stellen.</p>")
                                        : tr("<p style=\"font-size:16pt;\">Für diesen Tag und Zeitpunkt ist nichts zu stellen.</p>"));
        m_prev->setEnabled(false);
        m_repeat->setEnabled(false);
        m_next->setEnabled(false);
        speak(MedPrep::nothingText(days()));
        return;
    }
    showMedication(0, false);
    const QString intro = MedPrep::introText(m_patient, m_date->date(), days(), m_slot->currentData().toInt(), int(m_list.size()));
    speak(intro + QLatin1Char(' ') + textFor(m_list.first()));
}

void MedicationAssistant::showMedication(int i, bool speakIt)
{
    m_index = i;
    m_finished = false;
    m_stack->setCurrentIndex(0);
    const Medication &m = m_list[i];
    const int slot = m_slot->currentData().toInt();
    m_progress->setText(tr("<b>Medikament %1 von %2</b>").arg(i + 1).arg(m_list.size()));
    m_name->setText(m.name.toHtmlEscaped());
    QStringList d;
    if (!m.strength.isEmpty()) d << m.strength.toHtmlEscaped();
    if (!m.form.isEmpty()) d << m.form.toHtmlEscaped();
    if (!m.substance.isEmpty()) d << tr("Wirkstoff: %1").arg(m.substance.toHtmlEscaped());
    m_details->setText(d.join(QStringLiteral(" · ")));

    const QColor hi = palette().color(QPalette::Highlight);
    const int nd = days();
    if (nd > 1) {   // Wochendosierer: eine Zeile je Tag
        const QDate start = m_date->date();
        m_doses->clearContents();
        m_doses->setRowCount(nd + 1);
        m_doses->setColumnCount(5);
        m_doses->setMinimumHeight(240);
        m_doses->setMaximumHeight(320);
        m_doses->setItem(0, 0, new QTableWidgetItem());
        for (int s = 0; s < 4; ++s) {
            auto *h = new QTableWidgetItem(slotName(s));
            h->setTextAlignment(Qt::AlignCenter);
            m_doses->setItem(0, s + 1, h);
        }
        for (int i = 0; i < nd; ++i) {
            const QDate d = start.addDays(i);
            const bool due = !currentMedications({ m }, d).isEmpty() && MedSchedule::isDueOn(m, d);
            auto *dl = new QTableWidgetItem(QLocale().toString(d, QStringLiteral("ddd dd.MM.")));
            m_doses->setItem(i + 1, 0, dl);
            for (int s = 0; s < 4; ++s) {
                const QString dose = due ? doseOf(m, s) : QString();
                auto *v = new QTableWidgetItem(dose.isEmpty() ? QStringLiteral("—") : dose);
                v->setTextAlignment(Qt::AlignCenter);
                QFont f = v->font();
                f.setBold(!dose.isEmpty());
                f.setPointSizeF(f.pointSizeF() * (dose.isEmpty() ? 1.0 : 1.25));
                v->setFont(f);
                if (!dose.isEmpty() && (slot < 0 || slot == s)) {
                    v->setBackground(hi);
                    v->setForeground(palette().color(QPalette::HighlightedText));
                } else if (slot >= 0 && slot != s) {
                    v->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
                }
                m_doses->setItem(i + 1, s + 1, v);
            }
        }
    } else {
        m_doses->clearContents();
        m_doses->setRowCount(2);
        m_doses->setColumnCount(4);
        m_doses->setMinimumHeight(110);
        m_doses->setMaximumHeight(130);
        for (int s = 0; s < 4; ++s) {
            auto *h = new QTableWidgetItem(slotName(s));
            h->setTextAlignment(Qt::AlignCenter);
            QString dose = doseOf(m, s);
            auto *v = new QTableWidgetItem(dose.isEmpty() ? QStringLiteral("—")
                                                          : dose + (m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit));
            v->setTextAlignment(Qt::AlignCenter);
            QFont f = v->font();
            f.setPointSizeF(f.pointSizeF() * (dose.isEmpty() ? 1.2 : 1.7));
            f.setBold(!dose.isEmpty());
            v->setFont(f);
            const bool active = !dose.isEmpty() && (slot < 0 || slot == s);
            if (active) {
                v->setBackground(hi);
                v->setForeground(palette().color(QPalette::HighlightedText));
            } else if (slot >= 0 && slot != s) {
                v->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            }
            m_doses->setItem(0, s, h);
            m_doses->setItem(1, s, v);
        }
    }
    QStringList hints;
    if (MedSchedule::isInterval(m))
        hints << tr("<span style=\"color:#D55E00;\"><b>Einnahme:</b> %1</span>").arg(MedSchedule::describe(m).toHtmlEscaped());
    if (!m.rhythmNote.isEmpty())
        hints << tr("<span style=\"color:#D55E00;\"><b>Bemerkung:</b> %1</span>").arg(m.rhythmNote.toHtmlEscaped());
    if (!m.notes.isEmpty()) hints << tr("<b>Hinweis:</b> %1").arg(m.notes.toHtmlEscaped());
    if (!m.reason.isEmpty()) hints << tr("<b>Grund:</b> %1").arg(m.reason.toHtmlEscaped());
    m_hint->setText(hints.join(QStringLiteral("<br>")));

    m_prev->setEnabled(i > 0);
    if (m_leafletBtn) {
        m_leafletBtn->setEnabled(true);
        m_leafletBtn->setText(m.extra.contains(QLatin1String(Leaflet::kLeafletDocKey)) ? tr("Beipackzette&l ✔…")
                                                                                       : tr("Beipackzette&l…"));
    }
    m_repeat->setEnabled(true);
    m_next->setEnabled(true);
    m_next->setText(i + 1 < m_list.size() ? tr("&Weiter ▶") : tr("&Fertig ✔"));
    m_next->setFocus();
    if (speakIt) speak(textFor(m));
}

void MedicationAssistant::next()
{
    if (m_finished) { restart(); return; }
    if (m_index >= 0) m_confirmed.insert(m_index);
    if (m_index + 1 < m_list.size()) showMedication(m_index + 1, true);
    else showFinished();
}

void MedicationAssistant::previous()
{
    if (m_finished) { showMedication(m_list.size() - 1, true); return; }
    if (m_index > 0) showMedication(m_index - 1, true);
}

void MedicationAssistant::repeat()
{
    if (m_finished) { speak(MedPrep::doneText(), true); Sounds::play(Sounds::Kind::Done); return; }
    if (m_index >= 0 && m_index < m_list.size())
        speak(textFor(m_list[m_index]), true);   // immer vorlesen, auch ohne "automatisch"
}

void MedicationAssistant::showFinished()
{
    m_finished = true;
    m_stack->setCurrentIndex(1);
    m_doneLabel->setText(QStringLiteral(
        "<p style=\"font-size:64pt; color:#2e9d4f;\">✔</p>"
        "<p style=\"font-size:26pt; font-weight:bold; color:#2e9d4f;\">%1</p>"
        "<p style=\"font-size:13pt;\">%2</p>")
        .arg(tr("Medikamente sind gerichtet"),
             tr("%1 Medikament(e) für %2").arg(m_list.size()).arg(m_patient.displayName().toHtmlEscaped())
                 + (days() > 1 ? QStringLiteral("<br>") + tr("Wochendosierer %1 – %2")
                                     .arg(QLocale().toString(m_date->date(), QStringLiteral("ddd dd.MM.")),
                                          QLocale().toString(m_date->date().addDays(days() - 1), QStringLiteral("ddd dd.MM.")))
                               : QString())));
    m_prev->setEnabled(true);
    m_repeat->setEnabled(true);
    m_next->setText(tr("&Von vorn"));
    if (m_leafletBtn) m_leafletBtn->setEnabled(false);
    logPreparation(true);
    m_loggedLabel->setText(tr("<small>Im Protokoll vermerkt: %1 von %2</small>")
        .arg(QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")),
             Session::user().toHtmlEscaped()));
    Sounds::play(Sounds::Kind::Done);
    // Sprachhinweis nach dem Signalton
    QTimer::singleShot(900, this, [this] { if (m_finished) speak(MedPrep::doneText(), true); });
}

void MedicationAssistant::speak(const QString &text, bool force)
{
    if (force || m_autoRead->isChecked())
        m_speech->say(text);
}

void MedicationAssistant::keyPressEvent(QKeyEvent *e)
{
    if (qobject_cast<QLineEdit *>(focusWidget())) {   // Eingabe der Bemerkung
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { saveNote(); m_next->setFocus(); return; }
        QDialog::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Right:
        if (m_next->isEnabled()) next();
        return;
    case Qt::Key_Left:
    case Qt::Key_Backspace:
        if (m_prev->isEnabled()) previous();
        return;
    case Qt::Key_R:
        repeat();
        return;
    default:
        QDialog::keyPressEvent(e);
    }
}

void MedicationAssistant::closeEvent(QCloseEvent *e)
{
    m_speech->stop();
    QDialog::closeEvent(e);
}

int MedicationAssistant::days() const
{
    return m_mode ? std::max(1, m_mode->currentData().toInt()) : 1;
}

void MedicationAssistant::setWeekly(bool weekly)
{
    m_mode->setCurrentIndex(weekly ? 1 : 0);
    restart();
}

bool MedicationAssistant::dueInRange(const Medication &m, int slot) const
{
    return MedPrep::dueInRange(m, m_date->date(), days(), slot);
}

QString MedicationAssistant::textFor(const Medication &m) const
{
    return MedPrep::textFor(m, m_date->date(), days(), m_slot->currentData().toInt());
}

QString MedicationAssistant::weekSpeechText(const Medication &m, const QDate &start, int days, int slot)
{
    return MedPrep::weekSpeechText(m, start, days, slot);
}

void MedicationAssistant::setDay(const QDate &d)
{
    m_date->setDate(d);   // löst restart() aus
}
