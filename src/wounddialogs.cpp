// SPDX-License-Identifier: MIT
#include "wounddialogs.h"
#include "database.h"
#include "formloader.h"
#include "reportprinter.h"
#include "session.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

using namespace Wounds;

namespace {

QString dmy(const QDate &d) { return d.isValid() ? QLocale().toString(d, QStringLiteral("dd.MM.yyyy")) : QString(); }

QComboBox *combo(const QStringList &items, QWidget *parent, bool editable = false, const QString &none = QString())
{
    auto *c = new QComboBox(parent);
    if (!none.isNull()) c->addItem(none);
    c->addItems(items);
    c->setEditable(editable);
    if (editable) c->setInsertPolicy(QComboBox::NoInsert);
    return c;
}

// Kombinationsfeld mit erster Zeile „–“: Index -1 ⇔ Zeile 0
void setIndexOpt(QComboBox *c, int v) { c->setCurrentIndex(v < 0 ? 0 : v + 1); }
int indexOpt(const QComboBox *c) { return c->currentIndex() - 1; }

void setText(QComboBox *c, const QString &t)
{
    const int i = c->findText(t);
    if (i >= 0) c->setCurrentIndex(i);
    else if (c->isEditable()) c->setEditText(t);
}

QDoubleSpinBox *sizeSpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(-0.1, 99.9);
    s->setDecimals(1);
    s->setSingleStep(0.1);
    s->setSpecialValueText(QStringLiteral("–"));
    s->setSuffix(QStringLiteral(" cm"));
    s->setValue(-0.1);
    return s;
}
void setSize(QDoubleSpinBox *s, const std::optional<double> &v) { s->setValue(v ? *v : -0.1); }
std::optional<double> spinSize(const QDoubleSpinBox *s)
{
    if (s->value() < 0) return std::nullopt;
    return s->value();
}

QSpinBox *optSpin(int max, QWidget *parent, const QString &suffix = QString())
{
    auto *s = new QSpinBox(parent);
    s->setRange(-1, max);
    s->setSpecialValueText(QStringLiteral("–"));
    s->setSuffix(suffix);
    s->setValue(-1);
    return s;
}
void setOpt(QSpinBox *s, const std::optional<int> &v) { s->setValue(v ? *v : -1); }
std::optional<int> opt(const QSpinBox *s)
{
    if (s->value() < 0) return std::nullopt;
    return s->value();
}

QSpinBox *percent(QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(0, 100);
    s->setSingleStep(5);
    s->setSuffix(QStringLiteral(" %"));
    return s;
}

QDateEdit *dateEdit(QWidget *parent)
{
    auto *d = new QDateEdit(parent);
    d->setCalendarPopup(true);
    d->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    d->setDate(QDate::currentDate());
    d->setMinimumWidth(d->fontMetrics().horizontalAdvance(QStringLiteral("00.00.00000")) + 40);
    return d;
}

QPlainTextEdit *textBox(QWidget *parent, int lines = 3)
{
    auto *t = new QPlainTextEdit(parent);
    t->setTabChangesFocus(true);
    t->setFixedHeight(t->fontMetrics().lineSpacing() * lines + 14);
    return t;
}

QPixmap photoPixmap(const QByteArray &jpeg, int maxSide)
{
    QImage img;
    if (jpeg.isEmpty() || !img.loadFromData(jpeg)) return {};
    return QPixmap::fromImage(img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace

// ============================================================ ChoiceGroup

ChoiceGroup::ChoiceGroup(const QStringList &choices, int columns, QWidget *parent) : QWidget(parent)
{
    auto *g = new QGridLayout(this);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(2);
    for (int i = 0; i < choices.size(); ++i) {
        auto *b = new QCheckBox(choices.at(i), this);
        m_boxes << b;
        g->addWidget(b, i / columns, i % columns);
    }
}

QStringList ChoiceGroup::values() const
{
    QStringList l;
    for (QCheckBox *b : m_boxes)
        if (b->isChecked()) l << b->text().remove(QLatin1Char('&'));
    return l + m_extra;
}

void ChoiceGroup::setValues(const QStringList &values)
{
    m_extra = values;
    for (QCheckBox *b : m_boxes) {
        const QString t = b->text().remove(QLatin1Char('&'));
        b->setChecked(values.contains(t));
        m_extra.removeAll(t);
    }
}

// ============================================================ WoundDialog

WoundDialog::WoundDialog(const Wound &w, QWidget *parent) : QDialog(parent), m_wound(w)
{
    setObjectName(QStringLiteral("WoundDialog"));
    setWindowTitle(w.id ? tr("Wunde bearbeiten") : tr("Wunde hinzufügen"));
    auto *form = new QWidget;
    auto *outer = new QVBoxLayout(form);

    // --- Wunde
    auto *gWound = new QGroupBox(tr("Wunde"), form);
    auto *f1 = new QFormLayout(gWound);
    m_label = new QLineEdit(gWound);
    m_label->setObjectName(QStringLiteral("woundLabel"));
    m_label->setPlaceholderText(tr("z. B. Wunde %1").arg(w.id ? 1 : nextNumber(w.patientId)));
    f1->addRow(tr("&Bezeichnung:"), m_label);
    m_kind = combo(kindNames(), gWound);
    m_kind->setObjectName(QStringLiteral("woundKind"));
    f1->addRow(tr("Wund&art:"), m_kind);
    m_grade = new QComboBox(gWound);
    m_grade->setObjectName(QStringLiteral("woundGrade"));
    m_gradeLabel = new QLabel(gWound);
    f1->addRow(m_gradeLabel, m_grade);
    m_diagnosis = new QLineEdit(gWound);
    m_diagnosis->setPlaceholderText(tr("medizinische Diagnose, Grunderkrankung (z. B. Diabetes mellitus, pAVK)"));
    f1->addRow(tr("&Diagnose:"), m_diagnosis);
    m_site = combo(sites(), gWound, true);
    m_site->setObjectName(QStringLiteral("woundSite"));
    m_side = combo(sides(), gWound, false, QStringLiteral("–"));
    auto *siteRow = new QHBoxLayout;
    siteRow->addWidget(m_site, 1);
    siteRow->addWidget(m_side);
    f1->addRow(tr("Lokalisation:"), siteRow);
    m_siteDetail = new QLineEdit(gWound);
    m_siteDetail->setPlaceholderText(tr("genauer, z. B. 2 cm oberhalb des Innenknöchels"));
    f1->addRow(QString(), m_siteDetail);
    outer->addWidget(gWound);

    // --- Entstehung
    auto *gOrigin = new QGroupBox(tr("Entstehung und Verlauf"), form);
    auto *f2 = new QFormLayout(gOrigin);
    m_sinceKnown = new QCheckBox(tr("bekannt:"), gOrigin);
    m_since = dateEdit(gOrigin);
    auto *sinceRow = new QHBoxLayout;
    sinceRow->addWidget(m_sinceKnown);
    sinceRow->addWidget(m_since);
    sinceRow->addStretch(1);
    f2->addRow(tr("Entstanden am:"), sinceRow);
    m_origin = combo(origins(), gOrigin, true, QString(""));
    f2->addRow(tr("Entstanden &in/bei:"), m_origin);
    m_onAdmission = new QCheckBox(tr("bei Aufnahme bereits vorhanden, Aufnahme am"), gOrigin);
    m_onAdmission->setObjectName(QStringLiteral("woundOnAdmission"));
    m_admission = dateEdit(gOrigin);
    auto *admRow = new QHBoxLayout;
    admRow->addWidget(m_onAdmission);
    admRow->addWidget(m_admission);
    admRow->addStretch(1);
    f2->addRow(tr("Aufnahme:"), admRow);
    m_recurrences = new QSpinBox(gOrigin);
    m_recurrences->setRange(0, 99);
    m_recFree = new QLineEdit(gOrigin);
    m_recFree->setPlaceholderText(tr("rezidivfreie Zeit"));
    auto *recRow = new QHBoxLayout;
    recRow->addWidget(m_recurrences);
    recRow->addWidget(m_recFree, 1);
    f2->addRow(tr("Rezidive:"), recRow);
    m_previous = textBox(gOrigin);
    m_previous->setPlaceholderText(tr("bisherige Diagnostik und Therapie, Kompression, Druckentlastung …"));
    f2->addRow(tr("&Bisherige Maßnahmen:"), m_previous);
    m_physician = new QLineEdit(gOrigin);
    m_physician->setPlaceholderText(tr("Ärztin/Arzt, Wundexpertin/Wundexperte, Wundambulanz"));
    f2->addRow(tr("&Behandelt von:"), m_physician);
    outer->addWidget(gOrigin);

    // --- Patientenbezogen
    auto *gPerson = new QGroupBox(tr("Einschränkungen, Selbstmanagement, Ziel"), form);
    auto *f3 = new QFormLayout(gPerson);
    m_limits = new ChoiceGroup(limitationChoices(), 2, gPerson);
    f3->addRow(tr("Einschränkungen:"), m_limits);
    m_self = textBox(gPerson);
    m_self->setPlaceholderText(tr("Wissen und Fähigkeiten zum Umgang mit der Wunde, Unterstützung durch Angehörige …"));
    f3->addRow(tr("&Selbstmanagement:"), m_self);
    m_goal = combo(goals(), gPerson);
    f3->addRow(tr("Therapie&ziel:"), m_goal);
    m_consent = new QCheckBox(tr("Einwilligung zur Fotodokumentation liegt vor"), gPerson);
    m_consent->setObjectName(QStringLiteral("woundConsent"));
    f3->addRow(tr("Fotos:"), m_consent);
    outer->addWidget(gPerson);

    // --- Status
    auto *gState = new QGroupBox(tr("Status"), form);
    auto *f4 = new QFormLayout(gState);
    m_status = combo(statusNames(), gState);
    m_closed = dateEdit(gState);
    auto *stRow = new QHBoxLayout;
    stRow->addWidget(m_status);
    stRow->addWidget(new QLabel(tr("am"), gState));
    stRow->addWidget(m_closed);
    stRow->addStretch(1);
    f4->addRow(tr("Status:"), stRow);
    m_notes = textBox(gState, 2);
    f4->addRow(tr("Be&merkung:"), m_notes);
    outer->addWidget(gState);
    outer->addStretch(1);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(FormLoader::scrollable(form, this), 1);
    auto *hint = new QLabel(tr("Nach dem Expertenstandard „Pflege von Menschen mit chronischen Wunden“. "
                               "VitaParms ist kein Medizinprodukt – Einschätzung und Therapie gehören in fachliche Hände."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    lay->addWidget(hint);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Save)->setText(tr("Speichern"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    // Werte
    m_label->setText(w.label);
    m_kind->setCurrentIndex(qBound(0, w.kind, int(KindCount) - 1));
    updateGrades();
    m_grade->setCurrentIndex(w.grade < 0 ? 0 : qMin(w.grade + 1, m_grade->count() - 1));
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateGrades(); });
    m_diagnosis->setText(w.diagnosis);
    setText(m_site, w.site);
    if (w.site.isEmpty()) m_site->setEditText(QString());
    const int si = m_side->findText(w.side);
    m_side->setCurrentIndex(si < 0 ? 0 : si);
    m_siteDetail->setText(w.siteDetail);
    m_sinceKnown->setChecked(w.since.isValid());
    if (w.since.isValid()) m_since->setDate(w.since);
    m_since->setEnabled(w.since.isValid());
    connect(m_sinceKnown, &QCheckBox::toggled, m_since, &QWidget::setEnabled);
    setText(m_origin, w.origin);
    m_onAdmission->setChecked(w.onAdmission);
    if (w.admissionDate.isValid()) m_admission->setDate(w.admissionDate);
    m_admission->setEnabled(w.onAdmission);
    connect(m_onAdmission, &QCheckBox::toggled, m_admission, &QWidget::setEnabled);
    m_recurrences->setValue(w.recurrences);
    m_recFree->setText(w.recurrenceFree);
    m_previous->setPlainText(w.previousCare);
    m_physician->setText(w.physician);
    m_limits->setValues(w.limitations);
    m_self->setPlainText(w.selfManagement);
    m_goal->setCurrentIndex(qBound(0, w.goal, m_goal->count() - 1));
    m_consent->setChecked(w.photoConsent);
    m_status->setCurrentIndex(qBound(0, w.status, m_status->count() - 1));
    if (w.closedAt.isValid()) m_closed->setDate(w.closedAt);
    m_closed->setEnabled(w.status != Open);
    connect(m_status, &QComboBox::currentIndexChanged, this, [this](int i) { m_closed->setEnabled(i != Open); });
    m_notes->setPlainText(w.notes);
    resize(720, 760);
}

void WoundDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    FormLoader::fitToScreen(this);
}

void WoundDialog::updateGrades()
{
    const int kind = m_kind->currentIndex();
    const QStringList g = gradeNames(kind);
    const int old = m_grade->currentIndex();
    m_grade->clear();
    m_grade->addItem(QStringLiteral("–"));
    m_grade->addItems(g);
    m_grade->setCurrentIndex(old > 0 && old < m_grade->count() ? old : 0);
    m_grade->setEnabled(!g.isEmpty());
    const QString sys = gradeSystem(kind);
    m_gradeLabel->setText(sys.isEmpty() ? tr("Klassifikation:") : sys + QLatin1Char(':'));
}

Wound WoundDialog::wound() const
{
    Wound w = m_wound;
    w.label = m_label->text().trimmed();
    w.kind = m_kind->currentIndex();
    w.grade = m_grade->isEnabled() ? m_grade->currentIndex() - 1 : -1;
    w.diagnosis = m_diagnosis->text().trimmed();
    w.site = m_site->currentText().trimmed();
    w.side = m_side->currentIndex() > 0 ? m_side->currentText() : QString();
    w.siteDetail = m_siteDetail->text().trimmed();
    w.since = m_sinceKnown->isChecked() ? m_since->date() : QDate();
    w.origin = m_origin->currentText().trimmed();
    w.onAdmission = m_onAdmission->isChecked();
    w.admissionDate = w.onAdmission ? m_admission->date() : QDate();
    w.recurrences = m_recurrences->value();
    w.recurrenceFree = m_recFree->text().trimmed();
    w.previousCare = m_previous->toPlainText().trimmed();
    w.physician = m_physician->text().trimmed();
    w.limitations = m_limits->values();
    w.selfManagement = m_self->toPlainText().trimmed();
    w.goal = m_goal->currentIndex();
    w.photoConsent = m_consent->isChecked();
    w.status = m_status->currentIndex();
    w.closedAt = w.status != Open ? m_closed->date() : QDate();
    w.notes = m_notes->toPlainText().trimmed();
    return w;
}

void WoundDialog::accept()
{
    Wound w = wound();
    if (w.site.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte die Lokalisation der Wunde angeben."));
        m_site->setFocus();
        return;
    }
    QString err;
    if (!saveWound(w, &err)) {
        QMessageBox::warning(this, windowTitle(), tr("Die Wunde konnte nicht gespeichert werden:\n%1").arg(err));
        return;
    }
    m_wound = w;
    QDialog::accept();
}

// ============================================================ WoundEntryDialog

WoundEntryDialog::WoundEntryDialog(const Wound &w, const Entry &e, QWidget *parent)
    : QDialog(parent), m_wound(w), m_entry(e)
{
    setObjectName(QStringLiteral("WoundEntryDialog"));
    setWindowTitle((e.id ? tr("Eintrag bearbeiten – %1") : tr("Neuer Eintrag – %1")).arg(w.title()));
    auto *form = new QWidget;
    auto *outer = new QVBoxLayout(form);

    // --- Kopf
    auto *head = new QFormLayout;
    m_kind = combo({ entryKindName(Assessment), entryKindName(DressingChange) }, form);
    m_kind->setObjectName(QStringLiteral("entryKind"));
    head->addRow(tr("&Art:"), m_kind);
    m_at = new QDateTimeEdit(form);
    m_at->setCalendarPopup(true);
    m_at->setDisplayFormat(QStringLiteral("dd.MM.yyyy HH:mm"));
    head->addRow(tr("&Zeitpunkt:"), m_at);
    outer->addLayout(head);

    // --- Größe und Wundgrund (nur Beurteilung)
    auto *gSize = new QGroupBox(tr("Größe"), form);
    auto *f1 = new QFormLayout(gSize);
    m_len = sizeSpin(gSize);
    m_len->setObjectName(QStringLiteral("entryLength"));
    m_wid = sizeSpin(gSize);
    m_wid->setObjectName(QStringLiteral("entryWidth"));
    m_dep = sizeSpin(gSize);
    m_area = new QLabel(gSize);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Länge"), gSize));
    sizeRow->addWidget(m_len);
    sizeRow->addWidget(new QLabel(tr("Breite"), gSize));
    sizeRow->addWidget(m_wid);
    sizeRow->addWidget(new QLabel(tr("Tiefe"), gSize));
    sizeRow->addWidget(m_dep);
    sizeRow->addWidget(m_area, 1);
    f1->addRow(sizeRow);
    m_pockets = new QLineEdit(gSize);
    m_pockets->setPlaceholderText(tr("Uhrmethode (Kopf = 12 Uhr), z. B. Tasche 1,5 cm bei 3 Uhr"));
    f1->addRow(tr("&Taschen/Unterminierung:"), m_pockets);
    outer->addWidget(gSize);
    m_assessOnly << gSize;

    auto *gBed = new QGroupBox(tr("Wundgrund und Wundzustand"), form);
    auto *f2 = new QFormLayout(gBed);
    m_gran = percent(gBed);
    m_gran->setObjectName(QStringLiteral("entryGranulation"));
    m_epi = percent(gBed);
    m_fib = percent(gBed);
    m_nec = percent(gBed);
    m_groundSum = new QLabel(gBed);
    auto *bedRow = new QGridLayout;
    bedRow->addWidget(new QLabel(tr("Granulation"), gBed), 0, 0);
    bedRow->addWidget(m_gran, 0, 1);
    bedRow->addWidget(new QLabel(tr("Epithel"), gBed), 0, 2);
    bedRow->addWidget(m_epi, 0, 3);
    bedRow->addWidget(new QLabel(tr("Fibrin/Beläge"), gBed), 1, 0);
    bedRow->addWidget(m_fib, 1, 1);
    bedRow->addWidget(new QLabel(tr("Nekrose"), gBed), 1, 2);
    bedRow->addWidget(m_nec, 1, 3);
    bedRow->addWidget(m_groundSum, 0, 4, 2, 1);
    bedRow->setColumnStretch(4, 1);
    f2->addRow(tr("Wundgrund:"), bedRow);
    m_exposed = new ChoiceGroup(exposedChoices(), 4, gBed);
    f2->addRow(tr("Freiliegend:"), m_exposed);
    m_exudate = combo(exudateAmounts(), gBed, false, QStringLiteral("–"));
    m_exudateKind = combo(exudateKinds(), gBed, true, QString(""));
    auto *exRow = new QHBoxLayout;
    exRow->addWidget(m_exudate);
    exRow->addWidget(m_exudateKind, 1);
    f2->addRow(tr("Exsudat:"), exRow);
    m_odor = combo(odors(), gBed, false, QStringLiteral("–"));
    f2->addRow(tr("&Geruch:"), m_odor);
    m_edge = new ChoiceGroup(edgeChoices(), 3, gBed);
    f2->addRow(tr("Wundrand:"), m_edge);
    m_surround = new ChoiceGroup(surroundingChoices(), 3, gBed);
    f2->addRow(tr("Wundumgebung:"), m_surround);
    m_infection = new ChoiceGroup(infectionChoices(), 3, gBed);
    f2->addRow(tr("Infektionszeichen:"), m_infection);
    m_pain = optSpin(10, gBed, QStringLiteral(" / 10"));
    m_painChange = optSpin(10, gBed, QStringLiteral(" / 10"));
    auto *painRow = new QHBoxLayout;
    painRow->addWidget(new QLabel(tr("in Ruhe"), gBed));
    painRow->addWidget(m_pain);
    painRow->addWidget(new QLabel(tr("beim Verbandwechsel"), gBed));
    painRow->addWidget(m_painChange);
    painRow->addStretch(1);
    f2->addRow(tr("Schmerz (NRS):"), painRow);
    const QStringList g = gradeNames(w.kind);
    m_grade = combo(g, gBed, false, QStringLiteral("–"));
    m_grade->setEnabled(!g.isEmpty());
    f2->addRow((gradeSystem(w.kind).isEmpty() ? tr("Klassifikation") : gradeSystem(w.kind)) + QLatin1Char(':'), m_grade);
    m_phase = combo(phases(), gBed, false, QStringLiteral("–"));
    f2->addRow(tr("Heilungs&phase:"), m_phase);
    outer->addWidget(gBed);
    m_assessOnly << gBed;

    // --- Verlauf und Versorgung (immer)
    auto *gCare = new QGroupBox(tr("Verlauf und Versorgung"), form);
    auto *f3 = new QFormLayout(gCare);
    m_description = new QLineEdit(gCare);
    m_description->setObjectName(QStringLiteral("entryDescription"));
    m_description->setPlaceholderText(tr("kurze Beschreibung, z. B. Wunde sauber, Rand rosig"));
    f3->addRow(tr("&Beschreibung:"), m_description);
    m_trend = combo(trends(), gCare, false, QStringLiteral("–"));
    m_trend->setObjectName(QStringLiteral("entryTrend"));
    f3->addRow(tr("T&endenz:"), m_trend);
    m_cleansing = new QLineEdit(gCare);
    m_cleansing->setPlaceholderText(tr("z. B. Wundspüllösung, Débridement"));
    f3->addRow(tr("Wund&reinigung:"), m_cleansing);
    m_dressing = new QLineEdit(gCare);
    m_dressing->setObjectName(QStringLiteral("entryDressing"));
    m_dressing->setPlaceholderText(tr("Wundauflage(n), Produkt"));
    f3->addRow(tr("&Wundauflage:"), m_dressing);
    m_fixation = new QLineEdit(gCare);
    f3->addRow(tr("&Fixierung/Sekundärverband:"), m_fixation);
    m_compression = new QLineEdit(gCare);
    m_compression->setPlaceholderText(tr("Kompression, Druckentlastung, Lagerung"));
    f3->addRow(tr("&Kompression/Entlastung:"), m_compression);
    m_interval = optSpin(30, gCare);
    m_interval->setRange(0, 30);
    m_interval->setSpecialValueText(QStringLiteral("–"));
    m_interval->setSuffix(tr(" Tage"));
    m_hasNext = new QCheckBox(tr("nächster am"), gCare);
    m_next = dateEdit(gCare);
    auto *nextRow = new QHBoxLayout;
    nextRow->addWidget(new QLabel(tr("alle"), gCare));
    nextRow->addWidget(m_interval);
    nextRow->addWidget(m_hasNext);
    nextRow->addWidget(m_next);
    nextRow->addStretch(1);
    f3->addRow(tr("Verbandwechsel:"), nextRow);
    m_notes = textBox(gCare, 2);
    f3->addRow(tr("Be&merkung:"), m_notes);
    outer->addWidget(gCare);

    // --- Foto
    auto *gPhoto = new QGroupBox(tr("Foto"), form);
    auto *ph = new QHBoxLayout(gPhoto);
    m_photo = new QLabel(gPhoto);
    m_photo->setObjectName(QStringLiteral("entryPhoto"));
    m_photo->setFixedSize(220, 165);
    m_photo->setAlignment(Qt::AlignCenter);
    m_photo->setFrameShape(QFrame::StyledPanel);
    ph->addWidget(m_photo);
    auto *pv = new QVBoxLayout;
    m_photoBtn = new QPushButton(tr("Foto &laden …"), gPhoto);
    m_photoDel = new QPushButton(tr("Foto entfernen"), gPhoto);
    m_photoHint = new QLabel(gPhoto);
    m_photoHint->setWordWrap(true);
    pv->addWidget(m_photoBtn);
    pv->addWidget(m_photoDel);
    pv->addWidget(m_photoHint);
    pv->addStretch(1);
    ph->addLayout(pv, 1);
    outer->addWidget(gPhoto);
    outer->addStretch(1);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(FormLoader::scrollable(form, this), 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Save)->setText(tr("Speichern"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    // Werte
    m_kind->setCurrentIndex(e.kind == DressingChange ? 1 : 0);
    m_at->setDateTime(e.at.isValid() ? e.at : QDateTime::currentDateTime());
    setSize(m_len, e.length);
    setSize(m_wid, e.width);
    setSize(m_dep, e.depth);
    m_pockets->setText(e.pockets);
    m_gran->setValue(e.granulation);
    m_epi->setValue(e.epithel);
    m_fib->setValue(e.fibrin);
    m_nec->setValue(e.necrosis);
    m_exposed->setValues(e.exposed);
    setIndexOpt(m_exudate, e.exudate);
    setText(m_exudateKind, e.exudateKind);
    setIndexOpt(m_odor, e.odor);
    m_edge->setValues(e.edge);
    m_surround->setValues(e.surroundings);
    m_infection->setValues(e.infection);
    setOpt(m_pain, e.pain);
    setOpt(m_painChange, e.painChange);
    // neue Beurteilung: aktuelle Klassifikation der Wunde vorschlagen
    setIndexOpt(m_grade, e.id || e.grade >= 0 ? e.grade : w.grade);
    setIndexOpt(m_phase, e.phase);
    setIndexOpt(m_trend, e.trend);
    m_description->setText(e.description);
    m_cleansing->setText(e.cleansing);
    m_dressing->setText(e.dressing);
    m_fixation->setText(e.fixation);
    m_compression->setText(e.compression);
    m_interval->setValue(e.intervalDays);
    // neuer Eintrag mit Intervall: nächsten Wechsel gleich vorschlagen
    const bool next = e.nextChange.isValid() || (!e.id && e.intervalDays > 0);
    m_hasNext->setChecked(next);
    m_next->setDate(e.nextChange.isValid() ? e.nextChange : m_at->date().addDays(qMax(1, e.intervalDays)));
    m_next->setEnabled(next);
    m_notes->setPlainText(e.notes);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    for (QDoubleSpinBox *s : { m_len, m_wid, m_dep })
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] { updateArea(); });
    for (QSpinBox *s : { m_gran, m_epi, m_fib, m_nec })
        connect(s, &QSpinBox::valueChanged, this, [this] { updateArea(); });
    connect(m_hasNext, &QCheckBox::toggled, m_next, &QWidget::setEnabled);
    connect(m_interval, &QSpinBox::valueChanged, this, [this](int v) {
        if (v > 0) {
            m_hasNext->setChecked(true);
            m_next->setDate(m_at->date().addDays(v));
        }
    });
    connect(m_photoBtn, &QPushButton::clicked, this, [this] {
        QSettings st;
        const QString dir = st.value(QStringLiteral("wounds/lastDir"),
                                     QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).toString();
        const QString path = QFileDialog::getOpenFileName(this, tr("Wundfoto laden"), dir,
                                                          tr("Bilder (*.jpg *.jpeg *.png *.heic *.webp *.bmp)"));
        if (path.isEmpty()) return;
        st.setValue(QStringLiteral("wounds/lastDir"), QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, windowTitle(), tr("Die Datei lässt sich nicht öffnen."));
            return;
        }
        QString err;
        const QByteArray jpeg = preparePhoto(f.readAll(), &err);
        if (jpeg.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), err.isEmpty() ? tr("Das Bild lässt sich nicht lesen.") : err);
            return;
        }
        setPhotoData(jpeg);
    });
    connect(m_photoDel, &QPushButton::clicked, this, [this] { setPhotoData({}); });

    updateKind();
    updateArea();
    updatePhoto();
    resize(760, 820);
}

void WoundEntryDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    FormLoader::fitToScreen(this);
}

void WoundEntryDialog::setPhotoData(const QByteArray &jpeg)
{
    m_entry.photo = jpeg;
    updatePhoto();
}

void WoundEntryDialog::updateKind()
{
    const bool full = m_kind->currentIndex() == 0;
    for (QWidget *w : std::as_const(m_assessOnly)) w->setVisible(full);
}

void WoundEntryDialog::updateArea()
{
    Entry tmp;
    tmp.length = spinSize(m_len);
    tmp.width = spinSize(m_wid);
    const auto a = tmp.area();
    m_area->setText(a ? tr("Fläche %1 cm²").arg(QLocale().toString(*a, 'f', 1)) : QString());
    const int sum = m_gran->value() + m_epi->value() + m_fib->value() + m_nec->value();
    m_groundSum->setText(sum ? tr("Summe %1 %").arg(sum) : QString());
    m_groundSum->setStyleSheet(sum > 100 ? QStringLiteral("color: #c62828; font-weight: bold;") : QString());
}

void WoundEntryDialog::updatePhoto()
{
    const bool consent = m_wound.photoConsent;
    m_photoBtn->setEnabled(consent);
    m_photoDel->setEnabled(!m_entry.photo.isEmpty());
    const QPixmap px = photoPixmap(m_entry.photo, 220);
    if (px.isNull()) m_photo->setText(tr("kein Foto"));
    else m_photo->setPixmap(px);
    m_photoHint->setText(consent
        ? tr("Das Foto wird verkleinert in der Datenbank gespeichert. Ein Maßband im Bild erleichtert den Vergleich.")
        : tr("Fotos erst, wenn die Einwilligung zur Fotodokumentation vorliegt (Wunde bearbeiten)."));
}

Entry WoundEntryDialog::entry() const
{
    Entry e = m_entry;
    e.woundId = m_wound.id;
    e.patientId = m_wound.patientId;
    e.kind = m_kind->currentIndex() == 0 ? Assessment : DressingChange;
    e.at = m_at->dateTime();
    e.description = m_description->text().trimmed();
    e.trend = indexOpt(m_trend);
    e.cleansing = m_cleansing->text().trimmed();
    e.dressing = m_dressing->text().trimmed();
    e.fixation = m_fixation->text().trimmed();
    e.compression = m_compression->text().trimmed();
    e.intervalDays = m_interval->value();
    e.nextChange = m_hasNext->isChecked() ? m_next->date() : QDate();
    e.notes = m_notes->toPlainText().trimmed();
    if (e.kind == Assessment) {
        e.length = spinSize(m_len);
        e.width = spinSize(m_wid);
        e.depth = spinSize(m_dep);
        e.pockets = m_pockets->text().trimmed();
        e.granulation = m_gran->value();
        e.epithel = m_epi->value();
        e.fibrin = m_fib->value();
        e.necrosis = m_nec->value();
        e.exposed = m_exposed->values();
        e.exudate = indexOpt(m_exudate);
        e.exudateKind = m_exudateKind->currentText().trimmed();
        e.odor = indexOpt(m_odor);
        e.edge = m_edge->values();
        e.surroundings = m_surround->values();
        e.infection = m_infection->values();
        e.pain = opt(m_pain);
        e.painChange = opt(m_painChange);
        e.grade = m_grade->isEnabled() ? indexOpt(m_grade) : -1;
        e.phase = indexOpt(m_phase);
    } else {
        // Verbandwechsel: nur Kurzdokumentation
        e.length = e.width = e.depth = std::nullopt;
        e.pockets.clear();
        e.granulation = e.epithel = e.fibrin = e.necrosis = 0;
        e.exposed.clear();
        e.exudate = e.odor = e.grade = e.phase = -1;
        e.exudateKind.clear();
        e.edge.clear();
        e.surroundings.clear();
        e.infection.clear();
        e.pain = e.painChange = std::nullopt;
    }
    return e;
}

void WoundEntryDialog::accept()
{
    Entry e = entry();
    if (e.kind == DressingChange && e.description.isEmpty() && e.trend < 0 && e.dressing.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte mindestens Beschreibung, Tendenz oder Wundauflage angeben."));
        m_description->setFocus();
        return;
    }
    QString err;
    if (!saveEntry(e, &err)) {
        QMessageBox::warning(this, windowTitle(), tr("Der Eintrag konnte nicht gespeichert werden:\n%1").arg(err));
        return;
    }
    m_entry = e;
    QDialog::accept();
}

// ============================================================ WoundsDialog

namespace {
enum Col { CAt, CKind, CBy, CSize, CText, CPhoto, CCount };
}

WoundsDialog::WoundsDialog(const Patient &p, QWidget *parent, qint64 select)
    : QDialog(parent), m_patient(p)
{
    setObjectName(QStringLiteral("WoundsDialog"));
    setWindowTitle(tr("Wunddokumentation – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);
    auto *split = new QSplitter(Qt::Horizontal, this);

    // links: Wunden
    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(new QLabel(tr("<b>Wunden</b>"), left));
    m_list = new QListWidget(left);
    m_list->setObjectName(QStringLiteral("woundList"));
    m_list->setWordWrap(true);
    ll->addWidget(m_list, 1);
    m_showClosed = new QCheckBox(tr("abgeheilte und beendete zeigen"), left);
    m_showClosed->setChecked(true);
    ll->addWidget(m_showClosed);
    auto *lb = new QGridLayout;
    auto *btnNew = new QPushButton(tr("&Neue Wunde …"), left);
    btnNew->setObjectName(QStringLiteral("btnNewWound"));
    m_btnEdit = new QPushButton(tr("&Bearbeiten …"), left);
    m_btnEdit->setObjectName(QStringLiteral("btnEditWound"));
    m_btnHealed = new QPushButton(tr("Abge&heilt"), left);
    m_btnDelete = new QPushButton(tr("&Löschen"), left);
    lb->addWidget(btnNew, 0, 0);
    lb->addWidget(m_btnEdit, 0, 1);
    lb->addWidget(m_btnHealed, 1, 0);
    lb->addWidget(m_btnDelete, 1, 1);
    ll->addLayout(lb);

    // rechts: Kopf, Verlauf, Chronik
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    auto *top = new QHBoxLayout;
    m_head = new QLabel(right);
    m_head->setObjectName(QStringLiteral("woundHead"));
    m_head->setWordWrap(true);
    m_head->setTextFormat(Qt::RichText);
    m_head->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    top->addWidget(m_head, 1);
    m_chart = new QLabel(right);
    m_chart->setObjectName(QStringLiteral("woundChart"));
    m_chart->setFixedSize(320, 160);
    m_chart->setAlignment(Qt::AlignCenter);
    top->addWidget(m_chart);
    rl->addLayout(top);

    auto *eb = new QHBoxLayout;
    m_btnAssess = new QPushButton(tr("&Wundbeurteilung …"), right);
    m_btnAssess->setObjectName(QStringLiteral("btnAssess"));
    m_btnDressing = new QPushButton(tr("&Verbandwechsel …"), right);
    m_btnDressing->setObjectName(QStringLiteral("btnDressing"));
    m_btnEditEntry = new QPushButton(tr("Eintrag b&earbeiten …"), right);
    m_btnDelEntry = new QPushButton(tr("Eintrag l&öschen"), right);
    eb->addWidget(m_btnAssess);
    eb->addWidget(m_btnDressing);
    eb->addStretch(1);
    eb->addWidget(m_btnEditEntry);
    eb->addWidget(m_btnDelEntry);
    rl->addLayout(eb);

    m_table = new QTableWidget(0, CCount, right);
    m_table->setObjectName(QStringLiteral("woundChronicle"));
    m_table->setHorizontalHeaderLabels({ tr("Zeitpunkt"), tr("Art"), tr("Hz."), tr("Größe"), tr("Befund / Versorgung"), tr("Foto") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(true);
    m_table->horizontalHeader()->setSectionResizeMode(CText, QHeaderView::Stretch);
    m_table->setColumnWidth(CAt, 120);
    m_table->setColumnWidth(CKind, 120);
    m_table->setColumnWidth(CBy, 60);
    m_table->setColumnWidth(CSize, 170);
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, m_table, [this] { m_table->resizeRowsToContents(); });
    m_table->setColumnWidth(CPhoto, 50);
    rl->addWidget(m_table, 1);

    auto *bottom = new QHBoxLayout;
    m_entryText = new QLabel(right);
    m_entryText->setWordWrap(true);
    m_entryText->setTextFormat(Qt::RichText);
    m_entryText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_entryText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *sa = new QScrollArea(right);
    sa->setWidget(m_entryText);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setMinimumHeight(130);
    bottom->addWidget(sa, 1);
    m_photo = new QLabel(right);
    m_photo->setObjectName(QStringLiteral("woundPhoto"));
    m_photo->setFixedSize(200, 150);
    m_photo->setAlignment(Qt::AlignCenter);
    bottom->addWidget(m_photo);
    rl->addLayout(bottom);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    lay->addWidget(split, 1);

    auto *bb = new QHBoxLayout;
    m_btnOut = new QPushButton(tr("&Ausgabe"), this);
    auto *menu = new QMenu(m_btnOut);
    menu->addAction(tr("Drucken …"), this, [this] { output(0); });
    menu->addAction(tr("Druckvorschau …"), this, [this] { output(1); });
    menu->addAction(tr("Als PDF speichern …"), this, [this] { output(2); });
    menu->addAction(tr("Als Excel-Tabelle exportieren …"), this, [this] { output(3); });
    m_btnOut->setMenu(menu);
    bb->addWidget(m_btnOut);
    auto *note = new QLabel(tr("Hinweise ersetzen keine fachliche Wundbeurteilung."), this);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    bb->addWidget(note, 1);
    auto *close = new QPushButton(tr("Schließen"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    bb->addWidget(close);
    lay->addLayout(bb);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { showWound(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { editWound(); });
    connect(m_showClosed, &QCheckBox::toggled, this, [this] { reload(); });
    connect(btnNew, &QPushButton::clicked, this, &WoundsDialog::addWound);
    connect(m_btnEdit, &QPushButton::clicked, this, &WoundsDialog::editWound);
    connect(m_btnDelete, &QPushButton::clicked, this, &WoundsDialog::deleteWound);
    connect(m_btnHealed, &QPushButton::clicked, this, &WoundsDialog::markHealed);
    connect(m_btnAssess, &QPushButton::clicked, this, [this] { addEntry(Assessment); });
    connect(m_btnDressing, &QPushButton::clicked, this, [this] { addEntry(DressingChange); });
    connect(m_btnEditEntry, &QPushButton::clicked, this, &WoundsDialog::editEntry);
    connect(m_btnDelEntry, &QPushButton::clicked, this, &WoundsDialog::deleteEntry);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { showEntry(); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { editEntry(); });
    connect(&Database::instance(), &Database::woundsChanged, this, [this](qint64 pid) { if (pid == m_patient.id) reload(); });

    reload();
    if (select) selectWound(select);
    resize(1180, 760);
}

qint64 WoundsDialog::currentWoundId() const
{
    const int r = m_list->currentRow();
    return r >= 0 && r < m_wounds.size() ? m_wounds.at(r).id : 0;
}

void WoundsDialog::selectWound(qint64 id)
{
    for (int i = 0; i < m_wounds.size(); ++i)
        if (m_wounds.at(i).id == id) { m_list->setCurrentRow(i); return; }
}

void WoundsDialog::reload()
{
    const qint64 keep = currentWoundId();
    m_wounds = wounds(m_patient.id, m_showClosed->isChecked());
    QSignalBlocker b(m_list);
    m_list->clear();
    for (const Wound &w : std::as_const(m_wounds)) {
        auto *it = new QListWidgetItem(w.title(), m_list);
        QStringList tip{ woundSummary(w) };
        if (w.status != Open) {
            it->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            it->setText(w.title() + QStringLiteral(" (%1)").arg(statusNames().value(w.status)));
        } else {
            const auto n = notices(w, entries(w.id, false));
            for (const Notice &x : n) {
                tip << x.text;
                if (x.warn) it->setForeground(QColor(0xc6, 0x28, 0x28));
            }
        }
        it->setToolTip(tip.join(QLatin1Char('\n')));
    }
    int row = 0;
    for (int i = 0; i < m_wounds.size(); ++i)
        if (m_wounds.at(i).id == keep) row = i;
    if (!m_wounds.isEmpty()) m_list->setCurrentRow(row);
    b.unblock();
    showWound();
}

void WoundsDialog::showWound()
{
    const qint64 id = currentWoundId();
    const bool has = id != 0;
    m_btnEdit->setEnabled(has);
    m_btnDelete->setEnabled(has);
    m_btnAssess->setEnabled(has);
    m_btnDressing->setEnabled(has);
    m_btnOut->setEnabled(has);
    m_table->setRowCount(0);
    m_entries.clear();
    m_chart->clear();
    if (!has) {
        m_btnHealed->setEnabled(false);
        m_head->setText(m_wounds.isEmpty()
            ? tr("Noch keine Wunde erfasst. Mit <b>Neue Wunde</b> beginnen – Wunden, die schon bei der Aufnahme "
                 "bestanden, kennzeichnen Sie dort mit „bei Aufnahme bereits vorhanden“.")
            : QString());
        showEntry();
        return;
    }
    const Wound w = m_wounds.at(m_list->currentRow());
    m_btnHealed->setEnabled(w.status == Open);
    m_entries = entries(id, false);

    QString h = QStringLiteral("<b>%1</b>").arg(w.title().toHtmlEscaped());
    QStringList facts;
    if (!w.diagnosis.isEmpty()) facts << tr("Diagnose: %1").arg(w.diagnosis.toHtmlEscaped());
    if (!w.siteDetail.isEmpty()) facts << w.siteDetail.toHtmlEscaped();
    if (w.since.isValid()) facts << tr("seit %1").arg(dmy(w.since));
    if (w.onAdmission) facts << tr("bei Aufnahme vorhanden (%1)").arg(dmy(w.admissionDate));
    facts << tr("Ziel: %1").arg(goals().value(w.goal).toHtmlEscaped());
    if (w.status != Open) facts << QStringLiteral("%1 %2").arg(statusNames().value(w.status), dmy(w.closedAt));
    h += QStringLiteral("<br>") + facts.join(QStringLiteral(" · "));
    if (w.status == Open) {
        for (const Notice &n : notices(w, m_entries))
            h += QStringLiteral("<br><span style=\"color:%1\">%2 %3</span>")
                     .arg(n.warn ? QStringLiteral("#c62828") : QStringLiteral("#a67c00"),
                          n.warn ? QStringLiteral("⚠") : QStringLiteral("•"), n.text.toHtmlEscaped());
        const QDate due = nextAssessmentDue(w, m_entries);
        if (due.isValid()) h += QStringLiteral("<br>") + tr("Nächste Wundbeurteilung spätestens am %1.").arg(dmy(due));
    }
    m_head->setText(h);

    const QImage chart = areaChart(m_entries);
    if (chart.isNull()) m_chart->setText(tr("Flächenverlauf ab zwei\nvermessenen Beurteilungen"));
    else m_chart->setPixmap(QPixmap::fromImage(chart.scaled(m_chart->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));

    m_table->setRowCount(int(m_entries.size()));
    for (int r = 0; r < m_entries.size(); ++r) {
        const Entry &e = m_entries.at(r);
        auto put = [&](int c, const QString &t) {
            auto *it = new QTableWidgetItem(t);
            it->setToolTip(t);
            m_table->setItem(r, c, it);
        };
        put(CAt, QLocale().toString(e.at, QStringLiteral("dd.MM.yyyy HH:mm")));
        put(CKind, entryKindName(e.kind));
        put(CBy, e.by);
        put(CSize, sizeText(e));
        put(CText, entrySummary(e));
        put(CPhoto, e.photo.isEmpty() ? QString() : QStringLiteral("📷"));
        if (e.kind == Assessment) {
            QFont f = m_table->font();
            f.setBold(true);
            m_table->item(r, CKind)->setFont(f);
        }
    }
    QTimer::singleShot(0, m_table, &QTableWidget::resizeRowsToContents);   // erst mit endgültiger Spaltenbreite
    if (!m_entries.isEmpty()) m_table->selectRow(0);
    showEntry();
}

void WoundsDialog::showEntry()
{
    const int r = m_table->currentRow();
    const bool has = r >= 0 && r < m_entries.size() && !m_table->selectedItems().isEmpty();
    m_btnEditEntry->setEnabled(has);
    m_btnDelEntry->setEnabled(has);
    m_photo->clear();
    if (!has) { m_entryText->clear(); return; }
    const auto full = entry(m_entries.at(r).id);
    if (!full) return;
    const Entry &e = *full;
    QStringList l;
    l << QStringLiteral("<b>%1, %2</b> (%3)").arg(entryKindName(e.kind),
                                                  QLocale().toString(e.at, QStringLiteral("dd.MM.yyyy HH:mm")), e.by.toHtmlEscaped());
    auto add = [&](const QString &k, const QString &v) {
        if (!v.trimmed().isEmpty()) l << QStringLiteral("%1 %2").arg(k.toHtmlEscaped(), v.toHtmlEscaped());
    };
    add(tr("Größe:"), sizeText(e));
    add(tr("Taschen:"), e.pockets);
    add(tr("Freiliegend:"), e.exposed.join(QStringLiteral(", ")));
    add(tr("Geruch:"), e.odor >= 0 ? odors().value(e.odor) : QString());
    add(tr("Wundrand:"), e.edge.join(QStringLiteral(", ")));
    add(tr("Umgebung:"), e.surroundings.join(QStringLiteral(", ")));
    add(tr("Schmerz beim Verbandwechsel:"), e.painChange ? QStringLiteral("%1/10").arg(*e.painChange) : QString());
    add(tr("Phase:"), e.phase >= 0 ? phases().value(e.phase) : QString());
    if (e.grade >= 0) add(gradeSystem(m_wounds.value(m_list->currentRow()).kind) + QLatin1Char(':'),
                          gradeName(m_wounds.value(m_list->currentRow()).kind, e.grade));
    add(tr("Befund:"), entrySummary(e));
    add(tr("Reinigung:"), e.cleansing);
    add(tr("Fixierung:"), e.fixation);
    add(tr("Kompression/Entlastung:"), e.compression);
    add(tr("Nächster Verbandwechsel:"), dmy(e.nextChange));
    add(tr("Bemerkung:"), e.notes);
    m_entryText->setText(l.join(QStringLiteral("<br>")));
    const QPixmap px = photoPixmap(e.photo, 200);
    if (!px.isNull()) m_photo->setPixmap(px);
}

void WoundsDialog::addWound()
{
    Wound w;
    w.patientId = m_patient.id;
    WoundDialog dlg(w, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
        selectWound(dlg.wound().id);
    }
}

void WoundsDialog::editWound()
{
    const qint64 id = currentWoundId();
    const auto w = id ? wound(id) : std::nullopt;
    if (!w) return;
    WoundDialog dlg(*w, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
        selectWound(id);
    }
}

void WoundsDialog::deleteWound()
{
    const qint64 id = currentWoundId();
    if (!id) return;
    const Wound w = m_wounds.at(m_list->currentRow());
    if (QMessageBox::question(this, tr("Wunde löschen"),
                              tr("„%1“ mit der gesamten Chronik (%2 Einträge) endgültig löschen?\n\n"
                                 "Abgeheilte Wunden besser als „abgeheilt“ kennzeichnen – dann bleibt die Dokumentation erhalten.")
                                  .arg(w.title()).arg(entries(id, false).size()))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Wounds::deleteWound(id, &err))
        QMessageBox::warning(this, tr("Wunde löschen"), err);
    reload();
}

void WoundsDialog::markHealed()
{
    const qint64 id = currentWoundId();
    auto w = id ? wound(id) : std::nullopt;
    if (!w || w->status != Open) return;
    if (QMessageBox::question(this, tr("Wunde abgeheilt"), tr("„%1“ heute als abgeheilt kennzeichnen?").arg(w->title()))
        != QMessageBox::Yes)
        return;
    w->status = Healed;
    w->closedAt = QDate::currentDate();
    QString err;
    if (!saveWound(*w, &err)) QMessageBox::warning(this, tr("Wunde abgeheilt"), err);
    reload();
    selectWound(id);
}

void WoundsDialog::addEntry(int kind)
{
    const qint64 id = currentWoundId();
    const auto w = id ? wound(id) : std::nullopt;
    if (!w) return;
    Entry e;
    e.kind = kind;
    e.woundId = id;
    e.at = QDateTime::currentDateTime();
    // Versorgung vom letzten Eintrag übernehmen (meist unverändert)
    const QList<Entry> list = entries(id, false);
    if (!list.isEmpty()) {
        const Entry &last = list.first();
        e.cleansing = last.cleansing;
        e.dressing = last.dressing;
        e.fixation = last.fixation;
        e.compression = last.compression;
        e.intervalDays = last.intervalDays;
    }
    WoundEntryDialog dlg(*w, e, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
        selectWound(id);
    }
}

void WoundsDialog::editEntry()
{
    const int r = m_table->currentRow();
    const qint64 wid = currentWoundId();
    if (r < 0 || r >= m_entries.size() || !wid) return;
    const auto e = entry(m_entries.at(r).id);
    const auto w = wound(wid);
    if (!e || !w) return;
    WoundEntryDialog dlg(*w, *e, this);
    if (dlg.exec() == QDialog::Accepted) {
        reload();
        selectWound(wid);
    }
}

void WoundsDialog::deleteEntry()
{
    const int r = m_table->currentRow();
    if (r < 0 || r >= m_entries.size()) return;
    const Entry &e = m_entries.at(r);
    if (QMessageBox::question(this, tr("Eintrag löschen"),
                              tr("%1 vom %2 löschen?").arg(entryKindName(e.kind),
                                                          QLocale().toString(e.at, QStringLiteral("dd.MM.yyyy HH:mm"))))
        != QMessageBox::Yes)
        return;
    const qint64 wid = currentWoundId();
    QString err;
    if (!Wounds::deleteEntry(e.id, &err)) QMessageBox::warning(this, tr("Eintrag löschen"), err);
    reload();
    selectWound(wid);
}

void WoundsDialog::output(int how)
{
    const qint64 id = currentWoundId();
    const auto w = id ? wound(id) : std::nullopt;
    if (!w) return;
    const QString title = tr("Wunddokumentation");
    if (how == 3) {
        const QString path = ReportPrinter::askExportFile(this, tr("Wunddokumentation nach Excel exportieren"),
                                                          QStringLiteral("Wunde"), m_patient, QStringLiteral("xlsx"),
                                                          tr("Excel-Tabelle (*.xlsx)"));
        if (path.isEmpty()) return;
        QString err;
        if (!toXlsx(m_patient, *w, path, &err))
            QMessageBox::warning(this, title, tr("Export fehlgeschlagen:\n%1").arg(err));
        return;
    }
    const QString html = reportHtml(m_patient, *w, entries(id, true));
    const auto target = how == 0 ? ReportPrinter::Target::Printer
                      : how == 1 ? ReportPrinter::Target::Preview : ReportPrinter::Target::Pdf;
    ReportPrinter::htmlReport(this, m_patient, title, html, target, false);
}
