// VitaParms – Tagebuch für Vitalwerte
// Copyright (c) 2026 Michael Bergmann
// SPDX-License-Identifier: MIT
#include "accountdialogs.h"
#include "appsettings.h"
#include "database.h"
#include "demodata.h"
#include "edition.h"
#include "mainwindow.h"
#include "session.h"
#include "i18n.h"
#include "themes.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QSettings>
#include <QLabel>
#include <QHBoxLayout>
#include <QDialog>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QProgressDialog>
#include <QTranslator>

static bool confirmDisclaimer()
{
    if (AppSettings::disclaimerAccepted())
        return true;
    QMessageBox box(QMessageBox::Information, Edition::appName(),
        QObject::tr("<p><b>Willkommen bei %1!</b></p>"
                    "<p>%1 ist ein <b>persönliches Tagebuch</b> für gesundheitsbezogene "
                    "Messwerte – <b>kein Medizinprodukt</b>. Es stellt keine Diagnosen und "
                    "ersetzt keine ärztliche Beratung. Die Verwendung erfolgt auf eigene Gefahr.</p>"
                    "<p>Die Daten werden verschlüsselt gespeichert. Bewahren Sie den "
                    "Wiederherstellungsschlüssel sicher auf und legen Sie regelmäßig Sicherungen an.</p>").arg(Edition::appName()));
    QPushButton *ok = box.addButton(QObject::tr("Verstanden"), QMessageBox::AcceptRole);
    box.addButton(QObject::tr("Beenden"), QMessageBox::RejectRole);
    auto *dontShow = new QCheckBox(QObject::tr("Diesen Hinweis nicht mehr anzeigen"), &box);
    dontShow->setChecked(true);
    box.setCheckBox(dontShow);
    box.exec();
    if (box.clickedButton() != ok)
        return false;
    if (dontShow->isChecked())
        AppSettings::setDisclaimerAccepted(true);
    return true;
}

// Allererster Start (noch keine Datenbank, Sprache nie gewählt): Sprache wählen lassen –
// zweisprachig, vor dem Laden der Übersetzungen, damit kein Neustart nötig ist.
static void chooseLanguageOnFirstStart()
{
    QSettings st;
    if (st.value(QStringLiteral("ui/languageChosen"), false).toBool()) return;
    if (QCoreApplication::arguments().contains(QStringLiteral("--demo"))
        || Database::fileHasContent(AppSettings::databasePath())) {
        st.setValue(QStringLiteral("ui/languageChosen"), true);   // bestehende Installation: nicht fragen
        return;
    }
    LanguageDialog d;
    d.exec();
    const QString chosen = d.chosen();
    if (chosen.isEmpty()) return;   // geschlossen: Systemsprache, beim nächsten Start erneut fragen
    AppSettings::setLanguage(chosen);
    st.setValue(QStringLiteral("ui/languageChosen"), true);
}

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5: hochauflösende Bildschirme (Retina, Windows-Skalierung) wie unter Qt 6 berücksichtigen
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("VitaParms"));
    // VitaParms light: eigene Einstellungen und eigener Datenordner (beide Ausgaben laufen nebeneinander)
    QApplication::setApplicationName(Edition::settingsName());
    QApplication::setApplicationDisplayName(Edition::appName());
    QApplication::setApplicationVersion(QStringLiteral(VITAPARMS_VERSION));
    QApplication::setWindowIcon(QIcon(Edition::light() ? QStringLiteral(":/icons/vitaparms-light.png")
                                                       : QStringLiteral(":/icons/vitaparms.png")));

    // Sprache (beim allerersten Start wählen lassen, sonst Einstellung bzw. Systemsprache) und Erscheinungsbild
    chooseLanguageOnFirstStart();
    I18n::install(app);
    Themes::init();

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Tagebuch für Vitalwerte"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dbOpt(QStringList{ QStringLiteral("d"), QStringLiteral("database") },
                             QObject::tr("Datenbankdatei verwenden"), QObject::tr("datei"));
    parser.addOption(dbOpt);
    QCommandLineOption demoOpt(QStringLiteral("demo"), QObject::tr("Demo-Datenbank mit Beispieldaten starten"));
    parser.addOption(demoOpt);
    parser.process(app);

    if (!confirmDisclaimer())
        return 0;

    if (parser.isSet(demoOpt)) {
        // Demo: eigene Einstellungen und eigene Datenbank, wird jedes Mal neu erzeugt
        DemoData::enterDemoSettings();
        QApplication::setApplicationDisplayName(QObject::tr("%1 – Demo").arg(Edition::appName()));
        QProgressDialog progress(QObject::tr("Demo-Datenbank wird erstellt …"), QString(), 0, 100);
        progress.setWindowTitle(QObject::tr("%1 – Demo").arg(Edition::appName()));
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        qint64 pid = 0;
        QString err;
        const bool ok = DemoData::create(DemoData::databasePath(), &pid, &err, [&progress](int v) {
            progress.setValue(v);
            QCoreApplication::processEvents();
        });
        progress.close();
        if (!ok) {
            QMessageBox::critical(nullptr, QObject::tr("%1 – Demo").arg(Edition::appName()),
                                  QObject::tr("Die Demo-Datenbank konnte nicht erstellt werden:\n%1").arg(err));
            return 1;
        }
        AppSettings::setPrimaryUser(1, pid);
        AppSettings::setLastPatient(pid);
        MainWindow w;
        w.show();
        QTimer::singleShot(400, &w, [&w] { w.showStartupAppointments(); w.remindStock(); });
        const int rc = app.exec();
        Database::instance().close();
        Session::clear();
        return rc;
    }

    const QString dbPath = parser.isSet(dbOpt) ? QDir::current().absoluteFilePath(parser.value(dbOpt))
                                               : AppSettings::databasePath();
    // Anmeldung bzw. Ersteinrichtung; ohne gültige Anmeldung kein Zugriff
    if (!Session::openInteractive(nullptr, dbPath))
        return 0;
    if (!parser.isSet(dbOpt))
        AppSettings::setDatabasePath(dbPath);

    MainWindow w;
    w.show();
    // Termine heute und morgen der gewählten Person (abschaltbar)
    QTimer::singleShot(400, &w, [&w] { w.showStartupAppointments(); w.remindStock(); });
    const int rc = app.exec();
    Database::instance().close();
    Session::clear();
    return rc;
}
