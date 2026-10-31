// SPDX-License-Identifier: MIT
#include "accountdialogs.h"
#include "edition.h"
#include "demodata.h"
#include "manual.h"
#include "session.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextDocument>
#include <QThread>
#include <QVBoxLayout>

// ================================================================== Login

LoginDialog::LoginDialog(const QString &dbPath, const KeyStore &ks, const QString &presetUser, QWidget *parent)
    : QDialog(parent), m_ks(ks)
{
    setWindowTitle(tr("VitaParms – Anmeldung"));
    auto *lay = new QVBoxLayout(this);
    auto *head = new QLabel(tr("<b>Anmeldung</b><br><small>Datenbank: %1</small>")
                                .arg(QDir::toNativeSeparators(dbPath).toHtmlEscaped()), this);
    head->setWordWrap(true);
    lay->addWidget(head);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(presetUser, this);
    m_pw = new QLineEdit(this);
    m_pw->setEchoMode(QLineEdit::Password);
    form->addRow(tr("&Konto:"), m_name);
    form->addRow(tr("&Passwort:"), m_pw);
    lay->addLayout(form);
    m_msg = new QLabel(this);
    m_msg->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_msg->setWordWrap(true);
    lay->addWidget(m_msg);

    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(tr("&Anmelden"), QDialogButtonBox::AcceptRole)->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Beenden"));
    connect(buttons, &QDialogButtonBox::accepted, this, &LoginDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LoginDialog::reject);
    lay->addWidget(buttons);

    // weitere Möglichkeiten in einer eigenen Zeile, damit nichts abgeschnitten wird
    auto *more = new QHBoxLayout;
    QPushButton *rec = new QPushButton(tr("&Wiederherstellungsschlüssel…"), this);
    rec->setToolTip(tr("Alle Passwörter vergessen? Mit dem ausgedruckten Wiederherstellungsschlüssel entsperren."));
    QPushButton *imp = new QPushButton(tr("Datenbank &importieren…"), this);
    imp->setObjectName(QStringLiteral("importButton"));
    imp->setToolTip(tr("Eine Datenbank von einem anderen Gerät oder eine Sicherung übernehmen "
                       "(*.vpdb oder *.sqlite mit .keys-Datei)"));
    QPushButton *guide = new QPushButton(tr("&Erste Schritte"), this);
    guide->setObjectName(QStringLiteral("gettingStartedButton"));
    guide->setToolTip(tr("Öffnet die Kurzanleitung „Erste Schritte“ – auch ohne Anmeldung"));
    for (QPushButton *b : { rec, imp, guide }) {
        b->setAutoDefault(false);
        more->addWidget(b);
    }
    more->addStretch();
    connect(imp, &QPushButton::clicked, this, [this] { m_import = true; QDialog::reject(); });
    connect(rec, &QPushButton::clicked, this, [this] { m_recovery = true; QDialog::reject(); });
    connect(guide, &QPushButton::clicked, this, [this] { Manual::open(this, Manual::Doc::GettingStarted); });
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    lay->addWidget(sep);
    lay->addLayout(more);
    (presetUser.isEmpty() ? m_name : m_pw)->setFocus();
    setMinimumWidth(480);
}

void LoginDialog::accept()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray key;
    Role role = Role::User;
    const bool ok = m_ks.unlock(m_name->text(), m_pw->text(), &key, &role);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        ++m_failures;
        m_pw->clear();
        m_msg->setText(tr("Konto oder Passwort ist falsch."));
        // Wachsende Wartezeit erschwert das Durchprobieren
        if (m_failures >= 3) {
            setEnabled(false);
            QApplication::processEvents();
            QThread::msleep(std::min(10, m_failures) * 500);
            setEnabled(true);
        }
        m_pw->setFocus();
        return;
    }
    const Account *a = m_ks.find(m_name->text());
    m_userName = a ? a->name : m_name->text().trimmed();
    m_role = role;
    m_key = key;
    m_pw->clear();
    QDialog::accept();
}

// =========================================================== Konto anlegen

AccountEditDialog::AccountEditDialog(Mode mode, const QString &intro, const QString &presetName, QWidget *parent)
    : QDialog(parent), m_mode(mode)
{
    setWindowTitle(mode == Mode::FirstAdmin ? (Edition::light() ? tr("Anmeldung einrichten") : tr("Administrator-Konto einrichten"))
                   : mode == Mode::NewAccount ? tr("Neues Anmeldekonto")
                   : mode == Mode::ResetPassword ? tr("Passwort neu setzen")
                                                 : tr("Eigenes Passwort ändern"));
    auto *lay = new QVBoxLayout(this);
    if (!intro.isEmpty()) {
        auto *l = new QLabel(intro, this);
        l->setWordWrap(true);
        lay->addWidget(l);
    }
    auto *form = new QFormLayout;
    m_name = new QLineEdit(presetName, this);
    m_name->setReadOnly(mode == Mode::ResetPassword || mode == Mode::ChangeOwnPassword);
    form->addRow(Edition::light() ? tr("&Ihr Name:") : tr("&Kontoname:"), m_name);
    if (mode == Mode::NewAccount) {
        m_role = new QComboBox(this);
        m_role->addItem(roleName(Role::User), int(Role::User));
        m_role->addItem(roleName(Role::Admin), int(Role::Admin));
        form->addRow(tr("&Rolle:"), m_role);
    }
    if (mode == Mode::ChangeOwnPassword) {
        m_old = new QLineEdit(this);
        m_old->setEchoMode(QLineEdit::Password);
        form->addRow(tr("&Bisheriges Passwort:"), m_old);
    }
    m_pw1 = new QLineEdit(this);
    m_pw1->setEchoMode(QLineEdit::Password);
    m_pw2 = new QLineEdit(this);
    m_pw2->setEchoMode(QLineEdit::Password);
    form->addRow(tr("&Passwort:"), m_pw1);
    form->addRow(tr("Passwort &wiederholen:"), m_pw2);
    lay->addLayout(form);
    auto *hint = new QLabel(tr("<small>Mindestens 8 Zeichen. Ein langer Satz ist sicherer als ein kurzes, "
                               "kompliziertes Wort.</small>"), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    if (mode == Mode::NewAccount && m_role) {
        auto *rh = new QLabel(tr("<small><b>Anwender</b> erfassen und bearbeiten Daten.<br>"
                                 "<b>Administratoren</b> verwalten zusätzlich Anmeldekonten, Sicherungen, "
                                 "Datenbanken und Formulare.</small>"), this);
        rh->setWordWrap(true);
        lay->addWidget(rh);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Speichern"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &AccountEditDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &AccountEditDialog::reject);
    lay->addWidget(buttons);
    (m_name->isReadOnly() ? (m_old ? m_old : m_pw1) : m_name)->setFocus();
    setMinimumWidth(440);
}

QString AccountEditDialog::name() const { return m_name->text().trimmed(); }
QString AccountEditDialog::password() const { return m_pw1->text(); }
QString AccountEditDialog::oldPassword() const { return m_old ? m_old->text() : QString(); }
Role AccountEditDialog::role() const
{
    if (m_mode == Mode::FirstAdmin) return Role::Admin;
    return m_role ? Role(m_role->currentData().toInt()) : Role::User;
}

void AccountEditDialog::accept()
{
    if (name().isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("Bitte einen Kontonamen angeben."));
        return;
    }
    if (m_pw1->text() != m_pw2->text()) {
        QMessageBox::warning(this, windowTitle(), tr("Die Passwörter stimmen nicht überein."));
        return;
    }
    const QString p = KeyStore::passwordProblem(m_pw1->text());
    if (!p.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), p);
        return;
    }
    QDialog::accept();
}

// ===================================================== Wiederherstellung

RecoveryCodeDialog::RecoveryCodeDialog(const QString &code, const QString &dbPath, bool requireConfirm, QWidget *parent)
    : QDialog(parent), m_code(code), m_dbPath(dbPath)
{
    setWindowTitle(tr("Wiederherstellungsschlüssel"));
    auto *lay = new QVBoxLayout(this);
    auto *intro = new QLabel(tr(
        "<p>Mit diesem Schlüssel lässt sich die Datenbank öffnen, auch wenn <b>alle Passwörter "
        "vergessen</b> wurden oder die Schlüsseldatei (<tt>.keys</tt>) verloren ging.</p>"
        "<p><b>Bitte ausdrucken oder speichern und sicher verwahren</b> (z. B. bei wichtigen "
        "Unterlagen, nicht auf demselben Computer). Wer diesen Schlüssel besitzt, kann alle Daten "
        "lesen. Ohne Passwort und ohne diesen Schlüssel sind die Daten <b>unwiederbringlich "
        "verloren</b>.</p>"), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *codeLabel = new QLabel(code, this);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() * 1.35);
    mono.setBold(true);
    codeLabel->setFont(mono);
    codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setWordWrap(true);
    codeLabel->setFrameShape(QFrame::StyledPanel);
    codeLabel->setMargin(12);
    lay->addWidget(codeLabel);

    auto *row = new QHBoxLayout;
    auto *copy = new QPushButton(tr("&Kopieren"), this);
    auto *print = new QPushButton(tr("&Drucken…"), this);
    auto *save = new QPushButton(tr("Als &Datei speichern…"), this);
    row->addWidget(copy);
    row->addWidget(print);
    row->addWidget(save);
    row->addStretch();
    lay->addLayout(row);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_code); });
    connect(print, &QPushButton::clicked, this, &RecoveryCodeDialog::print);
    connect(save, &QPushButton::clicked, this, &RecoveryCodeDialog::saveFile);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Fertig"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    if (requireConfirm) {
        auto *confirm = new QCheckBox(tr("Ich habe den Wiederherstellungsschlüssel sicher aufbewahrt."), this);
        lay->addWidget(confirm);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        connect(confirm, &QCheckBox::toggled, buttons->button(QDialogButtonBox::Ok), &QPushButton::setEnabled);
    }
    lay->addWidget(buttons);
    setMinimumWidth(560);
}

static QString recoveryHtml(const QString &code, const QString &dbPath)
{
    return QCoreApplication::translate("RecoveryCodeDialog",
        "<h2>VitaParms – Wiederherstellungsschlüssel</h2>"
        "<p>Datenbank: %1<br>Erstellt am: %2</p>"
        "<p style=\"font-family: monospace; font-size: 15pt; font-weight: bold;\">%3</p>"
        "<p>Dieser Schlüssel öffnet die verschlüsselte Datenbank ohne Passwort. "
        "Sicher und getrennt vom Computer aufbewahren. Nicht weitergeben.</p>"
        "<p>Verwendung: Anmeldedialog → „Wiederherstellungsschlüssel…“.</p>")
        .arg(QDir::toNativeSeparators(dbPath).toHtmlEscaped(),
             QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("dd.MM.yyyy HH:mm")), code);
}

void RecoveryCodeDialog::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QTextDocument doc;
    doc.setHtml(recoveryHtml(m_code, m_dbPath));
    doc.print(&printer);
}

void RecoveryCodeDialog::saveFile()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Wiederherstellungsschlüssel speichern"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("VitaParms-Wiederherstellungsschluessel.html")),
        tr("HTML-Datei (*.html);;Textdatei (*.txt)"));
    if (path.isEmpty()) return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, windowTitle(), f.errorString());
        return;
    }
    if (path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)) {
        QTextDocument d;
        d.setHtml(recoveryHtml(m_code, m_dbPath));
        f.write(d.toPlainText().toUtf8());
    } else {
        f.write(QStringLiteral("<!doctype html><meta charset=\"utf-8\">").toUtf8() + recoveryHtml(m_code, m_dbPath).toUtf8());
    }
    f.commit();
    QMessageBox::information(this, windowTitle(),
        tr("Gespeichert. Bitte die Datei auf einen USB-Stick o. Ä. verschieben und nicht auf diesem Computer belassen."));
}

QByteArray askRecoveryKey(QWidget *parent, const QString &reason)
{
    for (;;) {
        bool ok = false;
        const QString code = QInputDialog::getText(parent, QCoreApplication::translate("Session", "Wiederherstellungsschlüssel"),
            reason + QCoreApplication::translate("Session", "\n\nBitte den Wiederherstellungsschlüssel eingeben "
                                                           "(Groß-/Kleinschreibung und Bindestriche egal):"),
            QLineEdit::Normal, {}, &ok);
        if (!ok) return {};
        const QByteArray key = KeyStore::keyFromRecoveryCode(code);
        if (!key.isEmpty()) return key;
        QMessageBox::warning(parent, QCoreApplication::translate("Session", "Wiederherstellungsschlüssel"),
                             QCoreApplication::translate("Session", "Der Schlüssel ist unvollständig oder enthält einen Tippfehler."));
    }
}

// ======================================================= Kontenverwaltung

AccountsDialog::AccountsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Anmeldekonten"));
    auto *lay = new QVBoxLayout(this);
    auto *info = new QLabel(tr("Angemeldet als <b>%1</b> (%2). Konten gelten für die geöffnete Datenbank.")
                                .arg(Session::user().toHtmlEscaped(), roleName(Session::role())), this);
    info->setWordWrap(true);
    lay->addWidget(info);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Konto"), tr("Rolle"), tr("Angelegt") });
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table);

    auto *row = new QHBoxLayout;
    auto add = [&](const QString &t, void (AccountsDialog::*slot)(), bool adminOnly) {
        auto *b = new QPushButton(t, this);
        connect(b, &QPushButton::clicked, this, slot);
        row->addWidget(b);
        if (adminOnly) m_adminOnly << b;
    };
    add(tr("&Neues Konto…"), &AccountsDialog::addAccount, true);
    add(tr("Passwort &setzen…"), &AccountsDialog::resetPassword, true);
    add(tr("&Rolle wechseln"), &AccountsDialog::toggleRole, true);
    add(tr("&Löschen…"), &AccountsDialog::removeAccount, true);
    row->addStretch();
    add(tr("&Eigenes Passwort ändern…"), &AccountsDialog::changeOwnPassword, false);
    lay->addLayout(row);
    auto *row2 = new QHBoxLayout;
    auto *rec = new QPushButton(tr("&Wiederherstellungsschlüssel anzeigen…"), this);
    connect(rec, &QPushButton::clicked, this, &AccountsDialog::showRecovery);
    m_adminOnly << rec;
    row2->addWidget(rec);
    row2->addStretch();
    lay->addLayout(row2);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("&Schließen"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    for (QWidget *w : m_adminOnly) w->setVisible(Session::isAdmin());
    if (Edition::light()) {
        // VitaParms light: ein Konto ohne Rollen – nur Passwort ändern und Wiederherstellungsschlüssel
        setWindowTitle(tr("Anmeldung und Passwort"));
        info->setText(tr("Angemeldet als <b>%1</b>. Das Passwort schützt die verschlüsselte Datenbank. Bewahren Sie "
                         "den Wiederherstellungsschlüssel sicher auf – er öffnet die Datenbank, falls Sie das Passwort "
                         "vergessen.").arg(Session::user().toHtmlEscaped()));
        m_table->hide();
        for (QWidget *w : m_adminOnly) w->setVisible(w == rec);
    }
    refresh();
    resize(620, 380);
}

void AccountsDialog::refresh()
{
    const QString sel = selectedName();
    m_table->setRowCount(0);
    for (const Account &a : Session::keyStore().accounts()) {
        if (!Session::isAdmin() && a.name.compare(Session::user(), Qt::CaseInsensitive) != 0)
            continue;   // Anwender sehen nur ihr eigenes Konto
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        auto *n = new QTableWidgetItem(a.name);
        if (a.name.compare(Session::user(), Qt::CaseInsensitive) == 0) {
            QFont f = n->font(); f.setBold(true); n->setFont(f);
        }
        m_table->setItem(r, 0, n);
        m_table->setItem(r, 1, new QTableWidgetItem(roleName(a.role)));
        m_table->setItem(r, 2, new QTableWidgetItem(QLocale().toString(a.created, QStringLiteral("dd.MM.yyyy HH:mm"))));
        if (a.name == sel) m_table->selectRow(r);
    }
    m_table->resizeColumnsToContents();
}

QString AccountsDialog::selectedName() const
{
    const auto items = m_table->selectedItems();
    return items.isEmpty() ? QString() : m_table->item(items.first()->row(), 0)->text();
}

bool AccountsDialog::persist()
{
    QString err;
    if (!Session::keyStore().save(Session::dbKey(), &err)) {
        QMessageBox::critical(this, windowTitle(), tr("Die Schlüsseldatei konnte nicht gespeichert werden:\n%1").arg(err));
        Session::keyStore().load(Session::keyStore().path());
        refresh();
        return false;
    }
    refresh();
    return true;
}

void AccountsDialog::addAccount()
{
    AccountEditDialog d(AccountEditDialog::Mode::NewAccount, {}, {}, this);
    if (d.exec() != QDialog::Accepted) return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Session::keyStore().addAccount(d.name(), d.password(), d.role(), Session::dbKey(), &err);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, windowTitle(), err); return; }
    persist();
}

void AccountsDialog::resetPassword()
{
    const QString n = selectedName();
    if (n.isEmpty()) return;
    AccountEditDialog d(AccountEditDialog::Mode::ResetPassword,
                        tr("Neues Passwort für „%1“. Teilen Sie es dem Kontoinhaber persönlich mit.").arg(n), n, this);
    if (d.exec() != QDialog::Accepted) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    Session::keyStore().setPassword(n, d.password(), Session::dbKey());
    QApplication::restoreOverrideCursor();
    persist();
}

void AccountsDialog::toggleRole()
{
    const QString n = selectedName();
    const Account *a = Session::keyStore().find(n);
    if (!a) return;
    const Role target = a->role == Role::Admin ? Role::User : Role::Admin;
    if (n.compare(Session::user(), Qt::CaseInsensitive) == 0 && target == Role::User
        && QMessageBox::question(this, windowTitle(),
               tr("Sie entziehen sich selbst die Administratorrechte (wirksam ab der nächsten Anmeldung). Fortfahren?"))
           != QMessageBox::Yes)
        return;
    QString err;
    if (!Session::keyStore().setRole(n, target, &err)) { QMessageBox::warning(this, windowTitle(), err); return; }
    persist();
}

void AccountsDialog::removeAccount()
{
    const QString n = selectedName();
    if (n.isEmpty()) return;
    if (n.compare(Session::user(), Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, windowTitle(), tr("Das eigene Konto kann nicht gelöscht werden."));
        return;
    }
    if (QMessageBox::question(this, windowTitle(), tr("Konto „%1“ löschen?").arg(n)) != QMessageBox::Yes) return;
    QString err;
    if (!Session::keyStore().removeAccount(n, &err)) { QMessageBox::warning(this, windowTitle(), err); return; }
    if (persist())
        QMessageBox::information(this, windowTitle(),
            tr("Das Konto wurde gelöscht. Hinweis: Wer früher Zugriff hatte, konnte die Datenbank bereits "
               "kopieren – bei Bedarf alte Sicherungen entsprechend schützen."));
}

void AccountsDialog::changeOwnPassword()
{
    AccountEditDialog d(AccountEditDialog::Mode::ChangeOwnPassword, {}, Session::user(), this);
    if (d.exec() != QDialog::Accepted) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = Session::keyStore().unlock(Session::user(), d.oldPassword(), nullptr, nullptr);
    if (ok) Session::keyStore().setPassword(Session::user(), d.password(), Session::dbKey());
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, windowTitle(), tr("Das bisherige Passwort ist falsch.")); return; }
    if (persist())
        QMessageBox::information(this, windowTitle(), tr("Das Passwort wurde geändert."));
}

void AccountsDialog::showRecovery()
{
    bool ok = false;
    const QString pw = QInputDialog::getText(this, windowTitle(),
        tr("Zur Bestätigung bitte Ihr Passwort eingeben:"), QLineEdit::Password, {}, &ok);
    if (!ok) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool valid = Session::keyStore().unlock(Session::user(), pw, nullptr, nullptr);
    QApplication::restoreOverrideCursor();
    if (!valid) { QMessageBox::warning(this, windowTitle(), tr("Das Passwort ist falsch.")); return; }
    RecoveryCodeDialog d(KeyStore::recoveryCode(Session::dbKey()), Session::keyStore().path().chopped(5), false, this);
    d.exec();
}

// =========================================================== Erster Start

LanguageDialog::LanguageDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("languageDialog"));
    setWindowTitle(QStringLiteral("VitaParms – Sprache / Language"));
    auto *lay = new QVBoxLayout(this);
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/vitaparms.png")).pixmap(48, 48));
    head->addWidget(icon, 0, Qt::AlignTop);
    auto *lbl = new QLabel(QStringLiteral(
        "<p><b>Willkommen bei VitaParms!</b><br>Bitte wählen Sie die Sprache.</p>"
        "<p><b>Welcome to VitaParms!</b><br>Please choose your language.</p>"), this);
    head->addWidget(lbl, 1);
    lay->addLayout(head);
    auto *row = new QHBoxLayout;
    auto *de = new QPushButton(QStringLiteral("&Deutsch"), this);
    auto *en = new QPushButton(QStringLiteral("&English"), this);
    de->setObjectName(QStringLiteral("langDe"));
    en->setObjectName(QStringLiteral("langEn"));
    for (QPushButton *b : { de, en }) {
        b->setMinimumHeight(40);
        b->setMinimumWidth(150);
        row->addWidget(b);
    }
    lay->addLayout(row);
    auto *note = new QLabel(QStringLiteral("<small>Später änderbar unter <i>Extras → Einstellungen</i>.<br>"
                                           "Can be changed later under <i>Extras → Settings</i>.</small>"), this);
    lay->addWidget(note);
    (QLocale::system().language() == QLocale::German ? de : en)->setDefault(true);
    connect(de, &QPushButton::clicked, this, [this] { m_chosen = QStringLiteral("de"); accept(); });
    connect(en, &QPushButton::clicked, this, [this] { m_chosen = QStringLiteral("en"); accept(); });
}

FirstStartBox::FirstStartBox(QWidget *parent)
    : QMessageBox(QMessageBox::Question, tr("%1 einrichten").arg(Edition::appName()),
                  tr("<p>Es gibt noch keine Datenbank.</p><p>Eine <b>neue Datenbank</b> anlegen oder eine "
                     "vorhandene – von einem anderen Gerät (*.vpdb) oder aus einer Sicherung (*.sqlite) – "
                     "<b>importieren</b>?</p>"
                     "<p>Zum Kennenlernen: <b>Erste Schritte</b> erklärt die Bedienung an Beispielen, "
                     "die <b>Demo</b> öffnet ein zweites Fenster mit Beispieldaten zum Ausprobieren.</p>"),
                  QMessageBox::NoButton, parent)
{
    setObjectName(QStringLiteral("firstStartBox"));
    m_fresh = addButton(tr("&Neue Datenbank anlegen"), QMessageBox::AcceptRole);
    m_import = addButton(tr("Datenbank &importieren…"), QMessageBox::ActionRole);
    QPushButton *guide = addButton(tr("&Erste Schritte"), QMessageBox::HelpRole);
    guide->setObjectName(QStringLiteral("gettingStartedButton"));
    guide->setToolTip(tr("Öffnet die Kurzanleitung „Erste Schritte“"));
    QPushButton *demo = addButton(tr("&Demo ausprobieren"), QMessageBox::HelpRole);
    demo->setObjectName(QStringLiteral("demoButton"));
    demo->setToolTip(tr("Startet ein zweites Programmfenster mit Beispieldaten – Ihre eigenen Daten bleiben unberührt"));
    addButton(tr("Abbrechen"), QMessageBox::RejectRole);
    setDefaultButton(m_fresh);
    // „Erste Schritte“ und „Demo“ lassen den Dialog offen
    for (QPushButton *b : { guide, demo }) {
        b->disconnect();
        connect(b, &QPushButton::clicked, this, [this, b, guide] {
            if (b == guide) Manual::open(this, Manual::Doc::GettingStarted);
            else DemoData::launch();
        });
    }
}
