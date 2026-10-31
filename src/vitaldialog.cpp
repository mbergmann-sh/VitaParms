// SPDX-License-Identifier: MIT
#include "vitaldialog.h"
#include "appsettings.h"
#include "bmi.h"
#include "database.h"
#include "edition.h"
#include "formloader.h"
#include "peakflow.h"
#include "stool.h"
#include "inr.h"
#include "fluid.h"
#include "vitaldefs.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <cmath>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QTextDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include "sounds.h"

static QLayout *layoutContaining(QLayout *l, QWidget *w);

// Höhe eines Rich-Text-Hinweises zur tatsächlichen Breite (QLabel schätzt sie im Formular zu groß,
// oder das Formular schneidet mehrzeilige Hinweise ab)
static void fitRichLabel(QLabel *l)
{
    if (!l) return;
    const int w = l->contentsRect().width();
    if (w > 100) {
        QTextDocument doc;
        doc.setDefaultFont(l->font());
        doc.setDocumentMargin(0);
        doc.setHtml(l->text());
        doc.setTextWidth(w);
        l->setFixedHeight(int(std::ceil(doc.size().height())) + 4);
    }
    l->updateGeometry();
}

VitalDialog::VitalDialog(const Patient &patient, const VitalRecord &record, Mode mode,
                         QWidget *parent)
    : QDialog(parent), m_patient(patient), m_record(record), m_mode(mode)
{
    setWindowTitle(mode == Mode::Admission ? tr("Vitalwerte bei Aufnahme")
                   : record.id ? tr("Messung bearbeiten") : tr("Neue Messung"));

    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("vitalform"), this);
    lay->addWidget(FormLoader::scrollable(m_form, this), 1);      // Knöpfe bleiben immer sichtbar

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (mode == Mode::Journal && record.id == 0) {
        m_nextButton = buttons->addButton(tr("Speichern && &weitere Messung"),
                                          QDialogButtonBox::ActionRole);
        m_nextButton->setToolTip(tr("Speichert die Messung und öffnet sofort eine neue "
                                    "(z. B. für Tagesprofile)."));
        connect(m_nextButton, &QPushButton::clicked, this, &VitalDialog::saveAndNext);
    }
    buttons->button(QDialogButtonBox::Save)->setText(tr("&Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &VitalDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &VitalDialog::reject);
    lay->addWidget(buttons);

    // Kopfzeile aus dem Stammblatt
    FormIO::write(m_form, QStringLiteral("headerNameLabel"), patient.lastName);
    FormIO::write(m_form, QStringLiteral("headerFirstNameLabel"), patient.firstName);
    QString geb;
    if (patient.birthDate.isValid()) {
        geb = tr("geb. %1").arg(QLocale().toString(patient.birthDate, QStringLiteral("dd.MM.yyyy")));
        const int age = patient.ageAt(QDate::currentDate());
        if (age >= 0) geb += tr(" (%1 J.)").arg(age);
    }
    FormIO::write(m_form, QStringLiteral("headerBirthDateLabel"), geb);

    // Messzeitpunkte für Tagesprofile
    if (auto *cb = m_form->findChild<QComboBox *>(QStringLiteral("contextCombo"))) {
        cb->clear();
        cb->addItem(QString());
        cb->addItems(AppSettings::profileContexts());
        if (mode == Mode::Admission) cb->setEnabled(false);
    }

    configureGlucose();
    setupBreathCounter();
    setupPainScale();
    setupPeakFlow();
    setupStool();
    setupCareValues();

    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("nowButton"))) {
        connect(b, &QPushButton::clicked, this, [this] {
            FormIO::write(m_form, QStringLiteral("dateTimeEdit"),
                          QDateTime::currentDateTime().addSecs(-QTime::currentTime().second()));
        });
    }

    m_fallbackHeight = Database::instance().latestHeight(patient.id);

    for (const QString &n : { QStringLiteral("weightSpin"), QStringLiteral("heightSpin") }) {
        if (auto *s = m_form->findChild<QDoubleSpinBox *>(n))
            connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VitalDialog::updateBmi);
        else if (auto *s2 = m_form->findChild<QSpinBox *>(n))
            connect(s2, qOverload<int>(&QSpinBox::valueChanged), this, &VitalDialog::updateBmi);
    }
    if (m_peakFlowInfo) {
        if (auto *s = m_form->findChild<QSpinBox *>(QStringLiteral("peakFlowSpin")))
            connect(s, qOverload<int>(&QSpinBox::valueChanged), this, &VitalDialog::updatePeakFlow);
        if (auto *s = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("heightSpin")))
            connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VitalDialog::updatePeakFlow);
        else if (auto *s2 = m_form->findChild<QSpinBox *>(QStringLiteral("heightSpin")))
            connect(s2, qOverload<int>(&QSpinBox::valueChanged), this, &VitalDialog::updatePeakFlow);
        if (auto *d = m_form->findChild<QDateTimeEdit *>(QStringLiteral("dateTimeEdit")))
            connect(d, &QDateTimeEdit::dateTimeChanged, this, &VitalDialog::updatePeakFlow);
    }

    if (!m_record.measuredAt.isValid())
        m_record.measuredAt = QDateTime(QDate::currentDate(),
                                        QTime(QTime::currentTime().hour(), QTime::currentTime().minute()));
    if (mode == Mode::Admission)
        m_record.isAdmission = true;
    loadRecord(m_record);

    // zweispaltiges Formular (ab 0.21): eigener Schlüssel, damit die alte schmale Größe nicht greift
    const QByteArray geo = AppSettings::windowState(QStringLiteral("vitaldialog2"));
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(1000, 800);
    FormLoader::fitToScreen(this);
    setMinimumWidth(m_form->findChild<QWidget *>(QStringLiteral("rightColumn")) || m_form->findChild<QWidget *>(QStringLiteral("fluidGroup")) ? 780 : 500);
}

void VitalDialog::configureGlucose()
{
    auto *s = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("glucoseSpin"));
    const bool mmol = AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL;
    if (s) {
        if (mmol) {
            s->setDecimals(1);
            s->setRange(0.4, 55.5);
            s->setSingleStep(0.1);
        } else {
            s->setDecimals(0);
            s->setRange(9, 999);
            s->setSingleStep(1);
        }
        if (s->specialValueText().isEmpty())
            s->setSpecialValueText(QStringLiteral("—"));
        s->setValue(s->minimum());
    }
    FormIO::write(m_form, QStringLiteral("glucoseUnitLabel"), VitalDefs::unit(VitalKey::Glucose));
}

void VitalDialog::loadRecord(const VitalRecord &r)
{
    auto iv = [](const std::optional<int> &o) { return o ? QVariant(*o) : QVariant(); };
    auto dv = [](const std::optional<double> &o) { return o ? QVariant(*o) : QVariant(); };

    FormIO::write(m_form, QStringLiteral("dateTimeEdit"), r.measuredAt);
    FormIO::write(m_form, QStringLiteral("contextCombo"), r.context);
    FormIO::write(m_form, QStringLiteral("pulseSpin"), iv(r.pulse));
    FormIO::write(m_form, QStringLiteral("systolicSpin"), iv(r.systolic));
    FormIO::write(m_form, QStringLiteral("diastolicSpin"), iv(r.diastolic));
    FormIO::write(m_form, QStringLiteral("temperatureSpin"), dv(r.temperature));
    FormIO::write(m_form, QStringLiteral("spo2Spin"), iv(r.spo2));
    FormIO::write(m_form, QStringLiteral("peakFlowSpin"), iv(r.peakFlow));
    FormIO::write(m_form, QStringLiteral("glucoseSpin"),
                  r.glucose ? QVariant(VitalDefs::glucoseToDisplay(*r.glucose)) : QVariant());
    FormIO::write(m_form, QStringLiteral("weightSpin"), dv(r.weight));
    // Neue Messung: bekannte Körpergröße (Aufnahme bzw. letzte Angabe) als änderbare Vorgabe
    m_heightPrefilled = m_mode == Mode::Journal && r.id == 0 && !r.height && m_fallbackHeight;
    FormIO::write(m_form, QStringLiteral("heightSpin"), dv(m_heightPrefilled ? m_fallbackHeight : r.height));
    FormIO::write(m_form, QStringLiteral("observationsEdit"), r.observations);
    if (m_stoolCheck) {
        m_stoolCheck->setChecked(r.stool.has_value());
        m_stoolCombo->setCurrentIndex(std::max(0, m_stoolCombo->findData(r.stool.value_or(0))));
        m_stoolCombo->setEnabled(r.stool.has_value());
        if (m_stoolDetails) m_stoolDetails->setVisible(r.stool.has_value());
        if (m_stoolAmount) m_stoolAmount->setCurrentText(r.stoolDetail.amount);
        if (m_stoolColor) m_stoolColor->setCurrentText(r.stoolDetail.color);
        if (m_stoolAdmixture) m_stoolAdmixture->setCurrentText(r.stoolDetail.admixture);
        if (m_stoolSymptoms) m_stoolSymptoms->setText(r.stoolDetail.symptoms);
    }
    FormIO::write(m_form, QStringLiteral("inrSpin"), r.inr ? QVariant(*r.inr) : QVariant());
    FormIO::write(m_form, QStringLiteral("fluidInSpin"), iv(r.fluidIn));
    FormIO::write(m_form, QStringLiteral("fluidOutSpin"), iv(r.fluidOut));
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("fluidInKindCombo")))
        c->setCurrentText(r.fluidIn ? (r.fluidInKind.isEmpty() ? QString() : r.fluidInKind) : Fluid::inKinds().value(0));
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("fluidOutKindCombo")))
        c->setCurrentText(r.fluidOut ? r.fluidOutKind : Fluid::outKinds().value(0));
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("edemaCombo")))
        c->setCurrentIndex(std::max(0, c->findData(r.edema ? *r.edema : -1)));
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("edemaSiteCombo"))) c->setCurrentText(r.edemaSite);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("edemaSideCombo")))
        c->setCurrentIndex(std::max(0, c->findText(r.edemaSide)));
    FormIO::applyExtra(m_form, r.extra);
    FormIO::write(m_form, QStringLiteral("painSpin"), iv(r.pain));   // nach applyExtra (ältere Formulare: Zusatzfeld)
    updateBmi();
    updateCareInfo();
    updatePeakFlow();

    // Fokus auf das erste Wertefeld
    if (auto *w = m_form->findChild<QWidget *>(QStringLiteral("systolicSpin")))
        w->setFocus();
}

static std::optional<int> readInt(QWidget *form, const QString &name, std::optional<int> old)
{
    const auto v = FormIO::read(form, name);
    if (!v) return old;                // Feld nicht im Formular
    if (!v->isValid() || v->isNull()) return std::nullopt;
    return qRound(v->toDouble());
}

static std::optional<double> readDouble(QWidget *form, const QString &name, std::optional<double> old)
{
    const auto v = FormIO::read(form, name);
    if (!v) return old;
    if (!v->isValid() || v->isNull()) return std::nullopt;
    return v->toDouble();
}

bool VitalDialog::collect(VitalRecord &r, bool interactive)
{
    if (const auto v = FormIO::read(m_form, QStringLiteral("dateTimeEdit")); v && v->toDateTime().isValid())
        r.measuredAt = v->toDateTime();
    r.context      = FormIO::readString(m_form, QStringLiteral("contextCombo"), r.context).trimmed();
    r.pulse        = readInt(m_form, QStringLiteral("pulseSpin"), r.pulse);
    r.systolic     = readInt(m_form, QStringLiteral("systolicSpin"), r.systolic);
    r.diastolic    = readInt(m_form, QStringLiteral("diastolicSpin"), r.diastolic);
    r.temperature  = readDouble(m_form, QStringLiteral("temperatureSpin"), r.temperature);
    r.spo2         = readInt(m_form, QStringLiteral("spo2Spin"), r.spo2);
    r.peakFlow     = readInt(m_form, QStringLiteral("peakFlowSpin"), r.peakFlow);
    if (const auto v = FormIO::read(m_form, QStringLiteral("glucoseSpin"))) {
        r.glucose = (v->isValid() && !v->isNull())
                        ? std::optional<double>(VitalDefs::glucoseFromDisplay(v->toDouble()))
                        : std::nullopt;
    }
    r.weight       = readDouble(m_form, QStringLiteral("weightSpin"), r.weight);
    r.height       = readDouble(m_form, QStringLiteral("heightSpin"), r.height);
    r.observations = FormIO::readString(m_form, QStringLiteral("observationsEdit"), r.observations);
    if (const auto v = FormIO::read(m_form, QStringLiteral("inrSpin")))
        r.inr = (v->isValid() && !v->isNull()) ? std::optional<double>(std::round(v->toDouble() * 10) / 10) : std::nullopt;
    r.pain         = readInt(m_form, QStringLiteral("painSpin"), r.pain);
    if (r.pain && (*r.pain < 0 || *r.pain > 10)) r.pain.reset();
    r.fluidIn      = readInt(m_form, QStringLiteral("fluidInSpin"), r.fluidIn);
    r.fluidOut     = readInt(m_form, QStringLiteral("fluidOutSpin"), r.fluidOut);
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("fluidInKindCombo")))
        r.fluidInKind = r.fluidIn ? c->currentText().trimmed() : QString();
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("fluidOutKindCombo")))
        r.fluidOutKind = r.fluidOut ? c->currentText().trimmed() : QString();
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("edemaCombo"))) {
        const int g = c->currentData().toInt();
        r.edema = g >= 0 ? std::optional<int>(g) : std::nullopt;
        auto *site = m_form->findChild<QComboBox *>(QStringLiteral("edemaSiteCombo"));
        auto *side = m_form->findChild<QComboBox *>(QStringLiteral("edemaSideCombo"));
        r.edemaSite = g > 0 && site ? site->currentText().trimmed() : QString();
        r.edemaSide = g > 0 && side ? side->currentText().trimmed() : QString();
    }
    if (m_stoolCheck) {
        r.stool = m_stoolCheck->isChecked() ? std::optional<int>(m_stoolCombo->currentData().toInt()) : std::nullopt;
        if (!r.stool) {
            r.stoolDetail = {};
        } else {
            if (m_stoolAmount) r.stoolDetail.amount = m_stoolAmount->currentText().trimmed();
            if (m_stoolColor) r.stoolDetail.color = m_stoolColor->currentText().trimmed();
            if (m_stoolAdmixture) r.stoolDetail.admixture = m_stoolAdmixture->currentText().trimmed();
            if (m_stoolSymptoms) r.stoolDetail.symptoms = m_stoolSymptoms->text().trimmed();
        }
    }
    r.extra        = FormIO::collectExtra(m_form, r.extra);
    // Ältere angepasste Formulare mit eigenem Feld "Peak-Flow": Wert in den festen Messwert übernehmen
    static const QRegularExpression peakKey(QStringLiteral("^\\s*(peak[- ]?flow|pef)\\s*$"), QRegularExpression::CaseInsensitiveOption);
    for (auto it = r.extra.begin(); it != r.extra.end();) {
        if (Database::isPainFieldKey(it.key())) {   // ältere Formulare: eigenes Feld „Schmerz (NRS)“
            const int v = it.value().isDouble() ? qRound(it.value().toDouble()) : it.value().toString().trimmed().toInt();
            if (!r.pain && v >= 0 && v <= 10 && (it.value().isDouble() || !it.value().toString().trimmed().isEmpty())) r.pain = v;
            it = r.extra.erase(it);
            continue;
        }
        if (!peakKey.match(it.key()).hasMatch()) { ++it; continue; }
        const int v = it.value().isDouble() ? qRound(it.value().toDouble()) : it.value().toString().toInt();
        if (!r.peakFlow && v > 0) r.peakFlow = v;
        it = r.extra.erase(it);
    }
    r.bmi          = Bmi::compute(r.weight, r.height ? r.height : m_fallbackHeight,
                                  m_patient.amputations);
    r.patientId    = m_patient.id;
    if (m_mode == Mode::Admission)
        r.isAdmission = true;

    if (!interactive)
        return true;

    if (!r.hasAnyValue()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte mindestens einen Wert eingeben."));
        return false;
    }
    if (r.systolic.has_value() != r.diastolic.has_value()) {
        if (QMessageBox::question(this, windowTitle(),
                tr("Beim Blutdruck ist nur ein Wert (systolisch oder diastolisch) eingetragen.\n"
                   "Trotzdem speichern?")) != QMessageBox::Yes)
            return false;
    }
    if (r.systolic && r.diastolic && *r.systolic <= *r.diastolic) {
        if (QMessageBox::question(this, windowTitle(),
                tr("Der systolische Wert (%1) ist nicht größer als der diastolische (%2).\n"
                   "Wurden die Werte vertauscht? Trotzdem speichern?")
                    .arg(*r.systolic).arg(*r.diastolic)) != QMessageBox::Yes)
            return false;
    }
    if (r.measuredAt > QDateTime::currentDateTime().addSecs(15 * 60)) {
        if (QMessageBox::question(this, windowTitle(),
                tr("Der Messzeitpunkt liegt in der Zukunft. Trotzdem speichern?"))
            != QMessageBox::Yes)
            return false;
    }
    return true;
}

void VitalDialog::updateBmi()
{
    VitalRecord tmp = m_record;
    collect(tmp, false);

    QString value = QStringLiteral("—");
    QStringList info;
    if (m_heightPrefilled && tmp.height && m_fallbackHeight && qFuzzyCompare(*tmp.height, *m_fallbackHeight))
        info << tr("Körpergröße aus früherer Angabe übernommen – bei Bedarf ändern.");
    if (tmp.bmi) {
        value = tr("%1 kg/m²").arg(QLocale().toString(*tmp.bmi, 'f', 1));
        const int age = m_patient.ageAt(tmp.measuredAt.date());
        if (age >= 18 || age < 0)
            value += QStringLiteral(" – ") + Bmi::whoCategory(*tmp.bmi);
        else
            info << tr("Hinweis: Die WHO-Einteilung gilt nur für Erwachsene; bei Kindern "
                       "und Jugendlichen werden Perzentilen verwendet.");
        if (!tmp.height && m_fallbackHeight)
            info << tr("Größe aus letzter Messung: %1 cm")
                        .arg(QLocale().toString(*m_fallbackHeight, 'f', 0));
        if (m_patient.amputations.any() && tmp.weight) {
            info << tr("Korrigiert für fehlende Gliedmaßen (%1 %): geschätztes Gesamtgewicht %2 kg")
                        .arg(QLocale().toString(Bmi::missingPercent(m_patient.amputations), 'f', 1),
                             QLocale().toString(Bmi::correctedWeight(*tmp.weight, m_patient.amputations), 'f', 1));
        }
    } else if (tmp.weight && !tmp.height && !m_fallbackHeight) {
        info << tr("Für den BMI wird die Körpergröße benötigt.");
    }
    FormIO::write(m_form, QStringLiteral("bmiValueLabel"), value);
    FormIO::write(m_form, QStringLiteral("bmiInfoLabel"), info.join(QLatin1Char('\n')));
}

bool VitalDialog::store()
{
    VitalRecord r = m_record;
    if (!collect(r, true))
        return false;
    if (m_mode == Mode::Journal) {
        QString err;
        if (!Database::instance().saveVital(r, &err)) {
            QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
            return false;
        }
        ++m_savedCount;
        if (r.height) m_fallbackHeight = r.height;
    }
    m_record = r;
    warnPeakFlow(r);
    warnStool(r);
    warnCareValues(r);
    if (m_mode == Mode::Journal) m_recent << r;
    return true;
}

void VitalDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    if (m_peakFlowInfo) QTimer::singleShot(0, this, &VitalDialog::updatePeakFlow);
    if (m_inrInfo) QTimer::singleShot(0, this, [this] { fitRichLabel(m_inrInfo); });
}

void VitalDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // nie höher als der Bildschirm: die Knöpfe unten bleiben erreichbar, das Formular scrollt
    QTimer::singleShot(0, this, [this] { FormLoader::fitToScreen(this); });
    if (m_peakFlowInfo) QTimer::singleShot(0, this, &VitalDialog::updatePeakFlow);
    if (m_inrInfo) QTimer::singleShot(0, this, [this] { fitRichLabel(m_inrInfo); });
}

// Peak Flow: Hinweiszeile unter dem Feld (eingefügt, damit auch angepasste Formulare sie bekommen)
void VitalDialog::setupPeakFlow()
{
    auto *spin = m_form->findChild<QSpinBox *>(QStringLiteral("peakFlowSpin"));
    if (!spin) return;
    m_peakFlowInfo = new QLabel(m_form);
    m_peakFlowInfo->setObjectName(QStringLiteral("peakFlowInfoLabel"));
    m_peakFlowInfo->setWordWrap(true);
    m_peakFlowInfo->setTextFormat(Qt::RichText);
    m_peakFlowInfo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    if (auto *form = qobject_cast<QFormLayout *>(spin->parentWidget() ? spin->parentWidget()->layout() : nullptr)) {
        int row = -1;
        QFormLayout::ItemRole role;
        form->getWidgetPosition(spin, &row, &role);
        if (row >= 0) { form->insertRow(row + 1, QString(), m_peakFlowInfo); return; }
    }
    if (auto *l = spin->parentWidget() ? spin->parentWidget()->layout() : nullptr) l->addWidget(m_peakFlowInfo);
}

void VitalDialog::updatePeakFlow()
{
    if (!m_peakFlowInfo) return;
    VitalRecord tmp = m_record;
    collect(tmp, false);
    const std::optional<double> height = tmp.height ? tmp.height : m_fallbackHeight;
    const QDate day = tmp.measuredAt.date();
    QString html;
    m_peakFlowInfo->setToolTip(QString());
    const bool dark = palette().base().color().lightness() < 128;
    if (tmp.peakFlow) {
        const PeakFlow::Assessment a = PeakFlow::assess(m_patient, *tmp.peakFlow, day, height);
        if (a.zone != PeakFlow::Zone::None) {
            html = QStringLiteral("<span style=\"color:%1\"><b>%2</b></span>")
                       .arg(PeakFlow::zoneColor(a.zone, dark).name(), a.text.toHtmlEscaped());
            if (!a.advice.isEmpty())
                html += QStringLiteral("<br><span style=\"color:%1\">%2</span>")
                            .arg(PeakFlow::zoneColor(a.zone, dark).name(), a.advice.toHtmlEscaped());
            m_peakFlowInfo->setToolTip(tr("Bezug: %1").arg(a.ref.description));
        } else {
            html = a.ref.missing.toHtmlEscaped();
        }
    } else {
        const PeakFlow::Reference ref = PeakFlow::reference(m_patient, day, height);
        html = (ref.value ? PeakFlow::limitsText(ref) : ref.missing).toHtmlEscaped();
    }
    m_peakFlowInfo->setText(QStringLiteral("<small>%1</small>").arg(html));
    m_peakFlowInfo->setVisible(!html.isEmpty());
    fitRichLabel(m_peakFlowInfo);
}

// Stuhlgang: Häkchen „ja“ schaltet die Auswahl nach der Bristol-Skala frei. Fehlen die Felder
// (angepasstes Formular einer älteren Vorlage), werden sie unter der Körpergröße eingefügt.
void VitalDialog::setupStool()
{
    m_stoolCheck = m_form->findChild<QCheckBox *>(QStringLiteral("stoolCheck"));
    m_stoolCombo = m_form->findChild<QComboBox *>(QStringLiteral("stoolCombo"));
    if ((!m_stoolCheck || !m_stoolCombo) && Edition::light()) {   // VitaParms light erfasst keinen Stuhlgang
        m_stoolCheck = nullptr;
        m_stoolCombo = nullptr;
        return;
    }
    if (!m_stoolCheck || !m_stoolCombo) {
        QWidget *anchor = m_form->findChild<QWidget *>(QStringLiteral("heightSpin"));
        if (!anchor) anchor = m_form->findChild<QWidget *>(QStringLiteral("weightSpin"));
        auto *form = anchor && anchor->parentWidget() ? qobject_cast<QFormLayout *>(anchor->parentWidget()->layout()) : nullptr;
        int row = -1;
        QFormLayout::ItemRole role;
        if (form) form->getWidgetPosition(anchor, &row, &role);
        if (!form || row < 0) { m_stoolCheck = nullptr; m_stoolCombo = nullptr; return; }
        auto *box = new QWidget(anchor->parentWidget());
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        m_stoolCheck = new QCheckBox(tr("ja"), box);
        m_stoolCheck->setObjectName(QStringLiteral("stoolCheck"));
        m_stoolCombo = new QComboBox(box);
        m_stoolCombo->setObjectName(QStringLiteral("stoolCombo"));
        h->addWidget(m_stoolCheck);
        h->addWidget(m_stoolCombo, 1);
        auto *lbl = new QLabel(tr("St&uhlgang"), anchor->parentWidget());
        lbl->setBuddy(m_stoolCheck);
        form->insertRow(row + 1, lbl, box);
    }
    m_stoolCombo->clear();
    for (int t = 0; t <= Stool::kTypes; ++t) {
        m_stoolCombo->addItem(Stool::choiceText(t), t);
        if (t > 0) m_stoolCombo->setItemData(t, Stool::groupName(Stool::group(t)), Qt::ToolTipRole);
    }
    m_stoolCombo->setToolTip(Stool::scaleNote());
    m_stoolCombo->setEnabled(false);
    m_stoolCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_stoolCombo->setMinimumContentsLength(18);
    connect(m_stoolCheck, &QCheckBox::toggled, m_stoolCombo, &QWidget::setEnabled);

    // Angaben fürs Stuhlprotokoll (nur sichtbar, wenn „ja“ angekreuzt ist)
    m_stoolDetails = m_form->findChild<QWidget *>(QStringLiteral("stoolDetails"));
    m_stoolAmount = m_form->findChild<QComboBox *>(QStringLiteral("stoolAmountCombo"));
    m_stoolColor = m_form->findChild<QComboBox *>(QStringLiteral("stoolColorCombo"));
    m_stoolAdmixture = m_form->findChild<QComboBox *>(QStringLiteral("stoolAdmixtureCombo"));
    m_stoolSymptoms = m_form->findChild<QLineEdit *>(QStringLiteral("stoolSymptomsEdit"));
    auto fill = [](QComboBox *c, const QStringList &items) {
        if (!c) return;
        c->clear();
        c->setEditable(true);
        c->addItem(QString());
        c->addItems(items);
        c->setInsertPolicy(QComboBox::NoInsert);
    };
    fill(m_stoolAmount, Stool::amountChoices());
    fill(m_stoolColor, Stool::colorChoices());
    fill(m_stoolAdmixture, Stool::admixtureChoices());
    if (m_stoolAdmixture && m_stoolAdmixture->lineEdit())
        m_stoolAdmixture->lineEdit()->setPlaceholderText(tr("mehreres mit Komma trennen"));
    if (m_stoolSymptoms) m_stoolSymptoms->setPlaceholderText(Stool::symptomsHint());
    if (m_stoolDetails) {
        m_stoolDetails->setVisible(false);
        connect(m_stoolCheck, &QCheckBox::toggled, m_stoolDetails, &QWidget::setVisible);
        // eingeblendete Angaben in den sichtbaren Bereich rollen
        connect(m_stoolCheck, &QCheckBox::toggled, this, [this](bool on) {
            if (!on) return;
            QTimer::singleShot(0, this, [this] {
                if (auto *sa = findChild<QScrollArea *>(QStringLiteral("formScroll"))) sa->ensureWidgetVisible(m_stoolDetails, 10, 20);
            });
        });
    }

    // Hinweis bei neuer Messung: seit mindestens drei Tagen kein Stuhlgang eingetragen
    if (m_mode != Mode::Journal || m_record.id != 0 || !m_patient.id) return;
    const Stool::Status st = Stool::status(Database::instance().vitals(m_patient.id));
    if (!st.warn) return;
    m_stoolInfo = new QLabel(m_stoolCombo->parentWidget());
    m_stoolInfo->setObjectName(QStringLiteral("stoolInfoLabel"));
    m_stoolInfo->setWordWrap(true);
    const bool dark = palette().base().color().lightness() < 128;
    m_stoolInfo->setStyleSheet(QStringLiteral("color: %1; font-size: 8.5pt;")
                                   .arg(Stool::groupColor(Stool::Group::Hard, dark).darker(dark ? 100 : 115).name()));
    m_stoolInfo->setText(tr("Seit %1 Tagen kein Stuhlgang eingetragen.").arg(st.daysWithout));
    m_stoolInfo->setToolTip(Stool::warningText(st));
    QWidget *row = m_stoolCheck->parentWidget();
    if (auto *form = qobject_cast<QFormLayout *>(row ? row->layout() : nullptr)) {
        // Häkchen liegt direkt im Formular (eingebautes Formular: Zeile in einer QHBoxLayout)
        int r = -1;
        QFormLayout::ItemRole role;
        QLayout *hl = layoutContaining(form, m_stoolCheck);
        for (int i = 0; i < form->rowCount() && r < 0; ++i) {
            QLayoutItem *it = form->itemAt(i, QFormLayout::FieldRole);
            if (it && (it->layout() == hl || it->widget() == m_stoolCheck->parentWidget())) r = i;
        }
        if (r < 0) form->getWidgetPosition(m_stoolCheck, &r, &role);
        if (r >= 0) { form->insertRow(r + 1, QString(), m_stoolInfo); return; }
    }
    if (QLayout *l = layoutContaining(m_form->layout(), m_stoolCombo)) l->addWidget(m_stoolInfo);
}

// INR, Ein- und Ausfuhr, Ödeme: Auswahllisten, Hinweiszeilen (auch in älteren Formularen ohne diese
// Felder unschädlich – dann bleibt alles, wie es ist)
void VitalDialog::setupCareValues()
{
    if (m_patient.id && m_mode == Mode::Journal)
        m_recent = Database::instance().vitals(m_patient.id, QDateTime(QDate::currentDate().addDays(-10), QTime(0, 0)), {});
    auto fill = [](QComboBox *c, const QStringList &items) {
        if (!c) return;
        c->clear();
        c->addItems(items);
        c->setInsertPolicy(QComboBox::NoInsert);
    };
    fill(m_form->findChild<QComboBox *>(QStringLiteral("fluidInKindCombo")), Fluid::inKinds());
    fill(m_form->findChild<QComboBox *>(QStringLiteral("fluidOutKindCombo")), Fluid::outKinds());
    fill(m_form->findChild<QComboBox *>(QStringLiteral("edemaSiteCombo")), QStringList{ QString() } + Fluid::edemaSites());
    fill(m_form->findChild<QComboBox *>(QStringLiteral("edemaSideCombo")), QStringList{ QString() } + Fluid::edemaSides());
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("edemaCombo"))) {
        c->clear();
        c->addItem(QStringLiteral("—"), -1);
        for (int g = 0; g <= Fluid::kEdemaMax; ++g) {
            c->addItem(Fluid::edemaName(g), g);
            c->setItemData(c->count() - 1, Fluid::edemaDescription(g), Qt::ToolTipRole);
        }
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &VitalDialog::updateCareInfo);
    }
    for (const char *n : { "edemaSiteCombo", "edemaSideCombo" })
        if (auto *c = m_form->findChild<QComboBox *>(QLatin1String(n)))
            connect(c, &QComboBox::currentTextChanged, this, &VitalDialog::updateCareInfo);
    for (const char *n : { "fluidInSpin", "fluidOutSpin" })
        if (auto *sp = m_form->findChild<QSpinBox *>(QLatin1String(n)))
            connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, &VitalDialog::updateCareInfo);
    // INR: Hinweis unter dem Feld (wie beim Peak Flow eingefügt)
    if (auto *spin = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("inrSpin"))) {
        m_inrInfo = new QLabel(m_form);
        m_inrInfo->setObjectName(QStringLiteral("inrInfoLabel"));
        m_inrInfo->setWordWrap(true);
        m_inrInfo->setTextFormat(Qt::RichText);
        if (auto *form = qobject_cast<QFormLayout *>(spin->parentWidget() ? spin->parentWidget()->layout() : nullptr)) {
            int row = -1;
            QFormLayout::ItemRole role;
            form->getWidgetPosition(spin, &row, &role);
            if (row >= 0) form->insertRow(row + 1, QString(), m_inrInfo);
        }
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VitalDialog::updateCareInfo);
    }
}

void VitalDialog::updateCareInfo()
{
    const bool dark = palette().base().color().lightness() < 128;
    VitalRecord tmp = m_record;
    collect(tmp, false);
    if (m_inrInfo) {
        QString h;
        if (tmp.inr) {
            const Inr::Assessment a = Inr::assess(m_patient, *tmp.inr);
            h = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(Inr::zoneColor(a.zone, dark).name(), a.text.toHtmlEscaped());
        } else {
            h = Inr::rangeText(Inr::range(m_patient)).toHtmlEscaped();
        }
        m_inrInfo->setText(QStringLiteral("<small>%1</small>").arg(h));
        fitRichLabel(m_inrInfo);
    }
    if (auto *l = m_form->findChild<QLabel *>(QStringLiteral("fluidInfoLabel"))) {
        // Bilanz heute einschließlich dieser Messung
        const QDate day = tmp.measuredAt.date();
        QList<VitalRecord> recs;
        for (const VitalRecord &r : std::as_const(m_recent)) if (r.id != tmp.id) recs << r;
        recs << tmp;
        const Fluid::Day d = Fluid::perDay(recs, day, day).value(day);
        QString t = day == QDate::currentDate() ? tr("Heute") : QLocale().toString(day, QStringLiteral("dd.MM."));
        t += tr(": Einfuhr %1 ml, Ausfuhr %2 ml, Bilanz %3").arg(QLocale().toString(d.in), QLocale().toString(d.out), Fluid::signedMl(d.balance()));
        const QString lim = Fluid::limitsText(m_patient);
        if (!lim.isEmpty()) t += QStringLiteral("\n") + lim;
        const int hi = Fluid::maxIntake(m_patient);
        l->setStyleSheet(hi && d.in > hi ? QStringLiteral("font-size: 8.5pt; color: %1;").arg(Inr::zoneColor(Inr::Zone::High, dark).name())
                                         : QStringLiteral("font-size: 8.5pt;"));
        l->setText(t);
    }
    if (auto *l = m_form->findChild<QLabel *>(QStringLiteral("edemaInfoLabel"))) {
        const int g = tmp.edema.value_or(-1);
        for (const char *n : { "edemaSiteCombo", "edemaSideCombo", "edemaSiteLabel" })
            if (auto *w = m_form->findChild<QWidget *>(QLatin1String(n))) w->setEnabled(g > 0);
        QString t = g >= 0 ? Fluid::edemaDescription(g) : QString();
        const QString adv = Fluid::edemaAdvice(tmp);
        l->setStyleSheet(adv.isEmpty() ? QStringLiteral("font-size: 8.5pt;")
                                       : QStringLiteral("font-size: 8.5pt; color: %1;").arg(Inr::zoneColor(Inr::Zone::High, dark).name()));
        l->setText(adv.isEmpty() ? t : t + QStringLiteral("\n") + adv);
        l->setVisible(!l->text().isEmpty());
    }
}

// Nach dem Speichern: INR außerhalb des Zielbereichs, auffällige Ödeme, Gewichtszunahme, Trinkmenge
void VitalDialog::warnCareValues(const VitalRecord &r)
{
    if (!isVisible()) return;
    QStringList items;
    bool critical = false;
    if (r.inr) {
        const Inr::Assessment a = Inr::assess(m_patient, *r.inr);
        if (a.zone == Inr::Zone::Low || a.zone == Inr::Zone::High || a.zone == Inr::Zone::VeryHigh) {
            items << QStringLiteral("<b>%1</b><br>%2").arg(a.text.toHtmlEscaped(), a.advice.toHtmlEscaped());
            critical |= a.zone == Inr::Zone::VeryHigh;
        }
    }
    if (r.edema && *r.edema > 0) {
        std::optional<int> prev;
        QDateTime prevAt;
        for (const VitalRecord &x : std::as_const(m_recent))
            if (x.edema && x.id != r.id && x.measuredAt < r.measuredAt && (!prevAt.isValid() || x.measuredAt > prevAt)) { prev = x.edema; prevAt = x.measuredAt; }
        if (const QString adv = Fluid::edemaAdvice(r, prev); !adv.isEmpty())
            items << QStringLiteral("<b>%1</b><br>%2").arg(tr("Ödeme: %1").arg(Fluid::edemaText(r)).toHtmlEscaped(),
                                                          adv.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }
    QList<VitalRecord> recs;
    for (const VitalRecord &x : std::as_const(m_recent)) if (x.id != r.id) recs << x;
    recs << r;
    if (r.weight)
        if (const Fluid::Notice n = Fluid::weightNotice(m_patient, recs, r.measuredAt.date()); n.warn)
            items << QStringLiteral("<b>%1</b><br>%2").arg(tr("Gewichtszunahme").toHtmlEscaped(), n.text.toHtmlEscaped());
    if (r.fluidIn && Fluid::maxIntake(m_patient) && r.measuredAt.date() == QDate::currentDate())
        if (const Fluid::Notice n = Fluid::intakeNotice(m_patient, recs, r.measuredAt.date()); n.warn)
            items << QStringLiteral("<b>%1</b><br>%2").arg(tr("Trinkmenge").toHtmlEscaped(), n.text.toHtmlEscaped());
    if (items.isEmpty()) return;
    QMessageBox box(critical ? QMessageBox::Critical : QMessageBox::Warning, tr("Bitte beachten"),
                    items.join(QStringLiteral("<br><br>")) + QStringLiteral("<br><br><small>%1</small>")
                        .arg(tr("VitaParms ist kein Medizinprodukt – maßgeblich sind die Anweisungen Ihrer Ärztin oder Ihres Arztes.").toHtmlEscaped()),
                    QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("careWarning"));
    box.exec();
}

// schwarzer oder blutiger Stuhl: nach dem Speichern auf ärztliche Abklärung hinweisen
void VitalDialog::warnStool(const VitalRecord &r)
{
    if (!r.stool || !isVisible() || !Stool::needsAttention(r.stoolDetail)) return;
    QMessageBox box(QMessageBox::Warning, tr("Stuhlgang"), Stool::attentionText(), QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("stoolWarning"));
    box.exec();
}

// gelber oder roter Bereich: nach dem Speichern deutlich darauf hinweisen
void VitalDialog::warnPeakFlow(const VitalRecord &r)
{
    if (!r.peakFlow || !isVisible()) return;
    const PeakFlow::Assessment a = PeakFlow::assess(m_patient, *r.peakFlow, r.measuredAt.date(),
                                                    r.height ? r.height : m_fallbackHeight);
    if (a.zone != PeakFlow::Zone::Yellow && a.zone != PeakFlow::Zone::Red) return;
    QMessageBox box(a.zone == PeakFlow::Zone::Red ? QMessageBox::Critical : QMessageBox::Warning,
                    tr("Peak Flow – %1").arg(PeakFlow::zoneName(a.zone)),
                    QStringLiteral("<b>%1</b><br><br>%2<br><br><small>%3</small>")
                        .arg(a.text.toHtmlEscaped(), a.advice.toHtmlEscaped(),
                             tr("Bezug: %1. VitaParms ist kein Medizinprodukt – maßgeblich ist Ihr ärztlicher Asthma-Aktionsplan.")
                                 .arg(a.ref.description).toHtmlEscaped()),
                    QMessageBox::Ok, this);
    box.setObjectName(QStringLiteral("peakFlowWarning"));
    box.exec();
}

void VitalDialog::accept()
{
    if (!store())
        return;
    AppSettings::setWindowState(QStringLiteral("vitaldialog2"), saveGeometry());
    QDialog::accept();
}

void VitalDialog::saveAndNext()
{
    if (!store())
        return;
    // Neue, leere Messung – Kontext und Uhrzeit vorbelegen
    VitalRecord next;
    next.patientId = m_patient.id;
    next.measuredAt = QDateTime(QDate::currentDate(),
                                QTime(QTime::currentTime().hour(), QTime::currentTime().minute()));
    if (next.measuredAt <= m_record.measuredAt)
        next.measuredAt = m_record.measuredAt.addSecs(60);
    // Ab sofort ist der Dialog für einen neuen Datensatz – Zusatzfelder leeren
    m_record = next;
    loadRecord(m_record);
    setWindowTitle(tr("Neue Messung (%1 gespeichert)").arg(m_savedCount));
}

// ---------------------------------------------------------------- Atemzüge zählen
// Funktioniert mit jedem (auch angepassten) Formular: gesucht wird das Feld mit
// vpField "Atemfrequenz" bzw. respRateSpin; der Knopf wird rechts daneben eingefügt.
static QLayout *layoutContaining(QLayout *l, QWidget *w)
{
    if (!l) return nullptr;
    if (l->indexOf(w) >= 0) return l;
    for (int i = 0; i < l->count(); ++i)
        if (QLayout *sub = l->itemAt(i)->layout())
            if (QLayout *found = layoutContaining(sub, w)) return found;
    return nullptr;
}

// VitaParms light: Schmerzstärke mit elf Tasten 0–10 statt Drehfeld (große Ziele, Farbe von grün nach rot).
// Das Drehfeld bleibt verborgen als Datenfeld erhalten; nochmaliges Klicken auf die gewählte Zahl leert den Wert.
void VitalDialog::setupPainScale()
{
    if (!Edition::light()) return;
    auto *spin = m_form->findChild<QSpinBox *>(QStringLiteral("painSpin"));
    auto *form = spin && spin->parentWidget() ? qobject_cast<QFormLayout *>(spin->parentWidget()->layout()) : nullptr;
    if (!form) return;
    int row = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(spin, &row, &role);
    if (row < 0) return;
    auto *box = new QWidget(spin->parentWidget());
    box->setObjectName(QStringLiteral("painScale"));
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *h = new QHBoxLayout;
    h->setSpacing(3);
    QList<QPushButton *> buttons;
    for (int i = 0; i <= 10; ++i) {
        auto *b = new QPushButton(QString::number(i), box);
        b->setObjectName(QStringLiteral("pain%1").arg(i));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::TabFocus);
        b->setFixedSize(30, 28);
        const QColor c = QColor::fromHslF((1.0 - i / 10.0) * 0.33, 0.62, 0.40);
        b->setStyleSheet(QStringLiteral("QPushButton:checked { background: %1; color: white; font-weight: bold;"
                                        " border: 1px solid %1; border-radius: 3px; }").arg(c.name()));
        h->addWidget(b);
        buttons << b;
    }
    h->addStretch();
    v->addLayout(h);
    auto *hint = new QLabel(box);
    hint->setObjectName(QStringLiteral("painHint"));
    hint->setStyleSheet(QStringLiteral("font-size: 8.5pt;"));
    v->addWidget(hint);
    form->removeWidget(spin);
    spin->setParent(box);
    spin->hide();
    form->setWidget(row, QFormLayout::FieldRole, box);
    if (auto *lbl = qobject_cast<QLabel *>(form->labelForField(box))) lbl->setBuddy(buttons.first());
    auto sync = [spin, buttons, hint] {
        const int val = spin->value();
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setChecked(i == val);
        hint->setText(val < 0 ? tr("nicht erfasst – Zahl anklicken (0 = kein Schmerz, 10 = stärkster vorstellbarer Schmerz)")
                      : val == 0 ? tr("kein Schmerz")
                      : val <= 3 ? tr("leichte Schmerzen")
                      : val <= 6 ? tr("mäßige Schmerzen")
                      : tr("starke Schmerzen"));
    };
    for (int i = 0; i < buttons.size(); ++i)
        connect(buttons[i], &QPushButton::clicked, this, [spin, i] { spin->setValue(spin->value() == i ? spin->minimum() : i); });
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, sync);
    sync();
}

void VitalDialog::setupBreathCounter()
{
    const auto spins = m_form->findChildren<QSpinBox *>();
    for (QSpinBox *s : spins)
        if (s->property("vpField").toString() == QLatin1String("Atemfrequenz")) { m_respSpin = s; break; }
    if (!m_respSpin) m_respSpin = m_form->findChild<QSpinBox *>(QStringLiteral("respRateSpin"));
    if (!m_respSpin || !m_respSpin->parentWidget()) return;
    QLayout *lay = layoutContaining(m_respSpin->parentWidget()->layout(), m_respSpin);
    if (!lay) return;

    auto *box = new QWidget(m_respSpin->parentWidget());
    auto *h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    delete lay->replaceWidget(m_respSpin, box);
    h->addWidget(m_respSpin, 1);
    m_countButton = new QPushButton(box);
    m_countButton->setObjectName(QStringLiteral("breathCountButton"));
    m_countButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_countButton->setText(tr("&1 Minute zählen"));
    m_countButton->setToolTip(tr("Startet eine Uhr über eine Minute: Ein Ton gibt das Startsignal, "
                                 "ein Doppelton das Ende. Zählen Sie dazwischen die Atemzüge "
                                 "(Heben des Brustkorbs) und tragen Sie die Zahl ein."));
    m_countButton->setAutoDefault(false);
    {   // feste Breite, damit das Feld beim Herunterzählen nicht springt
        const QString idle = m_countButton->text();
        m_countButton->setText(tr("Zählen … %1 s – Abbrechen").arg(60));
        const int busyW = m_countButton->sizeHint().width();
        m_countButton->setText(idle);
        m_countButton->setMinimumWidth(qMax(busyW, m_countButton->sizeHint().width()));
    }
    h->addWidget(m_countButton);
    m_countTimer = new QTimer(this);
    m_countTimer->setInterval(200);
    connect(m_countTimer, &QTimer::timeout, this, &VitalDialog::updateBreathCount);
    connect(m_countButton, &QPushButton::clicked, this, [this] {
        if (m_countTimer->isActive()) stopBreathCount(false);
        else startBreathCount();
    });
    Sounds::preload(Sounds::Kind::CountStart);
    Sounds::preload(Sounds::Kind::CountStop);
}

void VitalDialog::startBreathCount()
{
    m_countClock.start();
    m_countTimer->start();
    m_countButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    Sounds::play(Sounds::Kind::CountStart);
    updateBreathCount();
}

void VitalDialog::updateBreathCount()
{
    const qint64 left = 60000 - m_countClock.elapsed();
    if (left <= 0) { stopBreathCount(true); return; }
    const int sec = int((left + 999) / 1000);
    m_countButton->setText(tr("Zählen … %1 s – Abbrechen").arg(sec));
}

void VitalDialog::stopBreathCount(bool finished)
{
    m_countTimer->stop();
    m_countButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_countButton->setText(tr("&1 Minute zählen"));
    if (!finished) return;
    Sounds::play(Sounds::Kind::CountStop);
    m_respSpin->setFocus(Qt::OtherFocusReason);
    m_respSpin->selectAll();
}
