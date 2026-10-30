// SPDX-License-Identifier: MIT
#include "settingsdialog.h"
#include "quarterreminder.h"
#include "appsettings.h"
#include "database.h"
#include "i18n.h"
#include "themes.h"
#include "speech.h"
#include "leaflet.h"
#include <QCheckBox>
#include <QInputDialog>
#include "session.h"
#include "smtpdialog.h"
#include "upcoming.h"

#include <QMessageBox>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include "websitedialog.h"
#include "autobackup.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QLocale>
#include <QSpinBox>
#include <QUrl>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QString backupSummary()
{
    const AutoBackup::Config c = AutoBackup::config();
    if (!c.enabled) return SettingsDialog::tr("aus");
    const QList<AutoBackup::Entry> l = AutoBackup::list();
    const QString last = l.isEmpty() ? SettingsDialog::tr("noch keine")
                                     : QLocale().toString(l.first().when, QStringLiteral("dd.MM.yyyy HH:mm"));
    return SettingsDialog::tr("ein – letzte: %1, %2 von %3 Ständen").arg(last).arg(l.size()).arg(c.keep);
}

// Automatische Sicherung einstellen (Ordner, Anzahl), jetzt sichern, Ordner öffnen
void editAutoBackup(QWidget *parent)
{
    QDialog d(parent);
    d.setWindowTitle(SettingsDialog::tr("Automatische Sicherung"));
    const AutoBackup::Config c = AutoBackup::config();
    auto *lay = new QVBoxLayout(&d);
    auto *intro = new QLabel(SettingsDialog::tr("VitaParms sichert die Datenbank (verschlüsselt, mit den Anmeldekonten) beim Beenden, "
                                "wenn sich etwas geändert hat, und beim Start, wenn die letzte Sicherung älter als einen "
                                "Tag ist. Zurückholen: Datei → Sicherung wiederherstellen / importieren."), &d);
    intro->setWordWrap(true);
    lay->addWidget(intro);
    auto *form = new QFormLayout;
    auto *on = new QCheckBox(SettingsDialog::tr("Automatisch &sichern"), &d);
    on->setObjectName(QStringLiteral("autoBackupEnabled"));
    on->setChecked(c.enabled);
    form->addRow(QString(), on);
    auto *folder = new QLineEdit(c.folder.isEmpty() ? AutoBackup::defaultFolder() : c.folder, &d);
    folder->setObjectName(QStringLiteral("autoBackupFolder"));
    auto *choose = new QPushButton(SettingsDialog::tr("&Ändern…"), &d);
    auto *row = new QHBoxLayout;
    row->addWidget(folder, 1);
    row->addWidget(choose);
    auto *folderLabel = new QLabel(SettingsDialog::tr("&Ordner:"), &d);
    folderLabel->setBuddy(folder);
    form->addRow(folderLabel, row);
    auto *keep = new QSpinBox(&d);
    keep->setRange(2, 60);
    keep->setValue(c.keep);
    keep->setSuffix(SettingsDialog::tr(" Stände"));
    form->addRow(SettingsDialog::tr("&Aufbewahren:"), keep);
    lay->addLayout(form);
    auto *tip = new QLabel(SettingsDialog::tr("<b>Tipp:</b> Der Standardordner liegt auf derselben Festplatte wie die Datenbank. "
                              "Als Schutz vor einem Festplattendefekt einen USB-Stick, eine externe Festplatte oder "
                              "einen Cloud-Ordner wählen (die Sicherung ist verschlüsselt)."), &d);
    tip->setWordWrap(true);
    lay->addWidget(tip);
    auto *state = new QLabel(&d);
    state->setObjectName(QStringLiteral("autoBackupState"));
    state->setWordWrap(true);
    lay->addWidget(state);
    auto refresh = [state] {
        const QList<AutoBackup::Entry> l = AutoBackup::list();
        qint64 bytes = 0;
        for (const auto &e : l) bytes += e.size;
        state->setText(l.isEmpty() ? SettingsDialog::tr("Noch keine automatische Sicherung in diesem Ordner.")
            : SettingsDialog::tr("%1 Sicherung(en), zusammen %2 MB; letzte: %3")
                  .arg(l.size()).arg(QLocale().toString(bytes / 1048576.0, 'f', 1))
                  .arg(QLocale().toString(l.first().when, QStringLiteral("dd.MM.yyyy HH:mm"))));
    };
    auto apply = [&] {
        AutoBackup::Config n;
        n.enabled = on->isChecked();
        n.folder = QDir::cleanPath(folder->text().trimmed()) == QDir::cleanPath(AutoBackup::defaultFolder()) ? QString()
                                                                                                         : folder->text().trimmed();
        n.keep = keep->value();
        AutoBackup::saveConfig(n);
    };
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        const QString dir = QFileDialog::getExistingDirectory(&d, SettingsDialog::tr("Ordner für die automatische Sicherung"), folder->text());
        if (!dir.isEmpty()) { folder->setText(QDir::toNativeSeparators(dir)); apply(); refresh(); }
    });
    auto *now = new QPushButton(SettingsDialog::tr("&Jetzt sichern"), &d);
    now->setObjectName(QStringLiteral("autoBackupNow"));
    auto *open = new QPushButton(SettingsDialog::tr("Ordner ö&ffnen"), &d);
    QObject::connect(now, &QPushButton::clicked, &d, [&] {
        apply();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString path, err;
        const bool ok = AutoBackup::run(&path, &err);
        QApplication::restoreOverrideCursor();
        refresh();
        if (ok) QMessageBox::information(&d, d.windowTitle(), SettingsDialog::tr("Sicherung erstellt:\n%1").arg(QDir::toNativeSeparators(path)));
        else QMessageBox::warning(&d, d.windowTitle(), err);
    });
    QObject::connect(open, &QPushButton::clicked, &d, [&] {
        apply();
        QDir().mkpath(AutoBackup::folder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AutoBackup::folder()));
    });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    bb->button(QDialogButtonBox::Ok)->setText(SettingsDialog::tr("OK"));
    bb->button(QDialogButtonBox::Cancel)->setText(SettingsDialog::tr("Abbrechen"));
    bb->addButton(now, QDialogButtonBox::ActionRole);
    bb->addButton(open, QDialogButtonBox::ActionRole);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    lay->addWidget(bb);
    refresh();
    d.resize(620, d.sizeHint().height());
    const AutoBackup::Config before = AutoBackup::config();
    if (d.exec() == QDialog::Accepted) apply();
    else AutoBackup::saveConfig(before);
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Einstellungen"));
    auto *lay = new QVBoxLayout(this);

    // Darstellung
    auto *look = new QGroupBox(tr("Darstellung"), this);
    auto *lf = new QFormLayout(look);
    m_language = new QComboBox(look);
    m_language->addItem(tr("Systemsprache"), QStringLiteral("system"));
    m_language->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->setCurrentIndex(std::max(0, m_language->findData(AppSettings::language())));
    lf->addRow(tr("&Sprache:"), m_language);
    m_theme = new QComboBox(look);
    m_initialTheme = Themes::current();
    for (const QString &t : Themes::available())
        m_theme->addItem(Themes::displayName(t), t);
    m_theme->setCurrentIndex(std::max(0, m_theme->findData(m_initialTheme)));
    lf->addRow(tr("&Theme:"), m_theme);
    connect(m_theme, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        Themes::apply(m_theme->currentData().toString());   // sofortige Vorschau
    });
    auto *hint = new QLabel(tr("<small>Die Sprache wird nach einem Neustart wirksam, das Theme sofort.</small>"), look);
    lf->addRow(hint);
    lay->addWidget(look);

    // Sicherheit & Sprachausgabe
    auto *sec = new QGroupBox(tr("Sicherheit && Sprachausgabe"), this);
    auto *sf = new QFormLayout(sec);
    m_autoLock = new QComboBox(sec);
    m_autoLock->addItem(tr("keine"), 0);
    for (int m : { 10, 20, 30, 60 })
        m_autoLock->addItem(tr("nach %1 Minuten").arg(m), m);
    m_autoLock->setCurrentIndex(std::max(0, m_autoLock->findData(AppSettings::autoLockMinutes())));
    m_autoLock->setToolTip(tr("Nach dieser Zeit ohne Benutzung meldet VitaParms ab. Ungespeicherte "
                              "Eingaben in offenen Dialogen verfallen."));
    sf->addRow(tr("Automatisch &sperren:"), m_autoLock);
    m_voice = new QComboBox(sec);
    for (const auto &[k, label] : Speech::voiceChoices()) m_voice->addItem(label, k);
    m_voice->setCurrentIndex(std::max(0, m_voice->findData(AppSettings::voiceGender())));
    m_voice->setToolTip(tr("„automatisch“ wählt eine installierte Systemstimme der Oberflächensprache "
                           "mit passendem Geschlecht"));
    sf->addRow(tr("&Stimme (Vorlesen):"), m_voice);
    m_rate = new QSlider(Qt::Horizontal, sec);
    m_rate->setRange(-5, 5);
    m_rate->setValue(qRound(AppSettings::speechRate() * 10));
    m_rate->setTickPosition(QSlider::TicksBelow);
    sf->addRow(tr("Sprech&tempo:"), m_rate);
    m_leafletOnline = new QCheckBox(tr("Beipackzettel im &Internet nachschlagen"), sec);
    m_leafletOnline->setChecked(Leaflet::onlineAllowed());
    m_leafletOnline->setToolTip(tr("Übertragen werden nur Name, Stärke, Wirkstoff bzw. PZN eines Medikaments"));
    auto *srcBtn = new QPushButton(tr("Eigene &Quellen…"), sec);
    connect(srcBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(this, tr("Eigene Beipackzettel-Quellen"),
            tr("Eine Quelle je Zeile: <b>Name | Adresse</b><br>Platzhalter: {pzn} {pzn7} {name} {substance} "
               "{strength} {slug}<br>Beispiel: <tt>Hersteller-Suche | https://example.com/suche?q={name}</tt>"),
            Leaflet::customSourcesText(), &ok);
        if (ok) Leaflet::setCustomSourcesText(t);
    });
    auto *leafRow = new QHBoxLayout;
    leafRow->addWidget(m_leafletOnline);
    leafRow->addWidget(srcBtn);
    leafRow->addStretch();
    sf->addRow(tr("Beipackzettel:"), leafRow);
    if (Session::isAdmin()) {
        auto *smtp = new QPushButton(tr("E-&Mail-Versand für Erinnerungen einrichten…"), sec);
        connect(smtp, &QPushButton::clicked, this, [this] { SmtpDialog d(this); d.exec(); });
        sf->addRow(tr("Terminerinnerung:"), smtp);
        auto *web = new QPushButton(tr("&Website einrichten…"), sec);
        web->setObjectName(QStringLiteral("websiteButton"));
        connect(web, &QPushButton::clicked, this, [this] { WebsiteDialog d(this); d.exec(); });
        sf->addRow(tr("Website:"), web);
        auto *backupRow = new QHBoxLayout;
        auto *backupState = new QLabel(backupSummary(), sec);
        backupState->setObjectName(QStringLiteral("backupSummary"));
        auto *backupBtn = new QPushButton(tr("Ein&stellen…"), sec);
        backupBtn->setObjectName(QStringLiteral("backupSettings"));
        backupRow->addWidget(backupState, 1);
        backupRow->addWidget(backupBtn);
        connect(backupBtn, &QPushButton::clicked, this, [this, backupState] {
            editAutoBackup(this);
            backupState->setText(backupSummary());
        });
        sf->addRow(tr("Automatische Sicherung:"), backupRow);
        m_upcomingOnStart = new QCheckBox(tr("Termine von heute und morgen beim Programmstart &anzeigen"), sec);
        m_upcomingOnStart->setObjectName(QStringLiteral("upcomingOnStartCheck"));
        m_upcomingOnStart->setChecked(Upcoming::showOnStart());
        sf->addRow(QString(), m_upcomingOnStart);
        m_quarter = new QCheckBox(tr("An den &Quartalswechsel erinnern (KV-Karte in den Praxen einlesen lassen)"), sec);
        m_quarter->setObjectName(QStringLiteral("quarterReminderCheck"));
        m_quarter->setChecked(QuarterReminder::enabled());
        sf->addRow(QString(), m_quarter);
    }
    lay->addWidget(sec);

    // Hauptbenutzer
    auto *users = new QGroupBox(tr("Hauptbenutzer (Schnellzugriff)"), this);
    auto *uf = new QFormLayout(users);
    const auto patients = Database::instance().patients();
    for (int i = 0; i < 2; ++i) {
        m_primary[i] = new QComboBox(users);
        m_primary[i]->addItem(tr("— nicht belegt —"), 0);
        for (const Patient &p : patients)
            m_primary[i]->addItem(p.displayName(), p.id);
        const int idx = m_primary[i]->findData(AppSettings::primaryUser(i + 1));
        m_primary[i]->setCurrentIndex(idx < 0 ? 0 : idx);
        uf->addRow(tr("Hauptbenutzer %1 (Strg+%1):").arg(i + 1), m_primary[i]);
    }
    lay->addWidget(users);

    // Einheiten
    auto *units = new QGroupBox(tr("Einheit für Blutzucker"), this);
    auto *ul = new QHBoxLayout(units);
    m_mgdl = new QRadioButton(tr("mg/dl"), units);
    m_mmol = new QRadioButton(tr("mmol/l"), units);
    ul->addWidget(m_mgdl);
    ul->addWidget(m_mmol);
    ul->addStretch();
    (AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL ? m_mmol : m_mgdl)->setChecked(true);
    lay->addWidget(units);

    // Messzeitpunkte
    auto *ctx = new QGroupBox(tr("Messzeitpunkte für Tagesprofile (einer pro Zeile)"), this);
    auto *cl = new QVBoxLayout(ctx);
    m_contexts = new QPlainTextEdit(ctx);
    m_contexts->setPlainText(AppSettings::profileContexts().join(QLatin1Char('\n')));
    cl->addWidget(m_contexts);
    auto *reset = new QPushButton(tr("Standardliste wiederherstellen"), ctx);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_contexts->setPlainText(AppSettings::defaultProfileContexts().join(QLatin1Char('\n')));
    });
    cl->addWidget(reset, 0, Qt::AlignLeft);
    lay->addWidget(ctx);

    auto *db = new QLabel(tr("<small>Datenbank: %1</small>").arg(Database::instance().path().toHtmlEscaped()), this);
    db->setTextInteractionFlags(Qt::TextSelectableByMouse);
    db->setWordWrap(true);
    lay->addWidget(db);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Abbrechen"));
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    lay->addWidget(buttons);
    resize(520, 760);
}

void SettingsDialog::reject()
{
    if (Themes::current() != m_initialTheme)
        Themes::apply(m_initialTheme);
    QDialog::reject();
}

void SettingsDialog::accept()
{
    const QString lang = m_language->currentData().toString();
    if (lang != AppSettings::language()) {
        AppSettings::setLanguage(lang);
        m_restart = QMessageBox::question(this, tr("Sprache"),
            tr("Die Sprache wird beim nächsten Start übernommen.\nVitaParms jetzt neu starten? "
               "(Sie müssen sich danach erneut anmelden.)")) == QMessageBox::Yes;
    }
    AppSettings::setAutoLockMinutes(m_autoLock->currentData().toInt());
    AppSettings::setVoiceGender(m_voice->currentData().toString());
    Leaflet::setOnlineAllowed(m_leafletOnline->isChecked());
    Upcoming::setShowOnStart(m_upcomingOnStart->isChecked());
    QuarterReminder::setEnabled(m_quarter->isChecked());
    AppSettings::setSpeechRate(m_rate->value() / 10.0);
    for (int i = 0; i < 2; ++i)
        AppSettings::setPrimaryUser(i + 1, m_primary[i]->currentData().toLongLong());
    AppSettings::setGlucoseUnit(m_mmol->isChecked() ? AppSettings::GlucoseUnit::MmolL
                                                    : AppSettings::GlucoseUnit::MgDl);
    QStringList l;
    for (const QString &s : m_contexts->toPlainText().split(QLatin1Char('\n')))
        if (!s.trimmed().isEmpty()) l << s.trimmed();
    AppSettings::setProfileContexts(l);
    QDialog::accept();
}
