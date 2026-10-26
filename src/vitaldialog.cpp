// SPDX-License-Identifier: MIT
#include "vitaldialog.h"
#include "appsettings.h"
#include "bmi.h"
#include "database.h"
#include "formloader.h"
#include "peakflow.h"
#include "vitaldefs.h"

#include <QAbstractSpinBox>
#include <cmath>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QTextDocument>
#include <QLabel>
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

VitalDialog::VitalDialog(const Patient &patient, const VitalRecord &record, Mode mode,
                         QWidget *parent)
    : QDialog(parent), m_patient(patient), m_record(record), m_mode(mode)
{
    setWindowTitle(mode == Mode::Admission ? tr("Vitalwerte bei Aufnahme")
                   : record.id ? tr("Messung bearbeiten") : tr("Neue Messung"));

    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("vitalform"), this);
    lay->addWidget(m_form);

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
    setupPeakFlow();

    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("nowButton"))) {
        connect(b, &QPushButton::clicked, this, [this] {
            FormIO::write(m_form, QStringLiteral("dateTimeEdit"),
                          QDateTime::currentDateTime().addSecs(-QTime::currentTime().second()));
        });
    }

    m_fallbackHeight = Database::instance().latestHeight(patient.id);

    for (const QString &n : { QStringLiteral("weightSpin"), QStringLiteral("heightSpin") }) {
        if (auto *s = m_form->findChild<QDoubleSpinBox *>(n))
            connect(s, &QDoubleSpinBox::valueChanged, this, &VitalDialog::updateBmi);
        else if (auto *s2 = m_form->findChild<QSpinBox *>(n))
            connect(s2, &QSpinBox::valueChanged, this, &VitalDialog::updateBmi);
    }
    if (m_peakFlowInfo) {
        if (auto *s = m_form->findChild<QSpinBox *>(QStringLiteral("peakFlowSpin")))
            connect(s, &QSpinBox::valueChanged, this, &VitalDialog::updatePeakFlow);
        if (auto *s = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("heightSpin")))
            connect(s, &QDoubleSpinBox::valueChanged, this, &VitalDialog::updatePeakFlow);
        else if (auto *s2 = m_form->findChild<QSpinBox *>(QStringLiteral("heightSpin")))
            connect(s2, &QSpinBox::valueChanged, this, &VitalDialog::updatePeakFlow);
        if (auto *d = m_form->findChild<QDateTimeEdit *>(QStringLiteral("dateTimeEdit")))
            connect(d, &QDateTimeEdit::dateTimeChanged, this, &VitalDialog::updatePeakFlow);
    }

    if (!m_record.measuredAt.isValid())
        m_record.measuredAt = QDateTime(QDate::currentDate(),
                                        QTime(QTime::currentTime().hour(), QTime::currentTime().minute()));
    if (mode == Mode::Admission)
        m_record.isAdmission = true;
    loadRecord(m_record);

    const QByteArray geo = AppSettings::windowState(QStringLiteral("vitaldialog"));
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(560, 780);
    setMinimumWidth(500);
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
    FormIO::applyExtra(m_form, r.extra);
    updateBmi();
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
    r.extra        = FormIO::collectExtra(m_form, r.extra);
    // Ältere angepasste Formulare mit eigenem Feld "Peak-Flow": Wert in den festen Messwert übernehmen
    static const QRegularExpression peakKey(QStringLiteral("^\\s*(peak[- ]?flow|pef)\\s*$"), QRegularExpression::CaseInsensitiveOption);
    for (auto it = r.extra.begin(); it != r.extra.end();) {
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
    return true;
}

void VitalDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    if (m_peakFlowInfo) QTimer::singleShot(0, this, &VitalDialog::updatePeakFlow);
}

void VitalDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    if (m_peakFlowInfo) QTimer::singleShot(0, this, &VitalDialog::updatePeakFlow);
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
    {   // Höhe zur tatsächlichen Breite (sonst schneidet das Formular mehrzeilige Hinweise ab)
        const int w = m_peakFlowInfo->contentsRect().width();
        if (w > 100) {
            QTextDocument doc;
            doc.setDefaultFont(m_peakFlowInfo->font());
            doc.setDocumentMargin(0);
            doc.setHtml(m_peakFlowInfo->text());
            doc.setTextWidth(w);
            // feste Höhe: QLabel schätzt die Höhe von Rich Text im Formular zu groß
            m_peakFlowInfo->setFixedHeight(int(std::ceil(doc.size().height())) + 4);
        }
    }
    m_peakFlowInfo->updateGeometry();
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
    AppSettings::setWindowState(QStringLiteral("vitaldialog"), saveGeometry());
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
