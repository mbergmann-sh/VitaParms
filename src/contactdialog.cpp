// SPDX-License-Identifier: MIT
#include "contactdialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

ContactDialog::ContactDialog(const Contacts::Contact &c, QWidget *parent)
    : QDialog(parent), m_contact(c)
{
    setWindowTitle(c.name.isEmpty() && c.phone.isEmpty() ? tr("Neuer Kontakt") : tr("Kontakt bearbeiten"));
    setObjectName(QStringLiteral("contactDialog"));
    auto *lay = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto edit = [this](const char *obj, const QString &text, const QString &placeholder = {}) {
        auto *e = new QLineEdit(text, this);
        e->setObjectName(QLatin1String(obj));
        e->setPlaceholderText(placeholder);
        return e;
    };
    m_kind = new QComboBox(this);
    m_kind->setObjectName(QStringLiteral("contactKindCombo"));
    for (int k : Contacts::kindOrder()) m_kind->addItem(Contacts::kindName(k), k);
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(c.kind)));
    form->addRow(tr("&Art"), m_kind);

    m_name = edit("contactNameEdit", c.name);
    m_nameLabel = new QLabel(this);
    m_nameLabel->setBuddy(m_name);
    form->addRow(m_nameLabel, m_name);
    m_specialty = edit("contactSpecialtyEdit", c.specialty);
    m_specialtyLabel = new QLabel(this);
    m_specialtyLabel->setBuddy(m_specialty);
    form->addRow(m_specialtyLabel, m_specialty);
    m_person = edit("contactPersonEdit", c.person);
    m_personLabel = new QLabel(this);
    m_personLabel->setBuddy(m_person);
    form->addRow(m_personLabel, m_person);
    m_insuranceNo = edit("contactInsuranceNoEdit", c.insuranceNo, tr("z. B. A123456789 (auf der Versichertenkarte)"));
    m_insuranceLabel = new QLabel(tr("&Versichertennummer"), this);
    m_insuranceLabel->setBuddy(m_insuranceNo);
    form->addRow(m_insuranceLabel, m_insuranceNo);

    m_street = edit("contactStreetEdit", c.street);
    form->addRow(tr("&Straße, Nr."), m_street);
    auto *zipCity = new QHBoxLayout;
    m_zip = edit("contactZipEdit", c.zip);
    m_zip->setMaximumWidth(90);
    m_city = edit("contactCityEdit", c.city);
    zipCity->addWidget(m_zip);
    zipCity->addWidget(m_city, 1);
    auto *zipLabel = new QLabel(tr("&PLZ, Ort"), this);
    zipLabel->setBuddy(m_zip);
    form->addRow(zipLabel, zipCity);
    m_phone = edit("contactPhoneEdit", c.phone);
    form->addRow(tr("&Telefon"), m_phone);
    m_mobile = edit("contactMobileEdit", c.mobile);
    form->addRow(tr("&Mobil"), m_mobile);

    // E-Mail und Website mit Knopf zum Öffnen
    auto withButton = [this](QLineEdit *e, const QString &tip, auto urlOf) {
        auto *row = new QHBoxLayout;
        row->addWidget(e, 1);
        auto *b = new QToolButton(this);
        b->setText(QStringLiteral("↗"));
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [e, urlOf] {
            if (!e->text().trimmed().isEmpty()) QDesktopServices::openUrl(urlOf(e->text().trimmed()));
        });
        row->addWidget(b);
        return row;
    };
    m_email = edit("contactEmailEdit", c.email, tr("name@beispiel.de"));
    auto *emailLabel = new QLabel(tr("&E-Mail"), this);
    emailLabel->setBuddy(m_email);
    form->addRow(emailLabel, withButton(m_email, tr("E-Mail schreiben"), [](const QString &t) {
        return QUrl(QStringLiteral("mailto:") + t); }));
    m_website = edit("contactWebsiteEdit", c.website, tr("www.beispiel.de"));
    auto *webLabel = new QLabel(tr("&Website"), this);
    webLabel->setBuddy(m_website);
    form->addRow(webLabel, withButton(m_website, tr("Website öffnen"), [](const QString &t) {
        return QUrl::fromUserInput(t); }));
    m_note = new QPlainTextEdit(c.note, this);
    m_note->setObjectName(QStringLiteral("contactNoteEdit"));
    m_note->setMaximumHeight(70);
    m_note->setPlaceholderText(tr("z. B. Sprechzeiten, Fax, nächster Termin, Vertretung"));
    form->addRow(tr("&Notiz"), m_note);
    lay->addLayout(form);

    m_msg = new QLabel(this);
    m_msg->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_msg->setVisible(false);
    lay->addWidget(m_msg);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("&Übernehmen"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(bb, &QDialogButtonBox::accepted, this, &ContactDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_kind, qOverload<int>(&QComboBox::currentIndexChanged), this, &ContactDialog::updateKind);
    for (QLineEdit *e : findChildren<QLineEdit *>())        // Hinweis verschwindet bei der nächsten Eingabe
        connect(e, &QLineEdit::textChanged, m_msg, &QLabel::hide);
    updateKind();
    setMinimumWidth(520);
    m_name->setFocus();
}

void ContactDialog::updateKind()
{
    const int k = m_kind->currentData().toInt();
    using namespace Contacts;
    m_nameLabel->setText(k == Insurance ? tr("&Krankenkasse *") : k == Emergency || k == Other ? tr("&Name *")
                       : k == Clinic ? tr("&Klinik *") : k == Pharmacy ? tr("&Apotheke *")
                       : k == CareService ? tr("&Pflegedienst *") : tr("&Praxis / Ärztin, Arzt *"));
    m_specialtyLabel->setText(k == Clinic ? tr("&Abteilung / Station") : tr("&Fachrichtung"));
    m_personLabel->setText(k == Emergency || k == Other ? tr("&Beziehung") : tr("&Ansprechpartner"));
    m_person->setPlaceholderText(k == Emergency ? tr("z. B. Ehemann, Tochter, Nachbarin") : QString());
    m_specialty->setPlaceholderText(k == Specialist ? tr("z. B. Kardiologie, Augenheilkunde")
                                  : k == Clinic ? tr("z. B. Innere Medizin, Station 4b") : QString());
    const bool spec = k == Specialist || k == Clinic || k == FamilyDoctor;
    m_specialty->setVisible(spec);
    m_specialtyLabel->setVisible(spec);
    m_insuranceNo->setVisible(k == Insurance);
    m_insuranceLabel->setVisible(k == Insurance);
    adjustSize();
}

Contacts::Contact ContactDialog::contact() const
{
    Contacts::Contact c = m_contact;
    c.kind = m_kind->currentData().toInt();
    c.name = m_name->text().trimmed();
    c.specialty = m_specialty->isVisibleTo(this) ? m_specialty->text().trimmed() : QString();
    c.person = m_person->text().trimmed();
    c.insuranceNo = c.kind == Contacts::Insurance ? m_insuranceNo->text().trimmed() : QString();
    c.street = m_street->text().trimmed();
    c.zip = m_zip->text().trimmed();
    c.city = m_city->text().trimmed();
    c.phone = m_phone->text().trimmed();
    c.mobile = m_mobile->text().trimmed();
    c.email = m_email->text().trimmed();
    c.website = m_website->text().trimmed();
    c.note = m_note->toPlainText().trimmed();
    if (c.uid.isEmpty()) c.uid = Contacts::newUid();
    return c;
}

void ContactDialog::accept()
{
    const Contacts::Contact c = contact();
    QString problem;
    if (c.name.isEmpty()) problem = tr("Bitte einen Namen eintragen.");
    else if (!c.email.isEmpty() && !QRegularExpression(QStringLiteral("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$")).match(c.email).hasMatch())
        problem = tr("Die E-Mail-Adresse sieht nicht vollständig aus (name@beispiel.de).");
    if (!problem.isEmpty()) {
        m_msg->setText(problem);
        m_msg->setVisible(true);
        (c.name.isEmpty() ? m_name : m_email)->setFocus();
        return;
    }
    m_contact = c;
    QDialog::accept();
}
