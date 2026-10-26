// SPDX-License-Identifier: MIT
#include "websitedialog.h"
#include "websync.h"
#include "dbsync.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

WebsiteDialog::WebsiteDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Website"));
    setObjectName(QStringLiteral("websiteDialog"));
    const WebSync::Config c = WebSync::config();
    auto *v = new QVBoxLayout(this);

    auto *intro = new QLabel(tr(
        "<p>VitaParms kann die Daten <b>aller Personen</b> – den gesamten Verlauf der Messwerte, "
        "Medikamente, Termine und Berichte, aber <b>keine Krankenakte</b> – auf Ihrer eigenen Website "
        "zum Ansehen bereitstellen, etwa für Angehörige oder die Arztpraxis.</p>"
        "<p>Die Daten werden <b>vor dem Hochladen mit dem Master-Passwort verschlüsselt</b>. Auf dem "
        "Server liegt nur Zeichensalat; lesbar werden die Daten erst im Browser nach Eingabe des "
        "Passworts. Die Website kann nichts verändern.</p>"), this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);

    m_enabled = new QCheckBox(tr("Website &verwenden"), this);
    m_enabled->setObjectName(QStringLiteral("webEnabled"));
    m_enabled->setChecked(c.enabled);
    v->addWidget(m_enabled);

    auto *box = new QGroupBox(tr("Zugang"), this);
    auto *f = new QFormLayout(box);
    m_url = new QLineEdit(c.url, box);
    m_url->setObjectName(QStringLiteral("webUrl"));
    m_url->setPlaceholderText(QStringLiteral("https://vitaparms.example.de/"));
    f->addRow(tr("&Adresse:"), m_url);

    m_password = new QLineEdit(c.password, box);
    m_password->setObjectName(QStringLiteral("webPassword"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setToolTip(tr("Mit diesem Passwort melden sich alle Betrachter auf der Website an. "
                              "Mindestens 10 Zeichen; ein langer Satz ist sicherer als ein kurzes, kompliziertes Wort."));
    f->addRow(tr("&Master-Passwort:"), m_password);
    m_repeat = new QLineEdit(c.password, box);
    m_repeat->setObjectName(QStringLiteral("webPasswordRepeat"));
    m_repeat->setEchoMode(QLineEdit::Password);
    f->addRow(tr("Passwort &wiederholen:"), m_repeat);
    m_show = new QCheckBox(tr("Passwort &anzeigen"), box);
    connect(m_show, &QCheckBox::toggled, this, [this](bool on) {
        m_password->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        m_repeat->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    f->addRow(QString(), m_show);

    auto *keyRow = new QHBoxLayout;
    m_key = new QLineEdit(c.uploadKey.isEmpty() ? WebSync::generateUploadKey() : c.uploadKey, box);
    m_key->setObjectName(QStringLiteral("webUploadKey"));
    m_key->setReadOnly(true);
    m_key->setToolTip(tr("Berechtigt VitaParms zum Hochladen. Die Website merkt sich beim ersten Hochladen "
                         "diesen Schlüssel und nimmt danach nur noch Daten mit demselben Schlüssel an."));
    auto *newKey = new QPushButton(tr("&Neu"), box);
    newKey->setToolTip(tr("Neuen Upload-Schlüssel erzeugen – danach auf dem Server die Datei "
                          "data/.uploadkey.php löschen, damit er übernommen wird."));
    auto *copyKey = new QPushButton(tr("&Kopieren"), box);
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(newKey);
    keyRow->addWidget(copyKey);
    connect(newKey, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Upload-Schlüssel"),
                tr("Einen neuen Upload-Schlüssel erzeugen?\n\nDie Website nimmt ihn erst an, wenn Sie auf dem Server "
                   "die Datei data/.uploadkey.php gelöscht haben.")) == QMessageBox::Yes)
            m_key->setText(WebSync::generateUploadKey());
    });
    connect(copyKey, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_key->text()); });
    f->addRow(tr("&Upload-Schlüssel:"), keyRow);
    v->addWidget(box);

    m_auto = new QCheckBox(tr("&Automatisch aktualisieren – eine Minute nach Änderungen und beim Beenden"), this);
    m_auto->setObjectName(QStringLiteral("webAuto"));
    m_auto->setChecked(c.autoPublish);
    v->addWidget(m_auto);

    // ---- Abgleich mit anderen Geräten
    const DbSync::Config sc = DbSync::config();
    auto *syncBox = new QGroupBox(tr("Abgleich mit anderen Geräten (Tablet)"), this);
    auto *sv = new QVBoxLayout(syncBox);
    auto *syncIntro = new QLabel(tr("Über dieselbe Adresse und denselben Upload-Schlüssel tauschen VitaParms am Desktop "
                                    "und die Tablet-App die <b>verschlüsselte Datenbank</b> aus. Änderungen beider Seiten "
                                    "werden zusammengeführt; bei gleichzeitiger Änderung derselben Angabe gilt die jüngere."),
                                 syncBox);
    syncIntro->setWordWrap(true);
    sv->addWidget(syncIntro);
    m_syncEnabled = new QCheckBox(tr("Datenbank mit anderen Geräten &abgleichen"), syncBox);
    m_syncEnabled->setObjectName(QStringLiteral("syncEnabled"));
    m_syncEnabled->setChecked(sc.enabled);
    sv->addWidget(m_syncEnabled);
    m_syncAuto = new QCheckBox(tr("Automatisch abgleichen – nach Änderungen, alle 10 Minuten und beim Beenden"), syncBox);
    m_syncAuto->setObjectName(QStringLiteral("syncAuto"));
    m_syncAuto->setChecked(sc.autoSync);
    sv->addWidget(m_syncAuto);
    auto *syncRow = new QHBoxLayout;
    m_syncNow = new QPushButton(tr("Jetzt a&bgleichen"), syncBox);
    m_syncNow->setObjectName(QStringLiteral("syncNow"));
    syncRow->addWidget(m_syncNow);
    auto *couple = new QPushButton(tr("&Kopplungsdatei speichern…"), syncBox);
    couple->setObjectName(QStringLiteral("syncCouple"));
    couple->setToolTip(tr("Speichert Adresse und Upload-Schlüssel in einer Datei, die die Tablet-App bei der "
                          "Einrichtung öffnen kann – so muss der Schlüssel nicht abgetippt werden."));
    connect(couple, &QPushButton::clicked, this, [this] {
        if (!applyToConfig(true)) return;
        if (QMessageBox::question(this, tr("Kopplungsdatei"),
                tr("Die Datei enthält den Upload-Schlüssel. Wer sie besitzt, kann die verschlüsselte Datenbank "
                   "herunterladen (lesbar nur mit einem Anmeldekonto). Übertragen Sie sie nur auf eigene Geräte "
                   "und löschen Sie sie danach.\n\nJetzt speichern?")) != QMessageBox::Yes)
            return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Kopplungsdatei speichern"),
            QDir::home().filePath(QStringLiteral("VitaParms-Kopplung.vpcouple")), tr("Kopplungsdatei (*.vpcouple)"));
        if (path.isEmpty()) return;
        const WebSync::Config c = WebSync::config();
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Kopplungsdatei"), f.errorString());
            return;
        }
        f.write(QJsonDocument(QJsonObject{ { QStringLiteral("format"), QStringLiteral("vitaparms-coupling") },
                                           { QStringLiteral("url"), c.url }, { QStringLiteral("uploadKey"), c.uploadKey } })
                    .toJson(QJsonDocument::Indented));
        m_syncStatus->setText(tr("Kopplungsdatei gespeichert: %1").arg(QDir::toNativeSeparators(path)));
    });
    syncRow->addWidget(couple);
    m_syncStatus = new QLabel(syncBox);
    m_syncStatus->setObjectName(QStringLiteral("syncStatus"));
    m_syncStatus->setWordWrap(true);
    {
        const DbSync::Status st = DbSync::instance().status();
        if (st.last.isValid())
            m_syncStatus->setText(tr("Zuletzt abgeglichen: %1").arg(QLocale().toString(st.last, QLocale::ShortFormat))
                                  + (st.lastResult.isEmpty() ? QString() : QStringLiteral(" – ") + st.lastResult));
    }
    syncRow->addWidget(m_syncStatus, 1);
    sv->addLayout(syncRow);
    v->addWidget(syncBox);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("webStatus"));
    if (c.lastUpload.isValid())
        m_status->setText(tr("Zuletzt hochgeladen: %1").arg(QLocale().toString(c.lastUpload, QLocale::ShortFormat))
                          + (c.lastResult.isEmpty() ? QString() : QStringLiteral("\n") + c.lastResult));
    else if (!c.lastResult.isEmpty())
        m_status->setText(c.lastResult);
    v->addWidget(m_status);

    auto *actions = new QHBoxLayout;
    m_test = new QPushButton(tr("Verbindung &testen"), this);
    m_publish = new QPushButton(tr("&Jetzt hochladen"), this);
    m_open = new QPushButton(tr("Website ö&ffnen"), this);
    actions->addWidget(m_test);
    actions->addWidget(m_publish);
    actions->addWidget(m_open);
    actions->addStretch();
    v->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    WebSync &ws = WebSync::instance();
    connect(m_test, &QPushButton::clicked, this, [this, &ws] {
        if (!applyToConfig(true)) return;
        setBusy(true, tr("Verbindung wird geprüft …"));
        ws.testConnection();
    });
    connect(&ws, &WebSync::connectionTested, this, [this](bool ok, const QString &msg) {
        setBusy(false, (ok ? QStringLiteral("✔ ") : QStringLiteral("⚠ ")) + msg);
    });
    connect(m_publish, &QPushButton::clicked, this, [this, &ws] {
        if (!applyToConfig(true)) return;
        setBusy(true, tr("Daten werden verschlüsselt und hochgeladen …"));
        ws.publish();
    });
    connect(&ws, &WebSync::finished, this, [this](bool ok, const QString &msg) {
        setBusy(false, (ok ? QStringLiteral("✔ ") : QStringLiteral("⚠ ")) + msg);
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(WebSync::normalizedUrl(m_url->text())));
    });
    DbSync &ds = DbSync::instance();
    connect(m_syncNow, &QPushButton::clicked, this, [this, &ds] {
        if (!applyToConfig(true)) return;
        QString why;
        if (!DbSync::isReady(DbSync::config(), &why)) { m_syncStatus->setText(QStringLiteral("⚠ ") + why); return; }
        m_syncStatus->setText(tr("Wird abgeglichen …"));
        QApplication::setOverrideCursor(Qt::BusyCursor);
        ds.sync();
        QApplication::restoreOverrideCursor();
    });
    connect(&ds, &DbSync::finished, this, [this](bool ok, const QString &msg) {
        m_syncStatus->setText((ok ? QStringLiteral("✔ ") : QStringLiteral("⚠ ")) + msg);
    });
    for (QCheckBox *cb : { m_enabled, m_syncEnabled })
        connect(cb, &QCheckBox::toggled, this, &WebsiteDialog::updateState);
    connect(m_url, &QLineEdit::textChanged, this, &WebsiteDialog::updateState);
    updateState();
    resize(560, sizeHint().height());
}

void WebsiteDialog::updateState()
{
    const bool on = m_enabled->isChecked(), sync = m_syncEnabled->isChecked();
    for (QWidget *w : std::initializer_list<QWidget *>{ m_password, m_repeat, m_show, m_auto, m_publish })
        w->setEnabled(on);
    for (QWidget *w : std::initializer_list<QWidget *>{ m_url, m_key, m_test })
        w->setEnabled(on || sync);
    m_syncAuto->setEnabled(sync);
    m_syncNow->setEnabled(sync);
    m_open->setEnabled(!m_url->text().trimmed().isEmpty());
}

void WebsiteDialog::setBusy(bool busy, const QString &text)
{
    m_test->setEnabled(!busy && (m_enabled->isChecked() || m_syncEnabled->isChecked()));
    m_publish->setEnabled(!busy && m_enabled->isChecked());
    if (busy) QApplication::setOverrideCursor(Qt::BusyCursor);
    else QApplication::restoreOverrideCursor();
    if (!text.isEmpty()) m_status->setText(text);
}

bool WebsiteDialog::applyToConfig(bool showErrors)
{
    if (m_password->text() != m_repeat->text()) {
        if (showErrors) QMessageBox::warning(this, tr("Website"), tr("Die beiden Passwörter stimmen nicht überein."));
        return false;
    }
    WebSync::Config c = WebSync::config();
    c.enabled = m_enabled->isChecked();
    c.url = WebSync::normalizedUrl(m_url->text());
    c.password = m_password->text();
    c.uploadKey = m_key->text();
    c.autoPublish = m_auto->isChecked();
    QString why;
    if (c.enabled && !WebSync::isReady(c, &why)) {
        if (showErrors) QMessageBox::warning(this, tr("Website"), why);
        return false;
    }
    WebSync::saveConfig(c);
    m_url->setText(c.url);
    DbSync::Config sc = DbSync::config();
    sc.enabled = m_syncEnabled->isChecked();
    sc.autoSync = m_syncAuto->isChecked();
    sc.url = c.url;
    sc.uploadKey = c.uploadKey;
    if (sc.enabled && !DbSync::isReady(sc, &why)) {
        if (showErrors) QMessageBox::warning(this, tr("Abgleich"), why);
        return false;
    }
    DbSync::saveConfig(sc);
    return true;
}

void WebsiteDialog::accept()
{
    if (applyToConfig(true)) QDialog::accept();
}
