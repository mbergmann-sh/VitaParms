// SPDX-License-Identifier: MIT
#include "vaccinationdialog.h"

#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QComboBox *editableCombo(const QStringList &items, const QString &text, QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->addItems(items);
    c->setCurrentIndex(-1);
    c->setEditText(text);
    c->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    c->completer()->setFilterMode(Qt::MatchContains);
    c->completer()->setCompletionMode(QCompleter::PopupCompletion);
    c->setMinimumContentsLength(28);
    return c;
}
} // namespace

VaccinationDialog::VaccinationDialog(const Vaccinations::Vaccination &v, const QList<Vaccinations::Vaccination> &existing,
                                     const QStringList &doctors, QWidget *parent)
    : QDialog(parent), m_v(v)
{
    setObjectName(QStringLiteral("VaccinationDialog"));
    setWindowTitle(v.uid.isEmpty() ? tr("Impfung eintragen") : tr("Impfung ändern"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Übernehmen Sie die Angaben aus dem Impfpass bzw. vom Aufkleber des Impfstoffs."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *form = new QFormLayout;
    m_date = new QDateEdit(v.date.isValid() ? v.date : QDate::currentDate(), this);
    m_date->setObjectName(QStringLiteral("vaccDate"));
    m_date->setCalendarPopup(true);
    m_date->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    m_date->setMaximumDate(QDate::currentDate().addDays(1));
    m_against = editableCombo(Vaccinations::againstSuggestions(existing), v.against, this);
    m_against->setObjectName(QStringLiteral("vaccAgainst"));
    m_against->lineEdit()->setPlaceholderText(tr("z. B. Tetanus, Diphtherie, Pertussis"));
    m_product = editableCombo(Vaccinations::productSuggestions(existing), v.product, this);
    m_product->setObjectName(QStringLiteral("vaccProduct"));
    m_product->lineEdit()->setPlaceholderText(tr("Handelsname, z. B. Boostrix"));
    m_batch = new QLineEdit(v.batch, this);
    m_batch->setObjectName(QStringLiteral("vaccBatch"));
    m_batch->setPlaceholderText(tr("Ch.-B. vom Aufkleber"));
    QStringList docs = doctors;
    for (const auto &x : existing) if (!x.doctor.trimmed().isEmpty() && !docs.contains(x.doctor.trimmed())) docs << x.doctor.trimmed();
    m_doctor = editableCombo(docs, v.doctor, this);
    m_doctor->setObjectName(QStringLiteral("vaccDoctor"));
    m_note = new QLineEdit(v.note, this);
    m_note->setObjectName(QStringLiteral("vaccNote"));
    m_note->setPlaceholderText(tr("z. B. 2. Dosis, Auffrischung, Reaktion"));
    form->addRow(tr("&Datum:"), m_date);
    form->addRow(tr("Impfung &gegen:"), m_against);
    form->addRow(tr("&Impfstoff (Präparat):"), m_product);
    form->addRow(tr("&Chargennummer:"), m_batch);
    form->addRow(tr("&Arzt / Praxis:"), m_doctor);
    form->addRow(tr("&Bemerkung:"), m_note);
    lay->addLayout(form);
    m_msg = new QLabel(this);
    m_msg->setStyleSheet(QStringLiteral("color: #b3261e;"));
    lay->addWidget(m_msg);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Übernehmen"));
    connect(bb, &QDialogButtonBox::accepted, this, &VaccinationDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    for (QComboBox *c : { m_against, m_product })
        connect(c, &QComboBox::editTextChanged, m_msg, [this] { m_msg->clear(); });
    resize(520, sizeHint().height());
}

Vaccinations::Vaccination VaccinationDialog::vaccination() const
{
    Vaccinations::Vaccination v = m_v;
    if (v.uid.isEmpty()) v.uid = Vaccinations::newUid();
    v.date = m_date->date();
    v.against = m_against->currentText().trimmed();
    v.product = m_product->currentText().trimmed();
    v.batch = m_batch->text().trimmed();
    v.doctor = m_doctor->currentText().trimmed();
    v.note = m_note->text().trimmed();
    return v;
}

void VaccinationDialog::accept()
{
    if (m_against->currentText().trimmed().isEmpty() && m_product->currentText().trimmed().isEmpty()) {
        m_msg->setText(tr("Bitte angeben, wogegen geimpft wurde (oder den Impfstoff)."));
        m_against->setFocus();
        return;
    }
    QDialog::accept();
}
