// SPDX-License-Identifier: MIT
#include "smtpdialog.h"
#include "smtpclient.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

SmtpDialog::SmtpDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("E-Mail-Versand (SMTP)"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Postausgangsserver für E-Mail-Erinnerungen. Die Angaben – auch das "
                                "Passwort – werden in der verschlüsselten Datenbank gespeichert."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    const Smtp::Config c = Smtp::loadConfig();
    auto *f = new QFormLayout;
    m_host = new QLineEdit(c.host, this);
    m_host->setPlaceholderText(tr("z. B. smtp.example.de"));
    f->addRow(tr("&Server:"), m_host);
    m_sec = new QComboBox(this);
    m_sec->addItem(tr("STARTTLS (meist Port 587)"), int(Smtp::Security::StartTls));
    m_sec->addItem(tr("SSL/TLS (meist Port 465)"), int(Smtp::Security::SslTls));
    m_sec->addItem(tr("keine Verschlüsselung (nur im lokalen Netz)"), int(Smtp::Security::None));
    m_sec->setCurrentIndex(std::max(0, m_sec->findData(int(c.security))));
    f->addRow(tr("&Verschlüsselung:"), m_sec);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(c.port);
    f->addRow(tr("&Port:"), m_port);
    connect(m_sec, &QComboBox::activated, this, [this] {
        const auto s = Smtp::Security(m_sec->currentData().toInt());
        m_port->setValue(s == Smtp::Security::SslTls ? 465 : s == Smtp::Security::StartTls ? 587 : 25);
    });
    m_user = new QLineEdit(c.user, this);
    f->addRow(tr("&Benutzername:"), m_user);
    m_pass = new QLineEdit(c.password, this);
    m_pass->setEchoMode(QLineEdit::Password);
    f->addRow(tr("Pass&wort:"), m_pass);
    m_from = new QLineEdit(c.from, this);
    m_from->setPlaceholderText(tr("absender@example.de"));
    f->addRow(tr("&Absender:"), m_from);
    m_details = new QCheckBox(tr("Ort, Person und Notiz in die E-Mail aufnehmen"), this);
    m_details->setChecked(c.includeDetails);
    m_details->setToolTip(tr("E-Mails werden in der Regel unverschlüsselt zugestellt. Ohne diese Option "
                             "enthält die Erinnerung nur Titel, Zeitpunkt und Priorität."));
    f->addRow(QString(), m_details);
    lay->addLayout(f);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("&Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    QPushButton *test = buttons->addButton(tr("&Test-E-Mail senden…"), QDialogButtonBox::ActionRole);
    connect(test, &QPushButton::clicked, this, &SmtpDialog::sendTest);
    connect(buttons, &QDialogButtonBox::accepted, this, &SmtpDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SmtpDialog::reject);
    lay->addWidget(buttons);
    setMinimumWidth(480);
}

static Smtp::Config fromFields(QLineEdit *host, QSpinBox *port, QComboBox *sec, QLineEdit *user,
                               QLineEdit *pass, QLineEdit *from, QCheckBox *details)
{
    Smtp::Config c;
    c.host = host->text().trimmed();
    c.port = port->value();
    c.security = Smtp::Security(sec->currentData().toInt());
    c.user = user->text().trimmed();
    c.password = pass->text();
    c.from = from->text().trimmed();
    c.includeDetails = details->isChecked();
    return c;
}

void SmtpDialog::sendTest()
{
    const Smtp::Config c = fromFields(m_host, m_port, m_sec, m_user, m_pass, m_from, m_details);
    if (!c.isValid() || !Smtp::isValidAddress(c.from)) {
        QMessageBox::information(this, windowTitle(), tr("Bitte Server und eine gültige Absenderadresse angeben."));
        return;
    }
    bool ok = false;
    const QString to = QInputDialog::getText(this, windowTitle(), tr("Test-E-Mail senden an:"), QLineEdit::Normal,
        QSettings().value(QStringLiteral("appointments/lastEmail"), c.from).toString(), &ok).trimmed();
    if (!ok || to.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setEnabled(false);
    Smtp::sendAsync(c, to, tr("VitaParms: Test-E-Mail"),
                    tr("Diese Test-E-Mail bestätigt, dass VitaParms Erinnerungen versenden kann.\n"), this,
                    [this, to](bool sent, const QString &err) {
        setEnabled(true);
        QApplication::restoreOverrideCursor();
        if (sent) QMessageBox::information(this, windowTitle(), tr("Test-E-Mail an %1 wurde versendet.").arg(to));
        else QMessageBox::warning(this, windowTitle(), tr("Versand fehlgeschlagen:\n%1").arg(err));
    });
}

void SmtpDialog::accept()
{
    const Smtp::Config c = fromFields(m_host, m_port, m_sec, m_user, m_pass, m_from, m_details);
    if (!c.host.isEmpty() && !Smtp::isValidAddress(c.from)) {
        QMessageBox::information(this, windowTitle(), tr("Bitte eine gültige Absenderadresse angeben."));
        return;
    }
    if (!c.user.isEmpty() && c.security == Smtp::Security::None) {
        QMessageBox::information(this, windowTitle(),
            tr("Mit Benutzername/Passwort ist eine verschlüsselte Verbindung (STARTTLS oder SSL/TLS) erforderlich."));
        return;
    }
    Smtp::saveConfig(c);
    QDialog::accept();
}
