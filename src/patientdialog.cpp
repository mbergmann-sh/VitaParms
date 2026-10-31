// SPDX-License-Identifier: MIT
#include "patientdialog.h"
#include "edition.h"
#include "contactdialog.h"
#include <QBoxLayout>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QFileDialog>
#include <QGroupBox>
#include <QToolButton>
#include "pdftools.h"
#include "wounddialogs.h"
#include "wounds.h"
#include "leaflet.h"
#include "medsearchdialog.h"
#include "medschedule.h"
#include "peakflow.h"
#include "inr.h"
#include "fluid.h"
#include "appsettings.h"
#include "bmi.h"
#include "careinfo.h"
#include "database.h"
#include "formloader.h"
#include "medicationdialog.h"
#include "reportprinter.h"
#include "vitaldefs.h"
#include "vitaldialog.h"

#include <QAbstractButton>
#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QGridLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QCollator>
#include <QMenu>
#include <QMessageBox>
#include <QFormLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum MedCol { ColName, ColSubstance, ColDose, ColReason, ColNotes, ColStart, ColEnd, ColStatus, ColCount };

void fillArmCombo(QComboBox *c)
{
    c->clear();
    for (ArmLevel l : { ArmLevel::None, ArmLevel::Hand, ArmLevel::ForearmWithHand, ArmLevel::WholeArm })
        c->addItem(Bmi::armLevelText(l), int(l));
}
void fillLegCombo(QComboBox *c)
{
    c->clear();
    for (LegLevel l : { LegLevel::None, LegLevel::Foot, LegLevel::LowerLegWithFoot, LegLevel::WholeLeg })
        c->addItem(Bmi::legLevelText(l), int(l));
}
} // namespace

PatientDialog::PatientDialog(const Patient &p, QWidget *parent)
    : QDialog(parent), m_patient(p)
{
    setWindowTitle(p.id ? tr("Stammblatt – %1").arg(p.displayName()) : tr("Neuer Benutzer – Stammblatt"));

    auto *lay = new QVBoxLayout(this);
    m_form = FormLoader::load(QStringLiteral("patientform"), this);
    lay->addWidget(FormLoader::scrollable(m_form, this), 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("&Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &PatientDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PatientDialog::reject);
    lay->addWidget(buttons);

    // Auswahllisten befüllt das Programm (damit die gespeicherten Codes stimmen)
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("sexCombo"))) {
        c->clear();
        c->addItem(QStringLiteral("—"), QString());
        c->addItem(tr("männlich"), QStringLiteral("m"));
        c->addItem(tr("weiblich"), QStringLiteral("w"));
        c->addItem(tr("divers"),   QStringLiteral("d"));
    }
    for (const char *n : { "armLeftCombo", "armRightCombo" })
        if (auto *c = m_form->findChild<QComboBox *>(QLatin1String(n))) {
            fillArmCombo(c);
            connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatientDialog::updateAmputationInfo);
        }
    for (const char *n : { "legLeftCombo", "legRightCombo" })
        if (auto *c = m_form->findChild<QComboBox *>(QLatin1String(n))) {
            fillLegCombo(c);
            connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatientDialog::updateAmputationInfo);
        }

    setupCareWidgets();
    setupContacts();

    // Werte eintragen
    FormIO::write(m_form, QStringLiteral("lastNameEdit"), p.lastName);
    FormIO::write(m_form, QStringLiteral("firstNameEdit"), p.firstName);
    FormIO::write(m_form, QStringLiteral("birthDateEdit"), p.birthDate);
    FormIO::write(m_form, QStringLiteral("sexCombo"), p.sex);
    FormIO::write(m_form, QStringLiteral("streetEdit"), p.street);
    FormIO::write(m_form, QStringLiteral("zipEdit"), p.zip);
    FormIO::write(m_form, QStringLiteral("cityEdit"), p.city);
    FormIO::write(m_form, QStringLiteral("phoneEdit"), p.phone);
    FormIO::write(m_form, QStringLiteral("mobileEdit"), p.mobile);
    FormIO::write(m_form, QStringLiteral("diagnosesEdit"), p.diagnoses);
    FormIO::write(m_form, QStringLiteral("notesEdit"), p.notes);
    FormIO::write(m_form, QStringLiteral("armLeftCombo"),  int(p.amputations.armLeft));
    FormIO::write(m_form, QStringLiteral("armRightCombo"), int(p.amputations.armRight));
    FormIO::write(m_form, QStringLiteral("legLeftCombo"),  int(p.amputations.legLeft));
    FormIO::write(m_form, QStringLiteral("legRightCombo"), int(p.amputations.legRight));
    FormIO::applyExtra(m_form, p.extra);
    updateAmputationInfo();
    setupPeakFlow();
    setupTargets();
    // Pflegegrad und Schwerbehindertenausweis
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("careLevelCombo")))
        c->setCurrentIndex(qBound(0, p.careLevel, c->count() - 1));
    FormIO::write(m_form, QStringLiteral("careReviewEdit"), p.careReview);
    FormIO::write(m_form, QStringLiteral("disabilityDegreeSpin"), p.disabilityDegree > 0 ? QVariant(p.disabilityDegree) : QVariant());
    for (QCheckBox *cb : m_form->findChildren<QCheckBox *>())
        if (cb->property("vpMark").isValid())
            cb->setChecked(p.disabilityMarks.contains(cb->property("vpMark").toString()));
    if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("disabilityValidEdit")))
        d->setDate(p.disabilityValidUntil.isValid() ? p.disabilityValidUntil : QDate::currentDate().addYears(5));
    FormIO::write(m_form, QStringLiteral("disabilityUnlimitedCheck"),
                  p.disabilityDegree > 0 && !p.disabilityValidUntil.isValid());
    updateCareEnabled();
    loadAdvance(p);

    // Medikamentenplan
    if (p.id)
        m_meds = Database::instance().medications(p.id, true);
    m_medTable = m_form->findChild<QTableWidget *>(QStringLiteral("medicationTable"));
    if (m_medTable) {
        m_medTable->setColumnCount(ColCount);
        m_medTable->setHorizontalHeaderLabels({ tr("Präparat"), tr("Wirkstoff"), tr("Dosierung"),
                                                tr("Grund"), tr("Bemerkung / Hinweise"), tr("Beginn"),
                                                tr("Ende"), tr("Status") });
        m_medTable->verticalHeader()->hide();
        m_medTable->horizontalHeader()->setStretchLastSection(true);
        m_medTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_medTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_medTable->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(m_medTable, &QTableWidget::cellDoubleClicked, this, &PatientDialog::editMedication);
        // Sortieren per Klick auf den Spaltenkopf – nur innerhalb der jeweiligen Art (Regel-, Bedarfs-,
        // Notfallmedikation, Sonstiges); die Gruppen selbst bleiben unverändert
        QHeaderView *hh = m_medTable->horizontalHeader();
        hh->setSectionsClickable(true);
        hh->setSortIndicatorShown(false);
        hh->setToolTip(tr("Klick auf eine Spaltenüberschrift sortiert innerhalb jeder Art auf- bzw. absteigend"));
        connect(hh, &QHeaderView::sectionClicked, this, &PatientDialog::sortMedications);
        {
            QSettings s;
            m_sortColumn = s.value(QStringLiteral("medTable/sortColumn"), -1).toInt();
            m_sortOrder = s.value(QStringLiteral("medTable/sortDescending"), false).toBool() ? Qt::DescendingOrder : Qt::AscendingOrder;
            if (m_sortColumn >= ColCount) m_sortColumn = -1;
            if (m_sortColumn >= 0) {
                hh->setSortIndicatorShown(true);
                hh->setSortIndicator(m_sortColumn, m_sortOrder);
            }
        }
        connect(m_medTable, &QTableWidget::itemSelectionChanged, this, &PatientDialog::updateMedicationButtons);
    }
    auto connectButton = [this](const char *name, void (PatientDialog::*slot)()) {
        if (auto *b = m_form->findChild<QAbstractButton *>(QLatin1String(name)))
            connect(b, &QAbstractButton::clicked, this, slot);
    };
    connectButton("addMedicationButton", &PatientDialog::addMedication);
    connectButton("editMedicationButton", &PatientDialog::editMedication);
    connectButton("archiveMedicationButton", &PatientDialog::archiveMedication);
    connectButton("deleteMedicationButton", &PatientDialog::deleteMedication);
    connectButton("printMedicationButton", &PatientDialog::printMedication);
    connectButton("leafletMedicationButton", &PatientDialog::showLeaflet);
    connectButton("searchMedicationButton", &PatientDialog::searchMedications);
    connectButton("admissionButton", &PatientDialog::editAdmission);
    if (auto *c = m_form->findChild<QCheckBox *>(QStringLiteral("showArchivedCheck")))
        connect(c, &QCheckBox::toggled, this, &PatientDialog::fillMedicationTable);
    fillMedicationTable();

    // Aufnahme-Vitalwerte
    if (p.id)
        m_admission = Database::instance().admissionVitals(p.id);
    updateAdmissionLabel();
    setupWoundWidgets();

    const QByteArray geo = AppSettings::windowState(QStringLiteral("patientdialog"));
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(760, 560);
    FormLoader::fitToScreen(this);

    if (auto *e = m_form->findChild<QWidget *>(QStringLiteral("lastNameEdit")))
        e->setFocus();
}

Patient PatientDialog::collectPatient() const
{
    Patient p = m_patient;
    p.lastName  = FormIO::readString(m_form, QStringLiteral("lastNameEdit"), p.lastName).trimmed();
    p.firstName = FormIO::readString(m_form, QStringLiteral("firstNameEdit"), p.firstName).trimmed();
    if (const auto v = FormIO::read(m_form, QStringLiteral("birthDateEdit")))
        p.birthDate = v->toDate();
    p.sex       = FormIO::readString(m_form, QStringLiteral("sexCombo"), p.sex);
    p.street    = FormIO::readString(m_form, QStringLiteral("streetEdit"), p.street).trimmed();
    p.zip       = FormIO::readString(m_form, QStringLiteral("zipEdit"), p.zip).trimmed();
    p.city      = FormIO::readString(m_form, QStringLiteral("cityEdit"), p.city).trimmed();
    p.phone     = FormIO::readString(m_form, QStringLiteral("phoneEdit"), p.phone).trimmed();
    p.mobile    = FormIO::readString(m_form, QStringLiteral("mobileEdit"), p.mobile).trimmed();
    p.diagnoses = FormIO::readString(m_form, QStringLiteral("diagnosesEdit"), p.diagnoses);
    p.notes     = FormIO::readString(m_form, QStringLiteral("notesEdit"), p.notes);
    if (const auto v = FormIO::read(m_form, QStringLiteral("armLeftCombo")))  p.amputations.armLeft  = ArmLevel(v->toInt());
    if (const auto v = FormIO::read(m_form, QStringLiteral("armRightCombo"))) p.amputations.armRight = ArmLevel(v->toInt());
    if (const auto v = FormIO::read(m_form, QStringLiteral("legLeftCombo")))  p.amputations.legLeft  = LegLevel(v->toInt());
    if (const auto v = FormIO::read(m_form, QStringLiteral("legRightCombo"))) p.amputations.legRight = LegLevel(v->toInt());
    p.extra = FormIO::collectExtra(m_form, p.extra);
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("peakFlowBestSpin")))
        PeakFlow::setPersonalBest(p, sp->value());
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("bioSexCombo"))) {
        const QString v = c->currentData().toString();
        PeakFlow::setBiologicalSex(p, v.isEmpty() ? QChar() : v.at(0));
    }
    {   // INR-Zielbereich, Trinkmenge, Gewichtskontrolle (fehlen die Felder: unverändert)
        auto *lo = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("inrLowSpin"));
        auto *hi = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("inrHighSpin"));
        if (lo && hi) Inr::setRange(p, lo->value(), hi->value());
        auto *fmin = m_form->findChild<QSpinBox *>(QStringLiteral("fluidMinSpin"));
        auto *fmax = m_form->findChild<QSpinBox *>(QStringLiteral("fluidMaxSpin"));
        auto *ww = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("weightWatchSpin"));
        if (fmin && fmax && ww) Fluid::setLimits(p, fmin->value(), fmax->value(), ww->value());
    }
    if (m_contactTable) Contacts::store(p, m_contacts);   // angepasste ältere Formulare ohne Liste: unverändert
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("careLevelCombo")))
        p.careLevel = qMax(0, c->currentIndex());
    if (const auto v = FormIO::read(m_form, QStringLiteral("careReviewEdit")))
        p.careReview = p.careLevel > 0 ? v->toDate() : QDate();
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("disabilityDegreeSpin"))) {
        p.disabilityDegree = sp->value();
        QStringList marks;
        for (const CareInfo::Mark &m : CareInfo::marks())       // feste Reihenfolge wie im Ausweis
            for (QCheckBox *cb : m_form->findChildren<QCheckBox *>())
                if (cb->property("vpMark").toString() == m.code && cb->isChecked()) marks << m.code;
        p.disabilityMarks = p.disabilityDegree > 0 ? marks : QStringList();
        const auto unlimited = FormIO::read(m_form, QStringLiteral("disabilityUnlimitedCheck"));
        const auto valid = FormIO::read(m_form, QStringLiteral("disabilityValidEdit"));
        p.disabilityValidUntil = (p.disabilityDegree > 0 && !(unlimited && unlimited->toBool()) && valid)
                                     ? valid->toDate() : QDate();
    }
    for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
        if (!m_advCheck[k]) break;
        CareInfo::AdvanceDoc d;
        d.present = m_advCheck[k]->isChecked();
        d.person = m_advPerson[k]->text().trimmed();
        d.address = m_advAddr[k]->text().trimmed();
        d.phone = m_advPhone[k]->text().trimmed();
        d.validUntil = m_advValid[k]->date() > m_advValid[k]->minimumDate() ? m_advValid[k]->date() : QDate();
        d.docId = m_advUnlink[k] ? 0 : CareInfo::advanceDoc(m_patient, k).docId;
        CareInfo::setAdvanceDoc(p, k, d);
    }
    return p;
}

void PatientDialog::setupCareWidgets()
{
    if (auto *box = m_form->findChild<QWidget *>(QStringLiteral("disabilityMarksBox"))) {
        delete box->layout();
        auto *grid = new QGridLayout(box);
        grid->setContentsMargins(0, 0, 0, 0);
        int i = 0;
        for (const CareInfo::Mark &m : CareInfo::marks()) {
            auto *cb = new QCheckBox(m.code, box);
            cb->setObjectName(QStringLiteral("mark_") + QString(m.code).remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]"))));
            cb->setProperty("vpMark", m.code);
            cb->setToolTip(m.meaning);
            grid->addWidget(cb, i / 5, i % 5);
            ++i;
        }
    }
    setupAdvanceWidgets();
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("careLevelCombo")))
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatientDialog::updateCareEnabled);
    if (auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("disabilityDegreeSpin")))
        connect(sp, qOverload<int>(&QSpinBox::valueChanged), this, &PatientDialog::updateCareEnabled);
    if (auto *cb = m_form->findChild<QCheckBox *>(QStringLiteral("disabilityUnlimitedCheck")))
        connect(cb, &QCheckBox::toggled, this, &PatientDialog::updateCareEnabled);
}

void PatientDialog::setupAdvanceWidgets()
{
    auto *tab = m_form->findChild<QWidget *>(QStringLiteral("careTab"));
    if (!tab || !tab->layout() || m_form->findChild<QWidget *>(QStringLiteral("advanceGroup"))) return;
    auto *box = new QGroupBox(tr("Vorsorgedokumente"), tab);
    box->setObjectName(QStringLiteral("advanceGroup"));
    auto *g = new QGridLayout(box);
    const QStringList heads = { tr("Dokument liegt vor"), tr("Person"), tr("Anschrift"), tr("Telefon"), tr("gültig bis"), tr("PDF") };
    for (int c = 0; c < heads.size(); ++c) {
        auto *l = new QLabel(QStringLiteral("<small>%1</small>").arg(heads[c].toHtmlEscaped()), box);
        g->addWidget(l, 0, c);
    }
    for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
        const QString key = CareInfo::advanceKey(k);
        m_advCheck[k] = new QCheckBox(CareInfo::advanceName(k), box);
        m_advCheck[k]->setObjectName(key + QStringLiteral("Check"));
        m_advPerson[k] = new QLineEdit(box);
        m_advPerson[k]->setObjectName(key + QStringLiteral("PersonEdit"));
        m_advPerson[k]->setPlaceholderText(CareInfo::advancePersonLabel(k));
        m_advAddr[k] = new QLineEdit(box);
        m_advAddr[k]->setObjectName(key + QStringLiteral("AddressEdit"));
        m_advAddr[k]->setPlaceholderText(tr("Straße, PLZ Ort"));
        m_advPhone[k] = new QLineEdit(box);
        m_advPhone[k]->setObjectName(key + QStringLiteral("PhoneEdit"));
        m_advPhone[k]->setPlaceholderText(tr("Telefon"));
        m_advPhone[k]->setMaximumWidth(150);
        m_advValid[k] = new QDateEdit(box);
        m_advValid[k]->setObjectName(key + QStringLiteral("ValidEdit"));
        m_advValid[k]->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        m_advValid[k]->setCalendarPopup(true);
        m_advValid[k]->setMinimumDate(QDate(2000, 1, 1));
        m_advValid[k]->setSpecialValueText(tr("unbefristet"));
        m_advValid[k]->setDate(m_advValid[k]->minimumDate());
        m_advValid[k]->setToolTip(tr("Leer lassen (unbefristet), wenn das Dokument keine Befristung hat"));
        m_advPdf[k] = new QToolButton(box);
        m_advPdf[k]->setObjectName(key + QStringLiteral("PdfButton"));
        m_advPdf[k]->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_advPdf[k]->setPopupMode(QToolButton::InstantPopup);
        g->addWidget(m_advCheck[k], k + 1, 0);
        g->addWidget(m_advPerson[k], k + 1, 1);
        g->addWidget(m_advAddr[k], k + 1, 2);
        g->addWidget(m_advPhone[k], k + 1, 3);
        g->addWidget(m_advValid[k], k + 1, 4);
        g->addWidget(m_advPdf[k], k + 1, 5);
        connect(m_advCheck[k], &QCheckBox::toggled, this, &PatientDialog::updateAdvanceUi);
        connect(m_advPdf[k], &QToolButton::pressed, this, [this, k] { advancePdfMenu(k); });
    }
    g->setColumnStretch(1, 2);
    g->setColumnStretch(2, 3);
    auto *hint = new QLabel(tr("<small>Die PDF-Dateien werden verschlüsselt in der Krankenakte abgelegt (Art „Vorsorge“). "
                               "Bewahren Sie die Originale gut auffindbar auf – im Notfall zählen die unterschriebenen Dokumente.</small>"), box);
    hint->setWordWrap(true);
    g->addWidget(hint, CareInfo::AdvanceCount + 1, 0, 1, heads.size());
    auto *lay = qobject_cast<QBoxLayout *>(tab->layout());
    int at = lay ? lay->count() : 0;
    if (lay && at > 0 && lay->itemAt(at - 1)->spacerItem()) --at;
    if (lay) lay->insertWidget(at, box);
    else tab->layout()->addWidget(box);
}

void PatientDialog::loadAdvance(const Patient &p)
{
    for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
        if (!m_advCheck[k]) return;
        const CareInfo::AdvanceDoc d = CareInfo::advanceDoc(p, k);
        m_advCheck[k]->setChecked(d.present);
        m_advPerson[k]->setText(d.person);
        m_advAddr[k]->setText(d.address);
        m_advPhone[k]->setText(d.phone);
        m_advValid[k]->setDate(d.validUntil.isValid() ? d.validUntil : m_advValid[k]->minimumDate());
        m_advNewPdf[k].clear();
        m_advUnlink[k] = false;
    }
    updateAdvanceUi();
}

void PatientDialog::updateAdvanceUi()
{
    for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
        if (!m_advCheck[k]) return;
        const bool on = m_advCheck[k]->isChecked();
        for (QWidget *w : { static_cast<QWidget *>(m_advPerson[k]), static_cast<QWidget *>(m_advAddr[k]),
                            static_cast<QWidget *>(m_advPhone[k]), static_cast<QWidget *>(m_advValid[k]),
                            static_cast<QWidget *>(m_advPdf[k]) })
            w->setEnabled(on);
        const bool has = !m_advNewPdf[k].isEmpty() || (!m_advUnlink[k] && CareInfo::advanceDoc(m_patient, k).docId);
        m_advPdf[k]->setText(has ? tr("PDF ✔") : tr("PDF…"));
        m_advPdf[k]->setToolTip(has ? tr("PDF hinterlegt – anzeigen, ersetzen oder entfernen") : tr("PDF hinterlegen"));
    }
}

void PatientDialog::advancePdfMenu(int k)
{
    QMenu menu(this);
    const qint64 docId = m_advUnlink[k] ? 0 : CareInfo::advanceDoc(m_patient, k).docId;
    const bool has = !m_advNewPdf[k].isEmpty() || docId;
    QAction *put = menu.addAction(has ? tr("PDF &ersetzen…") : tr("PDF &hinterlegen…"));
    QAction *show = menu.addAction(tr("PDF &anzeigen"));
    QAction *drop = menu.addAction(tr("Verknüpfung &entfernen"));
    show->setEnabled(has);
    drop->setEnabled(has);
    QAction *a = menu.exec(m_advPdf[k]->mapToGlobal(QPoint(0, m_advPdf[k]->height())));
    m_advPdf[k]->setDown(false);
    if (a == put) {
        const QString f = QFileDialog::getOpenFileName(this, tr("%1 als PDF hinterlegen").arg(CareInfo::advanceName(k)),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("PDF-Dokument (*.pdf)"));
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, windowTitle(), file.errorString()); return; }
        const QByteArray data = file.readAll();
        if (!data.startsWith("%PDF")) {
            QMessageBox::warning(this, windowTitle(), tr("Die Datei ist kein PDF-Dokument."));
            return;
        }
        m_advNewPdf[k] = data;
        m_advUnlink[k] = false;
    } else if (a == show) {
        const QByteArray data = !m_advNewPdf[k].isEmpty() ? m_advNewPdf[k] : Database::instance().documentData(docId);
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("VitaParms-Ansicht"));
        QDir().mkpath(dir);
        const QString path = QDir(dir).filePath(CareInfo::advanceName(k) + QStringLiteral(".pdf"));
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) {
            out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            out.write(data);
            out.close();
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    } else if (a == drop) {
        m_advNewPdf[k].clear();
        m_advUnlink[k] = true;
    }
    updateAdvanceUi();
}

bool PatientDialog::storeAdvancePdfs(Patient &p, QString *error)
{
    bool changed = false;
    Database &db = Database::instance();
    for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
        if (m_advNewPdf[k].isEmpty()) continue;
        MedicalDocument d;
        d.patientId = p.id;
        d.docDate = QDate::currentDate();
        d.category = tr("Vorsorge");
        d.title = CareInfo::advanceName(k);
        d.fileName = CareInfo::advanceName(k) + QStringLiteral(".pdf");
        d.sha256 = QCryptographicHash::hash(m_advNewPdf[k], QCryptographicHash::Sha256);
        if (const auto existing = db.findDocumentByHash(p.id, d.sha256)) {
            d.id = existing->id;
        } else {
            const PdfTools::Info info = PdfTools::analyze(m_advNewPdf[k]);
            d.pages = info.pages;
            d.fullText = info.text;
            if (!db.addDocument(d, m_advNewPdf[k], error)) return false;
        }
        CareInfo::AdvanceDoc a = CareInfo::advanceDoc(p, k);
        a.docId = d.id;
        CareInfo::setAdvanceDoc(p, k, a);
        m_advNewPdf[k].clear();
        changed = true;
    }
    if (changed && !db.savePatient(p, error)) return false;
    m_patient = p;
    return true;
}

void PatientDialog::updateCareEnabled()
{
    const auto *c = m_form->findChild<QComboBox *>(QStringLiteral("careLevelCombo"));
    const bool care = c && c->currentIndex() > 0;
    for (const char *n : { "careReviewEdit", "careReviewLabel" })
        if (auto *w = m_form->findChild<QWidget *>(QLatin1String(n))) w->setEnabled(care);
    const auto *sp = m_form->findChild<QSpinBox *>(QStringLiteral("disabilityDegreeSpin"));
    const bool card = sp && sp->value() > 0;
    for (const char *n : { "disabilityMarksBox", "disabilityMarksLabel", "disabilityUnlimitedCheck", "disabilityValidLabel" })
        if (auto *w = m_form->findChild<QWidget *>(QLatin1String(n))) w->setEnabled(card);
    const auto *unl = m_form->findChild<QCheckBox *>(QStringLiteral("disabilityUnlimitedCheck"));
    if (auto *w = m_form->findChild<QWidget *>(QStringLiteral("disabilityValidEdit")))
        w->setEnabled(card && !(unl && unl->isChecked()));
}

// INR-Zielbereich, Trinkmenge und Gewichtskontrolle
void PatientDialog::setupTargets()
{
    auto *lo = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("inrLowSpin"));
    auto *hi = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("inrHighSpin"));
    if (lo && hi) {
        const Inr::Range r = Inr::range(m_patient);
        lo->setValue(r.personal ? r.low : 0);
        hi->setValue(r.personal ? r.high : 0);
        auto upd = [this, lo, hi] {
            auto *info = m_form->findChild<QLabel *>(QStringLiteral("inrInfoLabel"));
            if (!info) return;
            QString t;
            if (lo->value() > 0 && hi->value() > lo->value())
                t = tr("Werte außerhalb von %1–%2 werden hervorgehoben.").arg(QLocale().toString(lo->value(), 'f', 1),
                                                                             QLocale().toString(hi->value(), 'f', 1));
            else if (lo->value() > 0 || hi->value() > 0)
                t = tr("Bitte beide Grenzen eintragen (unten kleiner als oben).");
            else
                t = tr("Nicht eingetragen: Bewertet wird gegen den Standard 2,0–3,0.");
            info->setText(QStringLiteral("<small>%1</small>").arg(t.toHtmlEscaped()));
        };
        connect(lo, qOverload<double>(&QDoubleSpinBox::valueChanged), this, upd);
        connect(hi, qOverload<double>(&QDoubleSpinBox::valueChanged), this, upd);
        upd();
    }
    if (auto *s = m_form->findChild<QSpinBox *>(QStringLiteral("fluidMinSpin"))) s->setValue(Fluid::minIntake(m_patient));
    if (auto *s = m_form->findChild<QSpinBox *>(QStringLiteral("fluidMaxSpin"))) s->setValue(Fluid::maxIntake(m_patient));
    if (auto *s = m_form->findChild<QDoubleSpinBox *>(QStringLiteral("weightWatchSpin"))) s->setValue(Fluid::weightWatch(m_patient));
}

// Biologisches Geschlecht (für Normwerte), Peak Flow: persönlicher Bestwert, Anzeige des Sollwerts
void PatientDialog::setupPeakFlow()
{
    auto *best = m_form->findChild<QSpinBox *>(QStringLiteral("peakFlowBestSpin"));
    if (auto *bio = m_form->findChild<QComboBox *>(QStringLiteral("bioSexCombo"))) {
        bio->clear();
        bio->addItem(tr("— bitte wählen —"), QString());
        bio->addItem(tr("männlich"), QStringLiteral("m"));
        bio->addItem(tr("weiblich"), QStringLiteral("w"));
        const QChar s = PeakFlow::biologicalSex(m_patient);
        bio->setCurrentIndex(std::max(0, bio->findData(s.isNull() ? QString() : QString(s))));
        m_bioSexTouched = PeakFlow::bioSexDiffers(m_patient);
        connect(bio, qOverload<int>(&QComboBox::activated), this, [this] { m_bioSexTouched = true; });
        connect(bio, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateBioSex(false); });
    }
    if (auto *c = m_form->findChild<QComboBox *>(QStringLiteral("sexCombo")))
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateBioSex(true); });
    if (best) {
        best->setValue(PeakFlow::personalBest(m_patient));
        connect(best, qOverload<int>(&QSpinBox::valueChanged), this, &PatientDialog::updatePeakFlowInfo);
    }
    if (auto *d = m_form->findChild<QDateEdit *>(QStringLiteral("birthDateEdit")))
        connect(d, &QDateEdit::dateChanged, this, &PatientDialog::updatePeakFlowInfo);
    updateBioSex(false);
}

// Das biologische Geschlecht folgt dem Feld „Geschlecht“, solange es nicht von Hand gewählt wurde
void PatientDialog::updateBioSex(bool fromSexField)
{
    auto *bio = m_form->findChild<QComboBox *>(QStringLiteral("bioSexCombo"));
    const QString sex = FormIO::readString(m_form, QStringLiteral("sexCombo"), m_patient.sex);
    const QChar derived = PeakFlow::sexFromField(sex);
    if (bio && fromSexField && !m_bioSexTouched && !derived.isNull()) {
        const QSignalBlocker block(bio);
        bio->setCurrentIndex(std::max(0, bio->findData(QString(derived))));
    }
    if (auto *info = m_form->findChild<QLabel *>(QStringLiteral("bioSexInfoLabel")); info && bio) {
        const QString v = bio->currentData().toString();
        QString t;
        if (v.isEmpty())
            t = tr("Bitte wählen – sonst gibt es keine geschlechtsabhängigen Normwerte (z. B. Peak-Flow-Sollwert).");
        else if (derived.isNull() || v.at(0) != derived)
            t = tr("Weicht vom Feld „Geschlecht“ ab und wird für Normwerte verwendet.");
        else
            t = tr("Folgt dem Feld „Geschlecht“ (Reiter „Person“). Nur ändern, wenn das biologische Geschlecht abweicht.");
        info->setText(QStringLiteral("<small>%1</small>").arg(t.toHtmlEscaped()));
    }
    updatePeakFlowInfo();
}

void PatientDialog::updatePeakFlowInfo()
{
    auto *label = m_form->findChild<QLabel *>(QStringLiteral("peakFlowInfoLabel"));
    if (!label) return;
    const Patient p = collectPatient();
    const std::optional<double> height = p.id ? Database::instance().latestHeight(p.id) : std::nullopt;
    const PeakFlow::Reference norm = PeakFlow::normReference(p, QDate::currentDate(), height);
    QStringList lines;
    lines << (norm.value ? tr("Sollwert heute: %1 l/min (%2).").arg(norm.value).arg(norm.basis) : norm.missing);
    const PeakFlow::Reference ref = PeakFlow::reference(p, QDate::currentDate(), height);
    if (ref.value)
        lines << tr("Warnhinweise beziehen sich auf den %1 (%2 l/min): gelb unter %3, rot unter %4 l/min.")
                     .arg(ref.source == PeakFlow::Source::PersonalBest ? tr("persönlichen Bestwert") : tr("Sollwert"))
                     .arg(ref.value).arg(int(std::ceil(ref.value * 0.8))).arg(int(std::ceil(ref.value * 0.5)));
    else
        lines << tr("Ohne Bezugswert gibt es keine Warnhinweise – bitte den persönlichen Bestwert eintragen.");
    label->setText(QStringLiteral("<small>%1</small>").arg(lines.join(QStringLiteral("<br>"))));
}

void PatientDialog::updateAmputationInfo()
{
    const Patient p = collectPatient();
    QString t;
    if (p.amputations.any())
        t = tr("Fehlender Körperanteil: %1 %. Der BMI wird mit dem auf den vollständigen "
               "Körper hochgerechneten Gewicht berechnet (Osterkamp 1995).")
                .arg(QLocale().toString(Bmi::missingPercent(p.amputations), 'f', 1));
    else
        t = tr("Bei fehlenden Gliedmaßen wird der BMI automatisch korrigiert.");
    FormIO::write(m_form, QStringLiteral("amputationInfoLabel"), t);
}

QStringList PatientDialog::medicationCells(const Medication &m) const
{
    const QLocale loc;
    QString name = m.name;
    if (!m.strength.isEmpty()) name += QLatin1Char(' ') + m.strength;
    if (!m.form.isEmpty())     name += QStringLiteral(" (") + m.form + QLatin1Char(')');
    return {
        name, m.substance,
        (m.morning + m.noon + m.evening + m.night).trimmed().isEmpty() && m.category != Medication::Regular
            ? QString()
            : (m.category == Medication::AsNeeded ? tr("bei Bedarf: ") : m.category == Medication::Emergency ? tr("im Notfall: ") : QString())
            + m.dosageScheme()
            + (m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit)
            + (MedSchedule::isInterval(m) ? QStringLiteral(" · ") + MedSchedule::describe(m) : QString())
            + (MedSchedule::wantsAppointments(m) ? QStringLiteral(" · 🔔") : QString()),
        m.reason,
        m.rhythmNote.isEmpty() ? m.notes
                               : (m.notes.isEmpty() ? m.rhythmNote : m.rhythmNote + QStringLiteral(" · ") + m.notes),
        m.startDate.isValid() ? loc.toString(m.startDate, QStringLiteral("dd.MM.yyyy")) : QString(),
        m.endDate.isValid() ? loc.toString(m.endDate, QStringLiteral("dd.MM.yyyy")) : QString(),
        MedSchedule::statusText(m),
    };
}

void PatientDialog::sortMedications(int column)
{
    if (column < 0 || column >= ColCount) return;
    if (column == m_sortColumn)
        m_sortOrder = m_sortOrder == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    else {
        m_sortColumn = column;
        m_sortOrder = Qt::AscendingOrder;
    }
    QHeaderView *hh = m_medTable->horizontalHeader();
    hh->setSortIndicatorShown(true);
    hh->setSortIndicator(m_sortColumn, m_sortOrder);
    QSettings s;
    s.setValue(QStringLiteral("medTable/sortColumn"), m_sortColumn);
    s.setValue(QStringLiteral("medTable/sortDescending"), m_sortOrder == Qt::DescendingOrder);
    fillMedicationTable();
}

void PatientDialog::fillMedicationTable()
{
    if (!m_medTable) return;
    bool showArchived = false;
    if (const auto v = FormIO::read(m_form, QStringLiteral("showArchivedCheck")))
        showArchived = v->toBool();

    const qint64 selected = currentMedicationIndex() >= 0 ? m_meds[currentMedicationIndex()].id : -1;
    m_medTable->setRowCount(0);
    const QLocale loc;
    m_medTable->clearSpans();
    // Reihenfolge: Regel-, Bedarfs-, Notfallmedikation – jeweils mit Zwischenüberschrift
    QList<int> order;
    for (int cat = 0; cat < Medication::CategoryCount; ++cat) {
        QList<int> idx;
        for (int i = 0; i < m_meds.size(); ++i)
            if (std::clamp(m_meds[i].category, 0, Medication::CategoryCount - 1) == cat && (!m_meds[i].archived || showArchived)) idx << i;
        if (idx.isEmpty()) continue;
        if (m_sortColumn >= 0) {
            // Nur innerhalb dieser Art sortieren; leere Werte stehen immer am Ende
            QCollator coll(loc);
            coll.setNumericMode(true);
            coll.setCaseSensitivity(Qt::CaseInsensitive);
            const int c = m_sortColumn;
            const bool desc = m_sortOrder == Qt::DescendingOrder;
            auto key = [&](int i) -> QString {
                const Medication &m = m_meds[i];
                if (c == ColStart) return m.startDate.isValid() ? m.startDate.toString(Qt::ISODate) : QString();
                if (c == ColEnd) return m.endDate.isValid() ? m.endDate.toString(Qt::ISODate) : QString();
                return medicationCells(m).value(c).trimmed();
            };
            std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
                const QString ka = key(a), kb = key(b);
                if (ka.isEmpty() != kb.isEmpty()) return kb.isEmpty();   // Leere ans Ende
                const int r = coll.compare(ka, kb);
                return desc ? r > 0 : r < 0;
            });
        }
        order << -1 - cat;   // Kennung der Überschrift
        order << idx;
    }
    for (const int i : std::as_const(order)) {
        if (i < 0) {
            const int cat = -1 - i;
            const int row = m_medTable->rowCount();
            m_medTable->insertRow(row);
            int n = 0;
            for (const Medication &x : std::as_const(m_meds))
                if (std::clamp(x.category, 0, Medication::CategoryCount - 1) == cat && (!x.archived || showArchived)) ++n;
            auto *h = new QTableWidgetItem(QStringLiteral("%1 (%2)").arg(MedSchedule::categoryName(cat)).arg(n));
            QFont f = h->font();
            f.setBold(true);
            h->setFont(f);
            h->setFlags(Qt::ItemIsEnabled);
            const QColor base = cat == Medication::Emergency ? QColor(0xD5, 0x2B, 0x1E)
                              : cat == Medication::AsNeeded ? QColor(0xE6, 0x9F, 0x00)
                              : cat == Medication::Other ? QColor(0x88, 0x88, 0x88) : QColor(0x00, 0x72, 0xB2);
            QColor bg = base;
            bg.setAlpha(45);
            h->setBackground(bg);
            h->setToolTip(MedSchedule::categoryHint(cat));
            m_medTable->setItem(row, 0, h);
            for (int c = 1; c < ColCount; ++c) {
                auto *e = new QTableWidgetItem;
                e->setFlags(Qt::ItemIsEnabled);
                e->setBackground(bg);
                m_medTable->setItem(row, c, e);
            }
            continue;
        }
        const Medication &m = m_meds[i];
        const int row = m_medTable->rowCount();
        m_medTable->insertRow(row);
        const QStringList cells = medicationCells(m);
        for (int c = 0; c < ColCount; ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            it->setData(Qt::UserRole, i);
            if (m.archived) {
                QFont f = it->font();
                f.setItalic(true);
                it->setFont(f);
                it->setForeground(m_medTable->palette().color(QPalette::Disabled, QPalette::Text));
            }
            if (c == 0) {
                QStringList tip;
                if (!m.pzn.isEmpty()) tip << tr("PZN %1").arg(m.pzn);
                if (m.extra.contains(QLatin1String(Leaflet::kLeafletDocKey))) {
                    tip << tr("Beipackzettel in der Krankenakte gespeichert");
                    it->setText(it->text() + QStringLiteral("  📄"));
                }
                if (m.archived && !MedSchedule::stopReason(m).isEmpty())
                    tip << tr("Abgesetzt: %1").arg(MedSchedule::stopReason(m));
                if (MedSchedule::wantsAppointments(m))
                    tip << tr("Erinnerung: %1").arg(MedSchedule::reminderText(m));
                it->setToolTip(tip.join(QLatin1Char('\n')));
            }
            m_medTable->setItem(row, c, it);
        }
        if (m.id == selected && selected > 0)
            m_medTable->selectRow(row);
    }
    m_medTable->resizeColumnsToContents();
    updateMedicationButtons();
}

int PatientDialog::currentMedicationIndex() const
{
    if (!m_medTable) return -1;
    const auto sel = m_medTable->selectedItems();
    if (sel.isEmpty()) return -1;
    const QVariant v = sel.first()->data(Qt::UserRole);
    return v.isValid() ? v.toInt() : -1;
}

void PatientDialog::updateMedicationButtons()
{
    const int i = currentMedicationIndex();
    auto en = [this](const char *n, bool on) {
        if (auto *b = m_form->findChild<QAbstractButton *>(QLatin1String(n))) b->setEnabled(on);
    };
    en("editMedicationButton", i >= 0);
    en("deleteMedicationButton", i >= 0);
    en("archiveMedicationButton", i >= 0);
    en("leafletMedicationButton", i >= 0);
    if (auto *b = m_form->findChild<QAbstractButton *>(QStringLiteral("archiveMedicationButton")))
        b->setText(i >= 0 && m_meds[i].archived ? tr("&Wieder aufnehmen") : tr("&Absetzen…"));
}

void PatientDialog::addMedication()
{
    MedicationDialog d(Medication{}, this, m_patient.id);
    if (d.exec() != QDialog::Accepted) return;
    m_meds << d.medication();
    fillMedicationTable();
}

void PatientDialog::editMedication()
{
    const int i = currentMedicationIndex();
    if (i < 0) return;
    MedicationDialog d(m_meds[i], this, m_patient.id);
    if (d.exec() != QDialog::Accepted) return;
    m_meds[i] = d.medication();
    fillMedicationTable();
}

void PatientDialog::searchMedications()
{
    MedSearchDialog d(m_patient, m_meds, DrugClass::Btm, this);   // aktueller (auch ungespeicherter) Plan
    d.exec();
}

void PatientDialog::showLeaflet()
{
    const int i = currentMedicationIndex();
    if (i < 0) return;
    LeafletDialog d(m_meds[i], m_patient.id, this);
    d.exec();
    if (d.medicationChanged()) {
        m_meds[i] = d.medication();   // PZN/Verknüpfung – wird mit dem Stammblatt gespeichert
        fillMedicationTable();
    }
}

void PatientDialog::archiveMedication()
{
    const int i = currentMedicationIndex();
    if (i < 0) return;
    Medication &m = m_meds[i];
    if (m.archived) {                       // wieder aufnehmen
        MedSchedule::reactivate(m);
        fillMedicationTable();
        return;
    }
    // Absetzen: Datum (auch in der Zukunft) und Grund erfragen
    const bool planned = MedSchedule::stopPlanned(m);
    QDialog d(this);
    d.setWindowTitle(tr("Medikament absetzen"));
    auto *form = new QFormLayout(&d);
    auto *head = new QLabel(&d);
    head->setWordWrap(true);
    head->setMinimumWidth(420);
    form->addRow(head);
    auto *date = new QDateEdit(&d);
    date->setCalendarPopup(true);
    date->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    if (m.startDate.isValid()) date->setMinimumDate(std::min(m.startDate, QDate::currentDate()));
    date->setDate(planned ? MedSchedule::stopDate(m) : QDate::currentDate());
    form->addRow(tr("Abgesetzt &am"), date);
    auto *reason = new QLineEdit(MedSchedule::stopReason(m), &d);
    reason->setPlaceholderText(tr("z. B. Nebenwirkung, Therapie beendet, ersetzt durch …"));
    form->addRow(tr("&Grund"), reason);
    auto updateHead = [&] {
        const QLocale loc;
        if (date->date() > QDate::currentDate())
            head->setText(tr("<b>%1</b> wird bis einschließlich %2 weiter gegeben und am %3 automatisch "
                             "abgesetzt und ins Archiv verschoben.")
                              .arg(m.name.toHtmlEscaped(),
                                   loc.toString(date->date().addDays(-1), QStringLiteral("dd.MM.yyyy")),
                                   loc.toString(date->date(), QStringLiteral("dd.MM.yyyy"))));
        else
            head->setText(tr("<b>%1</b> wird nicht mehr eingenommen und ins Archiv verschoben. "
                             "Die Historie bleibt erhalten.").arg(m.name.toHtmlEscaped()));
    };
    connect(date, &QDateEdit::dateChanged, &d, updateHead);
    updateHead();
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Absetzen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    bool cancelStop = false;
    if (planned) {
        auto *undo = bb->addButton(tr("Absetzen &zurücknehmen"), QDialogButtonBox::ResetRole);
        connect(undo, &QPushButton::clicked, &d, [&] { cancelStop = true; d.accept(); });
    }
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(bb);
    if (d.exec() != QDialog::Accepted) return;
    if (cancelStop) MedSchedule::cancelPlannedStop(m);
    else MedSchedule::discontinue(m, date->date(), reason->text());
    fillMedicationTable();
}

void PatientDialog::deleteMedication()
{
    const int i = currentMedicationIndex();
    if (i < 0) return;
    if (QMessageBox::question(this, windowTitle(),
            tr("„%1“ endgültig löschen?\n\nTipp: Abgesetzte Medikamente besser archivieren – "
               "dann bleibt die Historie erhalten.").arg(m_meds[i].name)) != QMessageBox::Yes)
        return;
    if (m_meds[i].id) { m_deletedMedIds << m_meds[i].id; m_removedMeds << m_meds[i]; }
    m_meds.removeAt(i);
    fillMedicationTable();
}

void PatientDialog::printMedication()
{
    QList<Medication> active;
    for (const Medication &m : m_meds)
        if (!m.archived) active << m;

    QMenu menu(this);
    QAction *print = menu.addAction(tr("Drucken…"));
    QAction *pdf   = menu.addAction(tr("Als PDF speichern…"));
    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;
    ReportPrinter::medicationPlan(this, collectPatient(), active,
                                  chosen == pdf ? ReportPrinter::Target::Pdf
                                                : ReportPrinter::Target::Printer);
    Q_UNUSED(print)
}

void PatientDialog::editAdmission()
{
    Patient p = collectPatient();
    VitalRecord r = m_admission.value_or(VitalRecord{});
    r.isAdmission = true;
    VitalDialog d(p, r, VitalDialog::Mode::Admission, this);
    if (d.exec() != QDialog::Accepted) return;
    m_admission = d.record();
    m_admissionChanged = true;
    updateAdmissionLabel();
}

void PatientDialog::updateAdmissionLabel()
{
    QString t;
    if (!m_admission) {
        t = tr("Noch keine Aufnahmewerte erfasst.");
    } else {
        const VitalRecord &r = *m_admission;
        QStringList parts;
        parts << tr("<b>Aufnahme am %1</b>").arg(QLocale().toString(r.measuredAt, QStringLiteral("dd.MM.yyyy HH:mm")));
        QStringList vals;
        if (r.systolic || r.diastolic)
            vals << tr("RR %1/%2 mmHg").arg(r.systolic ? QString::number(*r.systolic) : QStringLiteral("–"),
                                            r.diastolic ? QString::number(*r.diastolic) : QStringLiteral("–"));
        for (VitalKey k : { VitalKey::Pulse, VitalKey::Temperature, VitalKey::SpO2, VitalKey::PeakFlow, VitalKey::Glucose,
                            VitalKey::Weight, VitalKey::Height, VitalKey::Bmi, VitalKey::Pain }) {
            const QString s = VitalDefs::formatted(r, k, true);
            if (!s.isEmpty()) vals << VitalDefs::shortLabel(k) + QLatin1Char(' ') + s;
        }
        parts << vals.join(QStringLiteral(" · "));
        if (!r.observations.trimmed().isEmpty())
            parts << r.observations.toHtmlEscaped();
        t = parts.join(QStringLiteral("<br>"));
    }
    FormIO::write(m_form, QStringLiteral("admissionLabel"), t);
    if (auto *b = m_form->findChild<QAbstractButton *>(QStringLiteral("admissionButton")))
        b->setText(m_admission ? tr("Aufnahmewerte &bearbeiten…") : tr("Aufnahmewerte &erfassen…"));
}

void PatientDialog::setupWoundWidgets()
{
    // Abschnitt direkt unter „Vitalwerte bei Aufnahme“ einfügen (auch bei angepassten Formularen,
    // solange die Gruppe „admissionGroup“ in einem Box-Layout steht)
    if (Edition::light()) return;    // VitaParms light: keine Wunddokumentation
    auto *adm = m_form->findChild<QWidget *>(QStringLiteral("admissionGroup"));
    auto *box = adm && adm->parentWidget() ? qobject_cast<QBoxLayout *>(adm->parentWidget()->layout()) : nullptr;
    if (!box) return;
    auto *g = new QGroupBox(tr("Wunden bei Aufnahme"), adm->parentWidget());
    g->setObjectName(QStringLiteral("admissionWoundGroup"));
    auto *v = new QVBoxLayout(g);
    m_woundLabel = new QLabel(g);
    m_woundLabel->setObjectName(QStringLiteral("admissionWoundLabel"));
    m_woundLabel->setWordWrap(true);
    m_woundLabel->setTextFormat(Qt::RichText);
    v->addWidget(m_woundLabel);
    auto *row = new QHBoxLayout;
    m_woundAdd = new QPushButton(tr("Wunde bei Aufnahme e&rfassen…"), g);
    m_woundAdd->setObjectName(QStringLiteral("admissionWoundButton"));
    m_woundOpen = new QPushButton(tr("Wunddokumentation…"), g);
    row->addWidget(m_woundAdd);
    row->addWidget(m_woundOpen);
    row->addStretch(1);
    v->addLayout(row);
    box->insertWidget(box->indexOf(adm) + 1, g);
    connect(m_woundAdd, &QPushButton::clicked, this, &PatientDialog::addAdmissionWound);
    connect(m_woundOpen, &QPushButton::clicked, this, [this] {
        WoundsDialog d(m_patient, this);
        d.exec();
        updateWoundLabel();
    });
    updateWoundLabel();
}

void PatientDialog::updateWoundLabel()
{
    if (!m_woundLabel) return;
    const bool saved = m_patient.id != 0;
    m_woundAdd->setEnabled(saved);
    m_woundOpen->setEnabled(saved);
    if (!saved) {
        m_woundLabel->setText(tr("Wunden lassen sich erfassen, sobald das Stammblatt einmal gespeichert ist."));
        return;
    }
    QStringList l;
    for (const Wounds::Wound &w : Wounds::wounds(m_patient.id))
        if (w.onAdmission) l << QStringLiteral("• ") + Wounds::woundSummary(w).toHtmlEscaped();
    m_woundLabel->setText(l.isEmpty() ? tr("Keine Wunden bei Aufnahme erfasst.") : l.join(QStringLiteral("<br>")));
}

void PatientDialog::addAdmissionWound()
{
    if (!m_patient.id) return;
    Wounds::Wound w;
    w.patientId = m_patient.id;
    w.onAdmission = true;
    w.admissionDate = m_admission ? m_admission->measuredAt.date() : QDate::currentDate();
    WoundDialog d(w, this);
    if (d.exec() != QDialog::Accepted) return;
    updateWoundLabel();
    // Aufnahmebefund gleich mit einer ersten Wundbeurteilung festhalten
    if (QMessageBox::question(this, tr("Wunde bei Aufnahme"),
                              tr("Jetzt die erste Wundbeurteilung (Aufnahmebefund mit Größe, Wundgrund, Exsudat …) erfassen?"))
        == QMessageBox::Yes) {
        Wounds::Entry e;
        e.woundId = d.wound().id;
        e.kind = Wounds::Assessment;
        e.at = m_admission ? m_admission->measuredAt : QDateTime::currentDateTime();
        e.grade = d.wound().grade;
        WoundEntryDialog ed(d.wound(), e, this);
        ed.exec();
        updateWoundLabel();
    }
}

void PatientDialog::accept()
{
    Patient p = collectPatient();
    if (p.lastName.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte mindestens den Namen angeben."));
        if (auto *e = m_form->findChild<QWidget *>(QStringLiteral("lastNameEdit"))) e->setFocus();
        return;
    }
    // Dubletten-Warnung bei Neuanlage
    if (p.id == 0) {
        for (const Patient &o : Database::instance().patients()) {
            if (o.lastName.compare(p.lastName, Qt::CaseInsensitive) == 0
                && o.firstName.compare(p.firstName, Qt::CaseInsensitive) == 0
                && (!o.birthDate.isValid() || !p.birthDate.isValid() || o.birthDate == p.birthDate)) {
                if (QMessageBox::question(this, windowTitle(),
                        tr("Es gibt bereits einen Benutzer „%1“. Trotzdem neu anlegen?")
                            .arg(o.displayName())) != QMessageBox::Yes)
                    return;
                break;
            }
        }
    }

    QString err;
    Database &db = Database::instance();

    // Amputationsangaben geändert? Dann ggf. BMI bestehender Messungen neu berechnen.
    bool recompute = false;
    if (p.id != 0 && Bmi::encode(p.amputations) != Bmi::encode(m_patient.amputations)) {
        recompute = QMessageBox::question(this, windowTitle(),
            tr("Die Angaben zu fehlenden Gliedmaßen wurden geändert.\n\n"
               "Sollen die BMI-Werte aller bisherigen Messungen mit den neuen Angaben neu "
               "berechnet werden?\n\n„Nein“ wählen, wenn sich der Körperzustand tatsächlich "
               "erst jetzt geändert hat – dann bleiben die alten Werte historisch korrekt."))
            == QMessageBox::Yes;
    }

    if (!db.savePatient(p, &err)
        || !db.saveMedications(p.id, m_meds, m_deletedMedIds, &err)) {
        QMessageBox::critical(this, windowTitle(), tr("Speichern fehlgeschlagen:\n%1").arg(err));
        return;
    }
    // Vorsorgedokumente: neu gewählte PDF in die Krankenakte legen und verknüpfen
    if (!storeAdvancePdfs(p, &err)) {
        QMessageBox::critical(this, windowTitle(), tr("PDF konnte nicht gespeichert werden:\n%1").arg(err));
        return;
    }
    // Medikamente mit „An Einnahme erinnern“ als Terminserien in die Terminliste übernehmen
    MedSchedule::syncAppointments(p.id, m_meds, m_removedMeds);
    m_deletedMedIds.clear();
    if (m_admission && m_admissionChanged) {
        VitalRecord r = *m_admission;
        r.patientId = p.id;
        // BMI mit ggf. geänderten Amputationsangaben neu berechnen
        if (r.weight)
            r.bmi = Bmi::compute(r.weight, r.height ? r.height : db.latestHeight(p.id), p.amputations);
        if (!db.saveVital(r, &err)) {
            QMessageBox::critical(this, windowTitle(), tr("Aufnahmewerte konnten nicht gespeichert werden:\n%1").arg(err));
            return;
        }
        m_admission = r;
        m_admissionChanged = false;
    }
    if (recompute) {
        const auto fallback = db.latestHeight(p.id);
        for (VitalRecord r : db.vitals(p.id)) {
            if (!r.weight) continue;
            r.bmi = Bmi::compute(r.weight, r.height ? r.height : fallback, p.amputations);
            db.saveVital(r, &err);
        }
    }
    m_patient = p;
    AppSettings::setWindowState(QStringLiteral("patientdialog"), saveGeometry());
    QDialog::accept();
}

// ================================================================ Kontakte

void PatientDialog::setupContacts()
{
    m_contactTable = m_form->findChild<QTableWidget *>(QStringLiteral("contactTable"));
    if (!m_contactTable) return;
    m_contacts = Contacts::list(m_patient);
    m_contactTable->setColumnCount(3);
    m_contactTable->setHorizontalHeaderLabels({ tr("Art"), tr("Name"), tr("Telefon") });
    m_contactTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contactTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_contactTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_contactTable->setAlternatingRowColors(true);
    m_contactTable->verticalHeader()->hide();
    m_contactTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_contactTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_contactTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_contactTable->setWordWrap(false);
    connect(m_contactTable, &QTableWidget::cellDoubleClicked, this, [this] { editContact(); });
    connect(m_contactTable, &QTableWidget::itemSelectionChanged, this, [this] {
        const bool sel = !m_contactTable->selectedItems().isEmpty();
        for (const char *n : { "editContactButton", "deleteContactButton" })
            if (auto *b = m_form->findChild<QPushButton *>(QLatin1String(n))) b->setEnabled(sel);
    });
    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("addContactButton")))
        connect(b, &QPushButton::clicked, this, &PatientDialog::addContact);
    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("editContactButton")))
        connect(b, &QPushButton::clicked, this, &PatientDialog::editContact);
    if (auto *b = m_form->findChild<QPushButton *>(QStringLiteral("deleteContactButton")))
        connect(b, &QPushButton::clicked, this, &PatientDialog::deleteContact);
    if (auto *e = m_form->findChild<QLineEdit *>(QStringLiteral("contactSearchEdit")))
        connect(e, &QLineEdit::textChanged, this, &PatientDialog::fillContactTable);
    fillContactTable();
}

void PatientDialog::fillContactTable()
{
    if (!m_contactTable) return;
    const QString selUid = [this] {
        const auto r = m_contactTable->selectionModel() ? m_contactTable->selectionModel()->selectedRows() : QModelIndexList();
        return r.isEmpty() ? QString() : m_contactTable->item(r.first().row(), 0)->data(Qt::UserRole).toString();
    }();
    QString filter;
    if (auto *e = m_form->findChild<QLineEdit *>(QStringLiteral("contactSearchEdit"))) filter = e->text().trimmed();
    const QStringList words = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_contactTable->setRowCount(0);
    int selRow = -1;
    for (const Contacts::Contact &c : std::as_const(m_contacts)) {
        const QString hay = c.searchText();
        bool match = true;
        for (const QString &w : words) if (!hay.contains(w, Qt::CaseInsensitive)) { match = false; break; }
        if (!match) continue;
        const int r = m_contactTable->rowCount();
        m_contactTable->insertRow(r);
        auto *kind = new QTableWidgetItem(c.kindName());
        kind->setData(Qt::UserRole, c.uid);
        QString name = c.title();
        if (!c.person.isEmpty()) name += QStringLiteral(" (") + c.person + QLatin1Char(')');
        if (!c.city.isEmpty()) name += QStringLiteral(", ") + c.city;
        auto *nameItem = new QTableWidgetItem(name);
        auto *tel = new QTableWidgetItem(c.phone.isEmpty() ? c.mobile : c.phone);
        const QString tip = c.summary();
        for (QTableWidgetItem *it : { kind, nameItem, tel }) it->setToolTip(tip);
        m_contactTable->setItem(r, 0, kind);
        m_contactTable->setItem(r, 1, nameItem);
        m_contactTable->setItem(r, 2, tel);
        if (c.uid == selUid) selRow = r;
    }
    if (selRow >= 0) m_contactTable->selectRow(selRow);
    const bool sel = selRow >= 0;
    for (const char *n : { "editContactButton", "deleteContactButton" })
        if (auto *b = m_form->findChild<QPushButton *>(QLatin1String(n))) b->setEnabled(sel);
}

void PatientDialog::addContact()
{
    Contacts::Contact c;
    // sinnvolle Vorgabe: was noch fehlt (Krankenkasse, Hausarzt), sonst Facharzt
    bool hasIns = false, hasGp = false;
    for (const auto &x : std::as_const(m_contacts)) {
        hasIns |= x.kind == Contacts::Insurance;
        hasGp |= x.kind == Contacts::FamilyDoctor;
    }
    c.kind = !hasIns ? Contacts::Insurance : !hasGp ? Contacts::FamilyDoctor : Contacts::Specialist;
    ContactDialog d(c, this);
    if (d.exec() != QDialog::Accepted) return;
    const Contacts::Contact n = d.contact();
    m_contacts << n;
    Contacts::sort(m_contacts);
    if (auto *e = m_form->findChild<QLineEdit *>(QStringLiteral("contactSearchEdit"))) e->clear();
    fillContactTable();
    for (int r = 0; r < m_contactTable->rowCount(); ++r)
        if (m_contactTable->item(r, 0)->data(Qt::UserRole).toString() == n.uid) m_contactTable->selectRow(r);
}

void PatientDialog::editContact()
{
    const auto rows = m_contactTable ? m_contactTable->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty()) return;
    const QString uid = m_contactTable->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
    for (Contacts::Contact &c : m_contacts) {
        if (c.uid != uid) continue;
        ContactDialog d(c, this);
        if (d.exec() != QDialog::Accepted) return;
        c = d.contact();
        break;
    }
    Contacts::sort(m_contacts);
    fillContactTable();
}

void PatientDialog::deleteContact()
{
    const auto rows = m_contactTable ? m_contactTable->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty()) return;
    const QString uid = m_contactTable->item(rows.first().row(), 0)->data(Qt::UserRole).toString();
    const QString name = m_contactTable->item(rows.first().row(), 1)->text();
    if (QMessageBox::question(this, tr("Kontakt entfernen"), tr("„%1“ aus den Kontakten entfernen?").arg(name))
        != QMessageBox::Yes)
        return;
    m_contacts.erase(std::remove_if(m_contacts.begin(), m_contacts.end(),
                                    [&](const Contacts::Contact &c) { return c.uid == uid; }), m_contacts.end());
    fillContactTable();
}
