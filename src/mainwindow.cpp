// SPDX-License-Identifier: MIT
#include "mainwindow.h"
#include "dbtransfer.h"
#include "medstock.h"
#include "protocoldialog.h"
#include "stool.h"
#include "stockdialog.h"
#include "careinfo.h"
#include "dbsync.h"
#include "appsettings.h"
#include "chartwidget.h"
#include "database.h"
#include "i18n.h"
#include "inactivity.h"
#include "appointmentspage.h"
#include "medassistant.h"
#include "medlogdialog.h"
#include "upcoming.h"
#include "medsearchdialog.h"
#include "reminderservice.h"
#include "accountdialogs.h"
#include "session.h"
#include "documentspage.h"
#include "formcustomizedialog.h"
#include "journalmodel.h"
#include "patientdialog.h"
#include "demodata.h"
#include "websitedialog.h"
#include "usbsyncdialog.h"
#include "glucoseprofiledialog.h"
#include "glucoseprofile.h"
#include "websync.h"
#include "manual.h"
#include "reportprinter.h"
#include "settingsdialog.h"
#include "vitaldefs.h"
#include "vitaldialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
enum Period { All, Days7, Days30, Days90, Year, Custom };
enum TabIndex { TabJournal = 0, TabChart = 1, TabDocs = 2, TabAppts = 3 };
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(appTitle());
    setWindowIcon(QIcon(QStringLiteral(":/icons/vitaparms.png")));
    buildUi();
    buildMenus();

    Database &db = Database::instance();
    connect(&db, &Database::patientsChanged, this, &MainWindow::reloadPatients);
    connect(&db, &Database::stockChanged, this, [this](qint64) { updateHeader(); });
    connect(&db, &Database::vitalsChanged, this, [this](qint64 pid) {
        if (pid == m_patientId) { reloadJournal(); reloadChart(); updateHeader(); }
    });
    {   // Hinweise mit Tagesbezug (z. B. Tage ohne Stuhlgang) auch nach Mitternacht aktuell halten
        auto *dayTimer = new QTimer(this);
        dayTimer->setInterval(15 * 60 * 1000);
        connect(dayTimer, &QTimer::timeout, this, [this] { if (Database::instance().isOpen()) updateHeader(); });
        dayTimer->start();
    }
    connect(&db, &Database::databaseReopened, this, [this] {
        m_patientId = 0;
        reloadPatients();
        statusBar()->showMessage(tr("Datenbank: %1").arg(QDir::toNativeSeparators(Database::instance().path())));
    });

    if (m_appts) m_appts->reload();          // Terminliste sofort füllen, nicht erst nach Änderungen
    reloadPatients();
    qint64 last = AppSettings::lastPatient();
    if (!last) last = AppSettings::primaryUser(1);
    if (last) selectPatient(last);
    else if (m_userCombo->count() > 0) onUserComboChanged(m_userCombo->currentIndex());

    statusBar()->showMessage(tr("Datenbank: %1").arg(QDir::toNativeSeparators(db.path())));
    m_sessionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_sessionLabel);
    updateSessionInfo();

    // Automatische Sperre bei Nichtbenutzung
    m_idle = new InactivityWatcher(this);
    m_idle->setMinutes(AppSettings::autoLockMinutes());
    connect(m_idle, &InactivityWatcher::idleTimeout, this, &MainWindow::lockForInactivity);

    // Medikamente mit abgelaufenem Enddatum stündlich in die Historie verschieben (z. B. über Mitternacht)
    auto *expire = new QTimer(this);
    expire->setInterval(60 * 60 * 1000);
    connect(expire, &QTimer::timeout, this, [] { Database::instance().archiveExpiredMedications(); });
    expire->start();

    // Terminerinnerungen (nur für das angemeldete Konto, solange das Programm läuft)
    // Website: nach Änderungen automatisch aktualisieren (falls eingerichtet), Meldungen in der Statusleiste
    if (!DemoData::isActive()) {
        WebSync &ws = WebSync::instance();
        ws.watchDatabase();
        connect(&ws, &WebSync::started, this, [this] { statusBar()->showMessage(tr("Website wird aktualisiert …")); });
        connect(&ws, &WebSync::finished, this, [this](bool ok, const QString &msg) {
            statusBar()->showMessage(ok ? msg : tr("Website: %1").arg(msg), ok ? 8000 : 20000);
        });
        // Abgleich mit anderen Geräten (Tablet): Stand nach dem Start holen, Änderungen verzögert senden
        DbSync &ds = DbSync::instance();
        DbSync::adoptInitialState();
        ds.watchDatabase();
        connect(&ds, &DbSync::started, this, [this] { statusBar()->showMessage(tr("Datenbank wird abgeglichen …")); });
        connect(&ds, &DbSync::finished, this, [this](bool ok, const QString &msg) {
            statusBar()->showMessage(ok ? tr("Abgleich: %1").arg(msg) : tr("Abgleich fehlgeschlagen: %1").arg(msg), ok ? 8000 : 20000);
        });
        QTimer::singleShot(4000, this, [] {
            const DbSync::Config c = DbSync::config();
            if (c.autoSync && DbSync::isReady(c)) DbSync::instance().sync();
        });
    } else {
        m_actWebPublish->setEnabled(false);
        m_actDbSync->setEnabled(false);
    }

    m_reminders = new ReminderService(this);
    connect(m_reminders, &ReminderService::statusMessage, this, [this](const QString &t) {
        statusBar()->showMessage(t, 15000);
    });
    connect(m_reminders, &ReminderService::openRequested, this, [this](qint64 id) {
        showNormal();
        raise();
        activateWindow();
        m_tabs->setCurrentWidget(m_appts);
        m_appts->editAppointmentId(id);
    });
    connect(m_reminders, &ReminderService::measureRequested, this, &MainWindow::measureForAppointment);
    connect(this, &MainWindow::aboutToLock, m_reminders, [this] { m_reminders->setEnabled(false); });
    m_reminders->setEnabled(true);

    const QByteArray geo = AppSettings::windowState(QStringLiteral("main/geometry"));
    if (!geo.isEmpty()) restoreGeometry(geo);
    else resize(1150, 760);
    restoreState(AppSettings::windowState(QStringLiteral("main/state")));
    updateActions();
}

// ======================================================================= UI

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *v = new QVBoxLayout(central);

    if (DemoData::isActive()) {
        // Hinweisband: Demo-Modus, eigene Daten bleiben unberührt
        auto *banner = new QLabel(central);
        banner->setObjectName(QStringLiteral("demoBanner"));
        banner->setWordWrap(true);
        banner->setTextFormat(Qt::RichText);
        banner->setText(tr("<b>Demo-Modus</b> – Beispieldaten einer erfundenen Person über %1 Tage. "
                           "Probieren Sie alles aus: Änderungen sind erlaubt und werden beim nächsten Start der "
                           "Demo verworfen. Ihre eigenen Daten bleiben unberührt. "
                           "Beenden: <i>Hilfe → Demo beenden</i>.").arg(DemoData::kDays));
        banner->setStyleSheet(QStringLiteral("QLabel { background: #FFF4D6; color: #3a2a00; border: 1px solid #D9601A;"
                                             " border-radius: 3px; padding: 6px 10px; }"));
        v->addWidget(banner);
    }

    m_header = new QLabel(central);
    m_header->setObjectName(QStringLiteral("patientHeader"));
    m_header->setTextFormat(Qt::RichText);
    m_header->setWordWrap(true);
    m_header->setContentsMargins(6, 4, 6, 4);
    m_header->setFrameShape(QFrame::StyledPanel);
    v->addWidget(m_header);

    m_tabs = new QTabWidget(central);
    v->addWidget(m_tabs, 1);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateActions);

    // ---------------------------------------------------------- Journal
    auto *jw = new QWidget(m_tabs);
    auto *jl = new QVBoxLayout(jw);
    auto *filter = new QHBoxLayout;
    filter->addWidget(new QLabel(tr("Zeitraum:"), jw));
    m_periodCombo = new QComboBox(jw);
    m_periodCombo->addItems({ tr("Alle Messungen"), tr("Letzte 7 Tage"), tr("Letzte 30 Tage"),
                              tr("Letzte 90 Tage"), tr("Letztes Jahr"), tr("Von – bis") });
    m_periodCombo->setCurrentIndex(Days30);
    filter->addWidget(m_periodCombo);
    m_fromEdit = new QDateEdit(QDate::currentDate().addMonths(-1), jw);
    m_toEdit = new QDateEdit(QDate::currentDate(), jw);
    for (QDateEdit *e : { m_fromEdit, m_toEdit }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        connect(e, &QDateEdit::dateChanged, this, &MainWindow::reloadJournal);
    }
    filter->addWidget(m_fromEdit);
    auto *dash = new QLabel(QStringLiteral("–"), jw);
    filter->addWidget(dash);
    filter->addWidget(m_toEdit);
    connect(m_periodCombo, &QComboBox::currentIndexChanged, dash, [this, dash] {
        dash->setVisible(m_periodCombo->currentIndex() == Custom);
    });
    dash->setVisible(false);
    m_admissionCheck = new QCheckBox(tr("Aufnahmewerte zeigen"), jw);
    m_admissionCheck->setChecked(true);
    filter->addWidget(m_admissionCheck);
    filter->addStretch();
    m_countLabel = new QLabel(jw);
    filter->addWidget(m_countLabel);
    jl->addLayout(filter);
    connect(m_periodCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onPeriodChanged);
    connect(m_admissionCheck, &QCheckBox::toggled, this, &MainWindow::reloadJournal);

    m_model = new JournalModel(this);
    m_table = new QTableView(jw);
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setWordWrap(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::editVital);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    jl->addWidget(m_table, 1);
    m_journalButtons = new QHBoxLayout;
    jl->addLayout(m_journalButtons);
    m_tabs->addTab(jw, tr("&Journal / Verlauf"));

    // ---------------------------------------------------------- Grafik
    auto *cw = new QWidget(m_tabs);
    auto *cl = new QVBoxLayout(cw);
    auto *ctrl = new QHBoxLayout;
    ctrl->addWidget(new QLabel(tr("Darstellung:"), cw));
    m_scaleCombo = new QComboBox(cw);
    m_scaleCombo->addItems({ tr("Tagesverlauf"), tr("Wochenverlauf"), tr("Monatsverlauf"), tr("Jahresverlauf"),
                             tr("Freier Zeitraum") });
    m_scaleCombo->setCurrentIndex(AppSettings::chartScale());
    ctrl->addWidget(m_scaleCombo);

    m_navigation = new QWidget(cw);
    auto *nav = new QHBoxLayout(m_navigation);
    nav->setContentsMargins(0, 0, 0, 0);
    auto *prev = new QToolButton(m_navigation);
    prev->setArrowType(Qt::LeftArrow);
    prev->setToolTip(tr("Zurück"));
    auto *next = new QToolButton(m_navigation);
    next->setArrowType(Qt::RightArrow);
    next->setToolTip(tr("Weiter"));
    m_refDate = new QDateEdit(QDate::currentDate(), m_navigation);
    m_refDate->setCalendarPopup(true);
    m_refDate->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    auto *today = new QPushButton(tr("Heute"), m_navigation);
    nav->addWidget(prev);
    nav->addWidget(m_refDate);
    nav->addWidget(next);
    nav->addWidget(today);
    ctrl->addWidget(m_navigation);
    connect(prev, &QToolButton::clicked, this, [this] { stepChart(-1); });
    connect(next, &QToolButton::clicked, this, [this] { stepChart(+1); });
    connect(today, &QPushButton::clicked, this, [this] { m_refDate->setDate(QDate::currentDate()); });
    connect(m_refDate, &QDateEdit::dateChanged, this, &MainWindow::reloadChart);

    m_customRange = new QWidget(cw);
    auto *cr = new QHBoxLayout(m_customRange);
    cr->setContentsMargins(0, 0, 0, 0);
    m_chartFrom = new QDateEdit(QDate::currentDate().addMonths(-3), m_customRange);
    m_chartTo = new QDateEdit(QDate::currentDate(), m_customRange);
    for (QDateEdit *e : { m_chartFrom, m_chartTo }) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        connect(e, &QDateEdit::dateChanged, this, &MainWindow::reloadChart);
    }
    cr->addWidget(m_chartFrom);
    cr->addWidget(new QLabel(QStringLiteral("–"), m_customRange));
    cr->addWidget(m_chartTo);
    ctrl->addWidget(m_customRange);
    m_customRange->hide();
    ctrl->addStretch();
    cl->addLayout(ctrl);
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onScaleChanged);
    m_customRange->setVisible(m_scaleCombo->currentIndex() == 4);
    m_navigation->setVisible(m_scaleCombo->currentIndex() != 4);

    auto *panels = new QHBoxLayout;
    panels->addWidget(new QLabel(tr("Bereiche:"), cw));
    const QStringList sel = AppSettings::chartSeries();
    for (const auto &pd : VitalDefs::panels()) {
        auto *c = new QCheckBox(QString(pd.title).replace(QLatin1Char('&'), QStringLiteral("&&")), cw);
        c->setProperty("panelId", pd.id);
        c->setChecked(sel.contains(pd.id));
        connect(c, &QCheckBox::toggled, this, [this] {
            QStringList ids;
            for (QCheckBox *x : m_panelChecks)
                if (x->isChecked()) ids << x->property("panelId").toString();
            AppSettings::setChartSeries(ids);
            reloadChart();
        });
        m_panelChecks << c;
        panels->addWidget(c);
    }
    panels->addSpacing(12);
    // Kurven einzeln ein-/ausblenden (auch per Klick auf die Legende)
    m_seriesButton = new QToolButton(cw);
    m_seriesButton->setText(tr("&Kurven"));
    m_seriesButton->setToolTip(tr("Einzelne Kurven ein- oder ausblenden – auch per Klick auf die Legende"));
    m_seriesButton->setPopupMode(QToolButton::InstantPopup);
    m_seriesButton->setMenu(new QMenu(m_seriesButton));
    panels->addWidget(m_seriesButton);
    panels->addStretch();
    cl->addLayout(panels);
    rebuildSeriesMenu();

    m_chart = new ChartWidget(cw);
    // Bei vielen Bereichen oder kleinem Fenster scrollen statt die Kurven wegzulassen
    auto *chartScroll = new QScrollArea(cw);
    chartScroll->setObjectName(QStringLiteral("chartScroll"));
    chartScroll->setWidgetResizable(true);
    chartScroll->setFrameShape(QFrame::NoFrame);
    chartScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chartScroll->setWidget(m_chart);
    cl->addWidget(chartScroll, 1);
    connect(m_chart, &ChartWidget::legendClicked, this, &MainWindow::toggleSeries);

    auto *outRow = new QHBoxLayout;
    m_separatePages = new QCheckBox(tr("Jeden Bereich auf einer eigenen &Seite ausgeben"), cw);
    m_separatePages->setToolTip(tr("Druck und PDF: jede Verlaufskurve auf einem eigenen A4-Blatt, jeweils mit Legende und Kennwerten"));
    m_separatePages->setChecked(AppSettings::chartSeparatePages());
    connect(m_separatePages, &QCheckBox::toggled, this, [this](bool b) {
        AppSettings::setChartSeparatePages(b);
        reloadChart();
    });
    outRow->addWidget(m_separatePages);
    outRow->addStretch();
    auto *bPrint = new QPushButton(tr("Drucken (A4)…"), cw);
    auto *bPdf = new QPushButton(tr("Als PDF…"), cw);
    auto *bXls = new QPushButton(tr("Nach Excel…"), cw);
    outRow->addWidget(bPrint);
    outRow->addWidget(bPdf);
    outRow->addWidget(bXls);
    cl->addLayout(outRow);
    connect(bPrint, &QPushButton::clicked, this, &MainWindow::printCurrent);
    connect(bPdf, &QPushButton::clicked, this, &MainWindow::pdfCurrent);
    connect(bXls, &QPushButton::clicked, this, &MainWindow::excelCurrent);

    m_tabs->addTab(cw, tr("&Grafik"));

    // ---------------------------------------------------------- Krankenakte
    m_docs = new DocumentsPage(m_tabs);
    m_tabs->addTab(m_docs, tr("&Dokumente (Krankenakte)"));
    connect(m_docs, &DocumentsPage::selectionChanged, this, &MainWindow::updateActions);

    // ---------------------------------------------------------- Termine
    m_appts = new AppointmentsPage(m_tabs);
    m_tabs->addTab(m_appts, tr("&Termine"));
    setCentralWidget(central);
    onPeriodChanged();
}

void MainWindow::buildMenus()
{
    // ---- Datei
    QMenu *file = menuBar()->addMenu(tr("&Datei"));
    m_actNewDb = file->addAction(tr("&Neue Datenbank…"), this, &MainWindow::newDatabase);
    m_actOpenDb = file->addAction(tr("Datenbank ö&ffnen…"), this, &MainWindow::openDatabase);
    file->addSeparator();
    m_actBackup = file->addAction(tr("&Sicherung erstellen…"), this, &MainWindow::backupDatabase);
    m_actRestore = file->addAction(tr("Sicherung &wiederherstellen / importieren…"), this, &MainWindow::restoreDatabase);
    m_actExportDb = file->addAction(tr("Datenbank e&xportieren (für ein anderes Gerät)…"), this, &MainWindow::exportDatabase);
    m_actExportDb->setStatusTip(tr("Datenbank und Anmeldekonten als eine Datei (*.vpdb) – z. B. per USB auf das Tablet"));
    file->addSeparator();
    m_actWebPublish = file->addAction(tr("&Website aktualisieren"), this, [this] {
        QString why;
        if (!WebSync::isReady(WebSync::config(), &why)) {
            if (QMessageBox::question(this, tr("Website"), why + QStringLiteral("\n\n") + tr("Website jetzt einrichten?"))
                == QMessageBox::Yes)
                showWebsiteSettings();
            return;
        }
        WebSync::instance().publish();
    });
    m_actWebPublish->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
    m_actWebPublish->setStatusTip(tr("Lädt die verschlüsselten Daten aller Personen auf die eigene Website"));
    m_actDbSync = file->addAction(tr("Datenbank a&bgleichen"), this, [this] {
        QString why;
        if (!DbSync::isReady(DbSync::config(), &why)) {
            if (QMessageBox::question(this, tr("Abgleich"), why + QStringLiteral("\n\n") + tr("Abgleich jetzt einrichten?"))
                == QMessageBox::Yes)
                showWebsiteSettings();
            return;
        }
        QApplication::setOverrideCursor(Qt::BusyCursor);
        DbSync::instance().sync();
        QApplication::restoreOverrideCursor();
    });
    m_actDbSync->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    m_actDbSync->setStatusTip(tr("Gleicht die verschlüsselte Datenbank über die eigene Website mit anderen Geräten (Tablet) ab"));
    QAction *usb = file->addAction(tr("Mit &Tablet über USB abgleichen…"), this, [this] { showUsbSync(false); });
    usb->setObjectName(QStringLiteral("actUsbSync"));
    usb->setStatusTip(tr("Gleicht die Datenbank direkt über das USB-Kabel mit der Tablet- oder Smartphone-App ab"));
    file->addSeparator();
    QAction *lock = file->addAction(tr("&Abmelden / Sperren"), this, &MainWindow::logout);
    lock->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Beenden"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // ---- Benutzer
    QMenu *users = menuBar()->addMenu(tr("&Benutzer"));
    QAction *np = users->addAction(QIcon::fromTheme(QStringLiteral("contact-new")), tr("&Neuer Benutzer…"),
                                   this, &MainWindow::newPatient);
    np->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    m_actEditPatient = users->addAction(QIcon::fromTheme(QStringLiteral("document-properties")),
                                        tr("&Stammblatt…"), this, &MainWindow::editPatient);
    m_actEditPatient->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_actDeletePatient = users->addAction(tr("Benutzer &löschen…"), this, &MainWindow::deletePatient);
    users->addSeparator();
    m_actMedAssist = users->addAction(QIcon::fromTheme(QStringLiteral("audio-volume-high")),
                                      tr("&Medikamente stellen (Assistent)…"), this, &MainWindow::medicationAssistant);
    m_actMedAssist->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_actMedStock = users->addAction(tr("Medikamenten&bestand und Bestellungen…"), this, &MainWindow::medicationStock);
    m_actMedStock->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    m_actMedStock->setStatusTip(tr("Packungen mit Verfallsdatum, Verbrauch beim Stellen, Reichweite und Bestelllisten"));
    {
        QMenu *search = users->addMenu(tr("Medikamente &suchen"));
        for (int g = 0; g < DrugClass::GroupCount; ++g) {
            QAction *act = search->addAction(DrugClass::groupName(DrugClass::Group(g)) + QStringLiteral("…"), this, [this, g] {
                if (const auto p = currentPatient()) {
                    MedSearchDialog d(*p, Database::instance().medications(p->id, true), g, this);
                    d.exec();
                }
            });
            m_actMedSearch << act;
        }
        m_actMedSearch.first()->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
        search->addSeparator();
        m_actMedSearch << search->addAction(tr("Freitext…"), this, [this] {
            if (const auto p = currentPatient()) {
                MedSearchDialog d(*p, Database::instance().medications(p->id, true), -1, this);
                d.exec();
            }
        });
    }
    m_actMedLog = users->addAction(tr("Protokoll Medikamente s&tellen…"), this, [this] {
        if (const auto p = currentPatient()) { MedLogDialog d(*p, this); d.exec(); }
    });
    {
        QMenu *print = users->addMenu(QIcon::fromTheme(QStringLiteral("document-print")), tr("&Drucken"));
        m_actUserPrint << print->addAction(tr("&Stammblatt…"), this, &MainWindow::printPatientSheet)
                       << print->addAction(tr("&Medikamentenliste…"), this, &MainWindow::printMedicationPlan)
                       << print->addAction(tr("Medikationsliste für die &Apotheke…"), this, &MainWindow::printPharmacyList)
                       << print->addAction(tr("&Erinnerungen…"), this, &MainWindow::printReminders);
        m_actUserPrint.last()->setToolTip(tr("Anstehende, offene Termine der gewählten Person drucken"));
    }
    users->addSeparator();
    for (int i = 0; i < 2; ++i) {
        m_actPrimary[i] = users->addAction(QString(), this, [this, i] { choosePrimary(i + 1); });
        m_actPrimary[i]->setShortcut(QKeySequence(Qt::CTRL | (i == 0 ? Qt::Key_1 : Qt::Key_2)));
    }
    users->addSeparator();
    for (int i = 0; i < 2; ++i)
        m_actAssign[i] = users->addAction(tr("Aktuellen Benutzer als Hauptbenutzer &%1 festlegen").arg(i + 1),
                                          this, [this, i] { assignPrimary(i + 1); });

    // ---- Messwerte
    QMenu *vit = menuBar()->addMenu(tr("&Messwerte"));
    m_actNewVital = vit->addAction(QIcon::fromTheme(QStringLiteral("list-add")), tr("&Neue Messung…"),
                                   this, &MainWindow::newVital);
    m_actNewVital->setShortcuts({ QKeySequence(Qt::CTRL | Qt::Key_M), QKeySequence::New });
    m_actEditVital = vit->addAction(tr("Messung &bearbeiten…"), this, &MainWindow::editVital);
    m_actEditVital->setShortcut(QKeySequence(Qt::Key_F2));
    m_actDeleteVital = vit->addAction(tr("Messung &löschen…"), this, &MainWindow::deleteVital);
    m_actDeleteVital->setShortcut(QKeySequence::Delete);
    vit->addSeparator();
    m_actGlucoseProfile = vit->addAction(tr("&BZ-Tagesprofil…"), this, &MainWindow::startGlucoseProfile);
    m_actGlucoseProfile->setObjectName(QStringLiteral("actGlucoseProfile"));
    m_actGlucoseProfile->setStatusTip(tr("Blutzucker zu sieben Zeitpunkten eines Tages messen – mit Erinnerung und Erfassungsfenster"));

    // ---- Krankenakte
    QMenu *akte = menuBar()->addMenu(tr("&Krankenakte"));
    m_actAddDoc = akte->addAction(tr("Dokumente &hinzufügen…"), this, [this] {
        m_tabs->setCurrentWidget(m_docs);
        m_docs->addDocuments();
    });
    m_actAddDoc->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actPackAkte = akte->addAction(tr("Akte &packen / per E-Mail senden…"), this, [this] {
        m_tabs->setCurrentWidget(m_docs);
        m_docs->packAndSend();
    });
    m_actPackAkte->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_actDocList = akte->addAction(tr("Dokumenten&liste drucken…"), this, [this] {
        m_tabs->setCurrentWidget(m_docs);
        m_docs->printList();
    });
    akte->addAction(tr("Krankenakte &anzeigen"), this, [this] { m_tabs->setCurrentWidget(m_docs); })
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));

    // ---- Termine
    QMenu *appt = menuBar()->addMenu(tr("&Termine"));
    m_actNewAppt = appt->addAction(QIcon::fromTheme(QStringLiteral("appointment-new")), tr("&Neuer Termin…"), this, [this] {
        m_tabs->setCurrentWidget(m_appts);
        m_appts->newAppointment();
    });
    m_actNewAppt->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    appt->addAction(tr("Termin&planer anzeigen"), this, [this] { m_tabs->setCurrentWidget(m_appts); })
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    appt->addAction(tr("Erinnerungen jetzt &prüfen"), this, [this] { if (m_reminders) m_reminders->checkNow(); });
    m_actUserPrint << appt->addAction(tr("Termine &heute und morgen…"), this, [this] { showStartupAppointments(true); });
    appt->addSeparator();
    appt->addAction(tr("Termine e&xportieren (iCalendar)…"), this, [this] {
        m_tabs->setCurrentWidget(m_appts);
        m_appts->exportICal();
    });

    // ---- Ausgabe
    QMenu *out = menuBar()->addMenu(tr("&Ausgabe"));
    m_actPrint = out->addAction(QIcon::fromTheme(QStringLiteral("document-print")), tr("&Drucken…"),
                                this, &MainWindow::printCurrent);
    m_actPrint->setShortcut(QKeySequence::Print);
    m_actPreview = out->addAction(tr("Druck&vorschau…"), this, &MainWindow::previewCurrent);
    m_actPreview->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    m_actPdf = out->addAction(tr("Als &PDF speichern…"), this, &MainWindow::pdfCurrent);
    m_actExcel = out->addAction(tr("Nach &Excel exportieren…"), this, &MainWindow::excelCurrent);
    m_actExcel->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    out->addSeparator();
    m_actSheet = out->addAction(tr("&Stammblatt drucken…"), this, &MainWindow::printPatientSheet);
    m_actMedPlan = out->addAction(tr("&Medikamentenplan drucken…"), this, &MainWindow::printMedicationPlan);
    m_actPharmacy = out->addAction(tr("Medikationsliste für die &Apotheke…"), this, &MainWindow::printPharmacyList);
    m_actPharmacy->setStatusTip(tr("Alle aktuellen Medikamente mit Wirkstoff, PZN, Gewicht, Allergien und Diagnosen – "
                                   "zum Prüfen auf Wechselwirkungen in der Apotheke"));
    out->addSeparator();
    // Protokolle aus den erfassten Werten der aktiven Person
    m_actUserPrint << out->addAction(tr("St&uhlprotokoll…"), this, [this] { showProtocol(Protocols::Kind::Stool); });
    m_actUserPrint.last()->setObjectName(QStringLiteral("actStoolProtocol"));
    m_actUserPrint.last()->setStatusTip(tr("Stuhlgänge mit Menge, Konsistenz (Bristol 1–7), Farbe, Beimengungen und Begleitsymptomen"));
    m_actUserPrint << out->addAction(tr("BZ-&Tagesprofile…"), this, [this] { showProtocol(Protocols::Kind::Glucose); });
    m_actUserPrint.last()->setObjectName(QStringLiteral("actGlucoseProtocol"));
    m_actUserPrint.last()->setStatusTip(tr("Blutzuckerwerte je Tag nach Profilzeitpunkten, aus den erfassten Messungen zusammengestellt"));

    // ---- Extras
    QMenu *extras = menuBar()->addMenu(tr("E&xtras"));
    extras->addAction(tr("&Einstellungen…"), this, &MainWindow::showSettings)->setShortcut(QKeySequence::Preferences);
    m_actForms = extras->addAction(tr("&Formulare anpassen…"), this, &MainWindow::customizeForms);
    extras->addAction(tr("&Anmeldekonten…"), this, &MainWindow::manageAccounts);
    m_actWebSettings = extras->addAction(tr("&Website…"), this, &MainWindow::showWebsiteSettings);

    // ---- Hilfe
    QMenu *help = menuBar()->addMenu(tr("&Hilfe"));
    m_actManual = help->addAction(QIcon::fromTheme(QStringLiteral("help-contents")), tr("&Handbuch"),
                                  this, [this] { Manual::open(this); });
    m_actManual->setShortcut(QKeySequence(Qt::Key_F1));
    m_actManual->setShortcutContext(Qt::ApplicationShortcut);
    m_actManual->setStatusTip(tr("Öffnet das Benutzerhandbuch"));
    QAction *guide = help->addAction(tr("&Erste Schritte"), this, [this] { Manual::open(this, Manual::Doc::GettingStarted); });
    guide->setObjectName(QStringLiteral("actGettingStarted"));
    guide->setStatusTip(tr("Öffnet die Kurzanleitung mit Beispielen: Messwerte, Grafik, Medikamente, Termine, Tablet"));
    help->addSeparator();
    if (DemoData::isActive()) {
        m_actDemo = help->addAction(tr("Demo &beenden"), this, &QWidget::close);
    } else {
        m_actDemo = help->addAction(tr("&Demo-Datenbank starten…"), this, &MainWindow::startDemo);
        m_actDemo->setStatusTip(tr("Öffnet ein zweites Fenster mit einer Beispielperson und 365 Tagen Messwerten"));
    }
    help->addSeparator();
    help->addAction(tr("&Haftungshinweis…"), this, &MainWindow::showDisclaimer);
    help->addAction(tr("Ü&ber VitaParms…"), this, &MainWindow::about);
    help->addAction(tr("Über &Qt…"), qApp, &QApplication::aboutQt);

    // ---- Schaltflächen unter dem Journal
    for (QAction *a : { m_actNewVital, m_actEditVital, m_actDeleteVital }) {
        auto *b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_journalButtons->addWidget(b);
    }
    m_journalButtons->addStretch();
    for (QAction *a : { m_actPrint, m_actPdf, m_actExcel }) {
        auto *b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_journalButtons->addWidget(b);
    }

    // ---- Werkzeugleiste
    QToolBar *tb = addToolBar(tr("Werkzeugleiste"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addWidget(new QLabel(tr(" Benutzer: "), tb));
    m_userCombo = new QComboBox(tb);
    m_userCombo->setMinimumContentsLength(24);
    m_userCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_userCombo, &QComboBox::activated, this, &MainWindow::onUserComboChanged);
    tb->addWidget(m_userCombo);
    tb->addAction(m_actPrimary[0]);
    tb->addAction(m_actPrimary[1]);
    tb->addSeparator();
    tb->addAction(np);
    tb->addAction(m_actEditPatient);
    tb->addSeparator();
    tb->addAction(m_actNewVital);
    tb->addAction(m_actMedAssist);
    tb->addAction(m_actNewAppt);
    tb->addSeparator();
    tb->addAction(m_actPrint);
}

// ================================================================= Benutzer

void MainWindow::reloadPatients()
{
    const qint64 keep = m_patientId;
    const auto list = Database::instance().patients();
    m_userCombo->blockSignals(true);
    m_userCombo->clear();
    for (const Patient &p : list) {
        QString t = p.displayName();
        if (p.birthDate.isValid())
            t += QStringLiteral(" (") + QLocale().toString(p.birthDate, QStringLiteral("dd.MM.yyyy")) + QLatin1Char(')');
        m_userCombo->addItem(t, p.id);
    }
    m_userCombo->blockSignals(false);

    int idx = m_userCombo->findData(keep);
    if (idx < 0 && m_userCombo->count() > 0) idx = 0;
    m_userCombo->setCurrentIndex(idx);
    m_patientId = idx >= 0 ? m_userCombo->itemData(idx).toLongLong() : 0;
    updatePrimaryActions();
    updateHeader();
    reloadJournal();
    reloadChart();
    if (m_docs) m_docs->setPatient(currentPatient());
    if (m_appts) m_appts->setCurrentPatient(m_patientId);
    updateActions();
}

void MainWindow::selectPatient(qint64 id)
{
    const int idx = m_userCombo->findData(id);
    if (idx < 0) return;
    m_userCombo->setCurrentIndex(idx);
    onUserComboChanged(idx);
}

void MainWindow::onUserComboChanged(int index)
{
    m_patientId = index >= 0 ? m_userCombo->itemData(index).toLongLong() : 0;
    AppSettings::setLastPatient(m_patientId);
    updateHeader();
    reloadJournal();
    reloadChart();
    if (m_docs) m_docs->setPatient(currentPatient());
    if (m_appts) m_appts->setCurrentPatient(m_patientId);
    updateActions();
}

std::optional<Patient> MainWindow::currentPatient() const
{
    if (!m_patientId) return std::nullopt;
    return Database::instance().patient(m_patientId);
}

void MainWindow::updateHeader()
{
    const auto p = currentPatient();
    if (!p) {
        m_header->setText(tr("<b>Kein Benutzer ausgewählt.</b> Legen Sie über "
                             "<i>Benutzer → Neuer Benutzer</i> ein Stammblatt an."));
        setWindowTitle(appTitle());
        return;
    }
    QString t = QStringLiteral("<span style=\"font-size:13pt;\"><b>%1</b></span>").arg(p->displayName().toHtmlEscaped());
    if (p->birthDate.isValid()) {
        t += tr(" &nbsp;·&nbsp; geb. %1").arg(QLocale().toString(p->birthDate, QStringLiteral("dd.MM.yyyy")));
        const int age = p->ageAt(QDate::currentDate());
        if (age >= 0) t += tr(" (%1 Jahre)").arg(age);
    }
    const QString diag = p->diagnoses.trimmed().split(QLatin1Char('\n')).join(QStringLiteral("; "));
    if (!diag.isEmpty())
        t += QStringLiteral("<br><small>%1 %2</small>").arg(tr("Diagnosen:"), diag.toHtmlEscaped());
    const QString allergies = p->extra.value(QStringLiteral("Allergien")).toString().trimmed();
    if (!allergies.isEmpty())
        t += QStringLiteral("<br><small><b>%1</b> %2</small>").arg(tr("Allergien:"),
                 allergies.split(QLatin1Char('\n')).join(QStringLiteral("; ")).toHtmlEscaped());
    // Pflegegrad und Schwerbehindertenausweis (bald fällige Überprüfung/Ablauf orange)
    QStringList care;
    if (p->careLevel > 0) {
        QString c = CareInfo::careLevelText(*p).toHtmlEscaped();
        if (CareInfo::careReviewSoon(*p)) c = QStringLiteral("<span style=\"color:#B35C00;\">%1</span>").arg(c);
        care << c;
    }
    if (p->disabilityDegree > 0) {
        QString c = CareInfo::disabilityText(*p).toHtmlEscaped();
        if (CareInfo::disabilityExpiresSoon(*p)) c = QStringLiteral("<span style=\"color:#B35C00;\">%1</span>").arg(c);
        care << tr("Ausweis: %1").arg(c);
    }
    if (const QString adv = CareInfo::advanceShort(*p); !adv.isEmpty()) {
        QString c = tr("Vorsorge: %1").arg(adv).toHtmlEscaped();
        if (CareInfo::advanceExpiresSoon(*p)) c = QStringLiteral("<span style=\"color:#B35C00;\">%1</span>").arg(c);
        care << c;
    }
    if (!care.isEmpty())
        t += QStringLiteral("<br><small>%1</small>").arg(care.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    QStringList emergency;
    for (const Medication &m : Database::instance().medications(p->id, false))
        if (m.category == Medication::Emergency)
            emergency << (m.name + (m.strength.isEmpty() ? QString() : QLatin1Char(' ') + m.strength)).toHtmlEscaped()
                             + (m.rhythmNote.isEmpty() ? QString() : QStringLiteral(" – ") + m.rhythmNote.toHtmlEscaped());
    if (!emergency.isEmpty())
        t += QStringLiteral("<br><small><span style=\"color:#D52B1E;\"><b>%1</b> %2</span></small>")
                 .arg(tr("Notfallmedikation:"), emergency.join(QStringLiteral("; ")));
    // Bestandsführung: nachzubestellende Medikamente bzw. noch nicht bestätigte Bestellliste
    {
        int need = 0;
        for (const Medication &m : Database::instance().medications(p->id, false)) {
            if (!MedStock::isTracked(m)) continue;
            const MedStock::Forecast f = MedStock::forecast(m);
            if ((f.state == MedStock::OrderNow || f.state == MedStock::Empty) && !f.onOrder) ++need;
        }
        bool openOrder = false;
        for (const MedStock::Order &o : MedStock::orders(p->id)) openOrder |= o.status == MedStock::Open && !o.items.isEmpty();
        QStringList s;
        if (need) s << tr("Medikamente nachbestellen: %1").arg(need);
        if (openOrder) s << tr("Bestellliste noch nicht als bestellt bestätigt");
        if (!s.isEmpty())
            t += QStringLiteral("<br><small><span style=\"color:#B35C00;\"><b>%1</b> %2</span></small>")
                     .arg(tr("Bestand:"), s.join(QStringLiteral(" · ")).toHtmlEscaped());
    }
    // Stuhlgang: seit mindestens drei Tagen nichts eingetragen (nur wenn Stuhlgang erfasst wird)
    {
        const Stool::Status st = Stool::status(Database::instance().vitals(p->id));
        if (st.warn) {
            t += QStringLiteral("<br><small><span style=\"color:#B35C00;\"><b>%1</b> %2</span></small>")
                     .arg(tr("Stuhlgang:"), tr("seit %1 Tagen keiner eingetragen (zuletzt %2)")
                                                .arg(st.daysWithout).arg(QLocale().toString(st.last, QStringLiteral("dd.MM."))).toHtmlEscaped());
            m_header->setToolTip(Stool::warningText(st));
        } else {
            m_header->setToolTip(QString());
        }
    }
    m_header->setText(t);
    setWindowTitle(QStringLiteral("%1 – %2").arg(appTitle(), p->displayName()));
}

void MainWindow::updatePrimaryActions()
{
    for (int i = 0; i < 2; ++i) {
        const qint64 id = AppSettings::primaryUser(i + 1);
        const auto p = id ? Database::instance().patient(id) : std::nullopt;
        m_actPrimary[i]->setText(p ? tr("&%1: %2").arg(i + 1).arg(p->firstName.isEmpty() ? p->lastName : p->firstName)
                                   : tr("Hauptbenutzer &%1 (nicht belegt)").arg(i + 1));
        m_actPrimary[i]->setToolTip(p ? tr("Hauptbenutzer %1: %2 (Strg+%1)").arg(i + 1).arg(p->displayName())
                                      : tr("Hauptbenutzer %1 über Benutzer-Menü festlegen").arg(i + 1));
        m_actPrimary[i]->setEnabled(p.has_value());
    }
}

void MainWindow::choosePrimary(int slot)
{
    const qint64 id = AppSettings::primaryUser(slot);
    if (id) selectPatient(id);
}

void MainWindow::assignPrimary(int slot)
{
    if (!m_patientId) return;
    AppSettings::setPrimaryUser(slot, m_patientId);
    // Doppelte Belegung vermeiden
    const int other = slot == 1 ? 2 : 1;
    if (AppSettings::primaryUser(other) == m_patientId)
        AppSettings::setPrimaryUser(other, 0);
    updatePrimaryActions();
    statusBar()->showMessage(tr("Hauptbenutzer %1 festgelegt.").arg(slot), 4000);
}

void MainWindow::newPatient()
{
    PatientDialog d(Patient{}, this);
    if (d.exec() == QDialog::Accepted) {
        const qint64 id = d.patient().id;
        reloadPatients();
        selectPatient(id);
        // Erster Benutzer wird automatisch Hauptbenutzer 1
        if (!AppSettings::primaryUser(1)) { AppSettings::setPrimaryUser(1, id); updatePrimaryActions(); }
    }
}

void MainWindow::editPatient()
{
    const auto p = currentPatient();
    if (!p) return;
    PatientDialog d(*p, this);
    if (d.exec() == QDialog::Accepted) {
        reloadPatients();
        reloadJournal();
        reloadChart();
    }
}

void MainWindow::deletePatient()
{
    if (!Session::isAdmin()) return;
    const auto p = currentPatient();
    if (!p) return;
    const auto n = Database::instance().vitals(p->id).size();
    const auto nd = Database::instance().documents(p->id).size();
    QMessageBox box(QMessageBox::Warning, tr("Benutzer löschen"),
                    tr("Soll „%1“ mit Stammblatt, Medikamentenplan, %2 Messung(en) und %3 "
                       "Dokument(en) der Krankenakte endgültig gelöscht werden?\n\nDies kann nicht "
                       "rückgängig gemacht werden. Legen Sie vorher ggf. eine Sicherung an.")
                        .arg(p->displayName()).arg(n).arg(nd),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
    QString err;
    if (!Database::instance().deletePatient(p->id, &err)) {
        QMessageBox::critical(this, tr("Benutzer löschen"), err);
        return;
    }
    for (int s = 1; s <= 2; ++s)
        if (AppSettings::primaryUser(s) == p->id) AppSettings::setPrimaryUser(s, 0);
    m_patientId = 0;
    reloadPatients();
}

// ================================================================ Messungen

qint64 MainWindow::selectedVitalId() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return 0;
    const VitalRecord *r = m_model->recordAt(rows.first().row());
    return r ? r->id : 0;
}

void MainWindow::newVital()
{
    const auto p = currentPatient();
    if (!p) return;
    VitalRecord r;
    r.patientId = p->id;
    VitalDialog d(*p, r, VitalDialog::Mode::Journal, this);
    d.exec();   // speichert selbst; Ansicht aktualisiert sich über das Signal
}

void MainWindow::startGlucoseProfile()
{
    const auto p = currentPatient();
    if (!p) return;
    GlucoseProfileDialog d(*p, this);
    if (d.exec() == QDialog::Accepted)
        statusBar()->showMessage(tr("BZ-Tagesprofil: %1 Erinnerung(en) gestellt.").arg(d.created()), 10000);
}

// Erinnerung eines BZ-Tagesprofils: Erfassungsfenster mit Betreff, Uhrzeit und Messzeitpunkt öffnen
void MainWindow::measureForAppointment(qint64 apptId)
{
    const auto a = Database::instance().appointment(apptId);
    if (!a || !a->patientId) return;
    const auto p = Database::instance().patient(a->patientId);
    if (!p) return;
    showNormal();
    raise();
    activateWindow();
    selectPatient(p->id);
    VitalRecord r;
    r.patientId = p->id;
    r.measuredAt = QDateTime::currentDateTime();
    r.context = GlucoseProfile::contextOf(*a);
    const QString subject = QStringLiteral("%1 %2").arg(GlucoseProfile::subject(), a->start.time().toString(QStringLiteral("HH:mm")));
    r.observations = subject;
    VitalDialog d(*p, r, VitalDialog::Mode::Journal, this);
    d.setWindowTitle(subject + QStringLiteral(" – ") + GlucoseProfile::labelOf(*a) + QStringLiteral(" – ") + p->displayName());
    if (QWidget *g = d.findChild<QWidget *>(QStringLiteral("glucoseSpin"))) g->setFocus();
    d.exec();
    if (d.savedCount() > 0) Database::instance().setAppointmentDone(apptId, true);
}

void MainWindow::editVital()
{
    const auto p = currentPatient();
    const qint64 id = selectedVitalId();
    if (!p || !id) return;
    const auto r = Database::instance().vital(id);
    if (!r) return;
    VitalDialog d(*p, *r, r->isAdmission ? VitalDialog::Mode::Admission : VitalDialog::Mode::Journal, this);
    if (d.exec() == QDialog::Accepted && r->isAdmission) {
        VitalRecord rec = d.record();
        QString err;
        Database::instance().saveVital(rec, &err);
    }
}

void MainWindow::deleteVital()
{
    const qint64 id = selectedVitalId();
    if (!id) return;
    const auto r = Database::instance().vital(id);
    if (!r) return;
    if (QMessageBox::question(this, tr("Messung löschen"),
            tr("Messung vom %1 löschen?").arg(QLocale().toString(r->measuredAt, QStringLiteral("dd.MM.yyyy HH:mm"))))
        != QMessageBox::Yes)
        return;
    QString err;
    if (!Database::instance().deleteVital(id, &err))
        QMessageBox::critical(this, tr("Messung löschen"), err);
}

// ================================================================= Journal

void MainWindow::onPeriodChanged()
{
    const bool custom = m_periodCombo->currentIndex() == Custom;
    m_fromEdit->setVisible(custom);
    m_toEdit->setVisible(custom);
    reloadJournal();
}

void MainWindow::journalRange(QDateTime &from, QDateTime &to) const
{
    const QDate today = QDate::currentDate();
    from = {};
    to = {};
    switch (m_periodCombo->currentIndex()) {
    case Days7:  from = QDateTime(today.addDays(-6), QTime(0, 0)); break;
    case Days30: from = QDateTime(today.addDays(-29), QTime(0, 0)); break;
    case Days90: from = QDateTime(today.addDays(-89), QTime(0, 0)); break;
    case Year:   from = QDateTime(today.addYears(-1).addDays(1), QTime(0, 0)); break;
    case Custom:
        from = QDateTime(m_fromEdit->date(), QTime(0, 0));
        to = QDateTime(m_toEdit->date().addDays(1), QTime(0, 0));
        break;
    default: break;
    }
}

QString MainWindow::journalPeriodText() const
{
    QDateTime from, to;
    journalRange(from, to);
    const QLocale loc;
    if (!from.isValid() && !to.isValid()) return tr("alle Messungen");
    const QString a = loc.toString(from.date(), QStringLiteral("dd.MM.yyyy"));
    const QString b = to.isValid() ? loc.toString(to.date().addDays(-1), QStringLiteral("dd.MM.yyyy"))
                                   : loc.toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy"));
    return QStringLiteral("%1 – %2").arg(a, b);
}

void MainWindow::reloadJournal()
{
    if (!m_model) return;
    if (!m_patientId) {
        m_model->setRecords({}, {});
        m_countLabel->clear();
        return;
    }
    QDateTime from, to;
    journalRange(from, to);
    const auto recs = Database::instance().vitals(m_patientId, from, to, m_admissionCheck->isChecked());
    if (const auto p = Database::instance().patient(m_patientId))
        m_model->setPatient(*p, Database::instance().latestHeight(m_patientId));
    m_model->setRecords(recs, Database::instance().vitalExtraKeys(m_patientId));
    m_table->resizeColumnsToContents();
    for (int c = 0; c < m_model->columnCount() - 1; ++c)
        m_table->setColumnWidth(c, std::min(m_table->columnWidth(c) + 8, 220));
    m_countLabel->setText(tr("%1 Messung(en)").arg(recs.size()));
    updateActions();
}

// ================================================================== Grafik

void MainWindow::onScaleChanged()
{
    const bool custom = m_scaleCombo->currentIndex() == 4;
    AppSettings::setChartScale(m_scaleCombo->currentIndex());
    m_customRange->setVisible(custom);
    m_navigation->setVisible(!custom);
    reloadChart();
}

void MainWindow::stepChart(int dir)
{
    QDate d = m_refDate->date();
    switch (m_scaleCombo->currentIndex()) {
    case 0: d = d.addDays(dir); break;
    case 1: d = d.addDays(7 * dir); break;
    case 2: d = d.addMonths(dir); break;
    case 3: d = d.addYears(dir); break;
    default: break;
    }
    m_refDate->setDate(d);
}

void MainWindow::showProtocol(Protocols::Kind kind)
{
    const auto p = currentPatient();
    if (!p) return;
    ProtocolDialog d(kind, *p, this);
    d.exec();
}

void MainWindow::reloadChart()
{
    if (!m_chart) return;
    ChartSpec spec;
    const int s = m_scaleCombo->currentIndex();
    spec.scale = s == 0 ? ChartSpec::Scale::Day : s == 1 ? ChartSpec::Scale::Week
               : s == 2 ? ChartSpec::Scale::Month : s == 3 ? ChartSpec::Scale::Year : ChartSpec::Scale::Custom;
    if (spec.scale == ChartSpec::Scale::Custom) {
        QDate a = m_chartFrom->date(), b = m_chartTo->date();
        if (b < a) std::swap(a, b);
        spec.from = QDateTime(a, QTime(0, 0));
        spec.to = QDateTime(b.addDays(1), QTime(0, 0));
    } else {
        ChartSpec::rangeFor(spec.scale, m_refDate->date(), spec.from, spec.to);
    }
    for (QCheckBox *c : m_panelChecks)
        if (c->isChecked()) spec.panelIds << c->property("panelId").toString();
    spec.hiddenKeys = AppSettings::chartHiddenKeys();
    spec.separatePages = m_separatePages && m_separatePages->isChecked();
    if (m_patientId)
        // 31 Tage Vorlauf: Zeitspannen ohne Stuhlgang, die vor dem Zeitraum beginnen (gezeichnet wird nur der Zeitraum)
        spec.records = Database::instance().vitals(m_patientId, spec.from.addDays(-31), spec.to, true);
    m_chart->setSpec(spec);
}

void MainWindow::toggleSeries(VitalKey key)
{
    QStringList hidden = AppSettings::chartHiddenKeys();
    const QString k = VitalDefs::key(key);
    if (hidden.contains(k)) hidden.removeAll(k);
    else hidden << k;
    AppSettings::setChartHiddenKeys(hidden);
    rebuildSeriesMenu();
    reloadChart();
}

void MainWindow::rebuildSeriesMenu()
{
    if (!m_seriesButton) return;
    QMenu *m = m_seriesButton->menu();
    m->clear();
    const QStringList hidden = AppSettings::chartHiddenKeys();
    for (const auto &pd : VitalDefs::panels()) {
        m->addSection(pd.title);
        for (VitalKey key : pd.keys) {
            QAction *a = m->addAction(VitalDefs::label(key).replace(QLatin1Char('&'), QStringLiteral("&&")));
            a->setCheckable(true);
            a->setChecked(!hidden.contains(VitalDefs::key(key)));
            a->setData(VitalDefs::key(key));
            connect(a, &QAction::triggered, this, [this, key] { toggleSeries(key); });
        }
    }
    m->addSeparator();
    m->addAction(tr("Alle Kurven einblenden"), this, [this] {
        AppSettings::setChartHiddenKeys({});
        rebuildSeriesMenu();
        reloadChart();
    })->setEnabled(!hidden.isEmpty());
    m_seriesButton->setText(hidden.isEmpty() ? tr("&Kurven") : tr("&Kurven (%1 ausgeblendet)").arg(hidden.size()));
}

// ================================================================= Ausgabe

void MainWindow::updateActions()
{
    const bool hasPatient = m_patientId != 0;
    const bool hasSel = selectedVitalId() != 0;
    if (m_actEditPatient) {
        const bool admin = Session::isAdmin();
        for (QAction *a : { m_actNewDb, m_actOpenDb, m_actBackup, m_actRestore, m_actExportDb, m_actForms, m_actWebSettings })
            if (a) a->setVisible(admin);
        m_actEditPatient->setEnabled(hasPatient);
        m_actDeletePatient->setEnabled(hasPatient && admin);
        m_actNewVital->setEnabled(hasPatient);
        if (m_actGlucoseProfile) m_actGlucoseProfile->setEnabled(hasPatient);
        m_actEditVital->setEnabled(hasPatient && hasSel && m_tabs->currentIndex() == TabJournal);
        m_actDeleteVital->setEnabled(hasPatient && hasSel && m_tabs->currentIndex() == TabJournal);
        for (QAction *a : { m_actPrint, m_actPreview, m_actPdf, m_actExcel, m_actSheet, m_actMedPlan, m_actPharmacy })
            a->setEnabled(hasPatient);
        for (QAction *a : m_actAssign) a->setEnabled(hasPatient);
        for (QAction *a : { m_actAddDoc, m_actPackAkte, m_actDocList }) a->setEnabled(hasPatient);
        m_actMedAssist->setEnabled(hasPatient);
        m_actMedStock->setEnabled(hasPatient);
        m_actMedLog->setEnabled(hasPatient);
        for (QAction *a : std::as_const(m_actUserPrint)) a->setEnabled(hasPatient);
        for (QAction *a : std::as_const(m_actMedSearch)) a->setEnabled(hasPatient);
        if (m_tabs->currentIndex() == TabAppts) {
            for (QAction *a : { m_actPrint, m_actPreview, m_actPdf, m_actExcel }) a->setEnabled(false);
            m_actPrint->setText(tr("&Drucken…"));
            m_actPdf->setText(tr("Als &PDF speichern…"));
            m_actExcel->setText(tr("Nach &Excel exportieren…"));
            return;
        }
        const bool chart = m_tabs->currentIndex() == TabChart;
        if (m_tabs->currentIndex() == TabDocs) {
            const bool docSel = !m_docs->selectedIds().isEmpty();
            m_actPrint->setText(tr("Ausgewählte Dokumente &drucken…"));
            m_actPrint->setEnabled(hasPatient && docSel);
            m_actPreview->setEnabled(hasPatient && docSel);
            m_actPdf->setEnabled(false);
            m_actExcel->setEnabled(false);
            m_actPdf->setText(tr("Als &PDF speichern…"));
            m_actExcel->setText(tr("Nach &Excel exportieren…"));
            return;
        }
        m_actPrint->setText(chart ? tr("Grafik &drucken…") : tr("Verlauf &drucken…"));
        m_actPdf->setText(chart ? tr("Grafik als &PDF speichern…") : tr("Verlauf als &PDF speichern…"));
        m_actExcel->setText(chart ? tr("Grafik nach &Excel exportieren…") : tr("Verlauf nach &Excel exportieren…"));
    }
}

void MainWindow::printCurrent()
{
    const auto p = currentPatient();
    if (!p) return;
    if (m_tabs->currentIndex() == TabDocs) { m_docs->printSelected(); return; }
    if (m_tabs->currentIndex() == TabChart)
        ReportPrinter::charts(this, *p, m_chart->spec(), ReportPrinter::Target::Printer);
    else
        ReportPrinter::journal(this, *p, *m_model, journalPeriodText(), ReportPrinter::Target::Printer);
}

void MainWindow::previewCurrent()
{
    const auto p = currentPatient();
    if (!p) return;
    if (m_tabs->currentIndex() == TabDocs) { m_docs->printSelected(); return; }
    if (m_tabs->currentIndex() == TabChart)
        ReportPrinter::charts(this, *p, m_chart->spec(), ReportPrinter::Target::Preview);
    else
        ReportPrinter::journal(this, *p, *m_model, journalPeriodText(), ReportPrinter::Target::Preview);
}

void MainWindow::pdfCurrent()
{
    const auto p = currentPatient();
    if (!p || m_tabs->currentIndex() == TabDocs) return;
    if (m_tabs->currentIndex() == TabChart)
        ReportPrinter::charts(this, *p, m_chart->spec(), ReportPrinter::Target::Pdf);
    else
        ReportPrinter::journal(this, *p, *m_model, journalPeriodText(), ReportPrinter::Target::Pdf);
}

void MainWindow::excelCurrent()
{
    const auto p = currentPatient();
    if (!p || m_tabs->currentIndex() == TabDocs) return;
    bool ok = m_tabs->currentIndex() == TabChart
                  ? ReportPrinter::chartsExcel(this, *p, m_chart->spec())
                  : ReportPrinter::journalExcel(this, *p, *m_model, journalPeriodText());
    if (ok) statusBar()->showMessage(tr("Excel-Datei gespeichert."), 4000);
}

void MainWindow::printPatientSheet()
{
    const auto p = currentPatient();
    if (!p) return;
    ReportPrinter::patientSheet(this, *p, Database::instance().medications(p->id, false),
                                Database::instance().admissionVitals(p->id), ReportPrinter::Target::Preview);
}

void MainWindow::printMedicationPlan()
{
    const auto p = currentPatient();
    if (!p) return;
    ReportPrinter::medicationPlan(this, *p, Database::instance().medications(p->id, false),
                                  ReportPrinter::Target::Preview);
}

void MainWindow::printPharmacyList()
{
    const auto p = currentPatient();
    if (!p) return;
    ReportPrinter::pharmacyList(this, *p, Database::instance().medications(p->id, false), ReportPrinter::Target::Preview);
}

void MainWindow::printReminders()
{
    const auto p = currentPatient();
    if (!p) return;
    RemindersPrintDialog d(*p, this);
    d.exec();
}

void MainWindow::showStartupAppointments(bool force)
{
    if (!force && !Upcoming::showOnStart()) return;
    const auto p = currentPatient();
    if (!p) return;
    const QList<Appointment> list = Upcoming::list(p->id, QDate::currentDate(), 2);
    if (list.isEmpty()) {
        if (force)
            QMessageBox::information(this, tr("Anstehende Termine"),
                                     tr("Für %1 stehen heute und morgen keine offenen Termine an.").arg(p->displayName()));
        return;
    }
    StartupAppointmentsDialog d(*p, list, this);
    connect(&d, &StartupAppointmentsDialog::openPlannerRequested, this, [this] {
        m_tabs->setCurrentWidget(m_appts);
    });
    d.exec();
}

void MainWindow::medicationAssistant()
{
    const auto p = currentPatient();
    if (!p) return;
    const QList<Medication> meds = Database::instance().medications(p->id, false);
    if (MedicationAssistant::currentMedications(meds, QDate::currentDate()).isEmpty()) {
        QMessageBox::information(this, tr("Medikamente stellen"),
                                 tr("Für %1 sind keine aktuellen Medikamente eingetragen.\n"
                                    "Bitte zuerst den Medikamentenplan im Stammblatt pflegen.").arg(p->displayName()));
        return;
    }
    MedicationAssistant d(*p, meds, this);
    d.exec();
    remindStock(d.stockShortage());   // Bestand nach dem Stellen prüfen
}

void MainWindow::medicationStock()
{
    const auto p = currentPatient();
    if (!p) return;
    StockDialog d(*p, this);
    d.exec();
    updateHeader();
}

void MainWindow::remindStock(const QStringList &shortage)
{
    const auto p = currentPatient();
    if (!p) return;
    StockDialog::remind(this, *p, shortage);
    updateHeader();
}

// ========================================================== Datei / Extras

bool MainWindow::reopenDatabase(const QString &path, bool createNew)
{
    const QString previous = Database::instance().path();
    const QString previousUser = Session::user();
    Database::instance().close();
    Session::clear();
    if (Session::openInteractive(this, path, createNew)) {
        AppSettings::setDatabasePath(path);
        afterLogin();
        return true;
    }
    // Abgebrochen/fehlgeschlagen: zurück zur bisherigen Datenbank (erneute Anmeldung)
    if (!previous.isEmpty() && previous != path && Session::openInteractive(this, previous)) {
        afterLogin();
        return false;
    }
    if (!Session::isLoggedIn()) {
        QMessageBox::information(this, tr("Datenbank"), tr("Ohne geöffnete Datenbank wird VitaParms beendet."));
        qApp->quit();
    }
    Q_UNUSED(previousUser)
    return false;
}

void MainWindow::afterLogin()
{
    m_patientId = 0;
    reloadPatients();
    qint64 last = AppSettings::lastPatient();
    if (!last) last = AppSettings::primaryUser(1);
    if (last) selectPatient(last);
    statusBar()->showMessage(tr("Datenbank: %1").arg(QDir::toNativeSeparators(Database::instance().path())));
    updateSessionInfo();
    updateActions();
    if (m_appts) m_appts->reload();
    if (m_reminders) m_reminders->setEnabled(true);
}

void MainWindow::updateSessionInfo()
{
    if (!m_sessionLabel) return;
    m_sessionLabel->setText(Session::isLoggedIn()
        ? tr("🔒 %1 (%2)").arg(Session::user(), roleName(Session::role()))
        : QString());
    m_sessionLabel->setToolTip(tr("Angemeldet als %1 – Datenbank verschlüsselt").arg(Session::user()));
}

void MainWindow::lockForInactivity()
{
    // Offene (modale) Dialoge zuerst schließen – ungespeicherte Eingaben darin verfallen.
    if (QWidget *m = QApplication::activeModalWidget()) {
        if (auto *d = qobject_cast<QDialog *>(m)) d->reject();
        else m->close();
        QTimer::singleShot(100, this, &MainWindow::lockForInactivity);
        return;
    }
    emit aboutToLock();
    logout();
}

void MainWindow::logout()
{
    if (m_idle) m_idle->setActive(false);
    emit aboutToLock();
    AppSettings::setWindowState(QStringLiteral("main/geometry"), saveGeometry());
    hide();
    const QString path = Database::instance().path();
    Database::instance().close();
    Session::clear();
    DocumentsPage::cleanupTempFiles();
    if (!Session::openInteractive(nullptr, path)) {
        qApp->quit();
        return;
    }
    afterLogin();
    show();
    if (m_idle) m_idle->setActive(true);
}

void MainWindow::manageAccounts()
{
    AccountsDialog d(this);
    d.exec();
    updateSessionInfo();
}

void MainWindow::newDatabase()
{
    if (!Session::isAdmin()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Neue Datenbank anlegen"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("vitaparms.sqlite")),
        tr("VitaParms-Datenbank (*.sqlite *.db)"));
    if (path.isEmpty()) return;
    if (Database::fileHasContent(path)) {
        QMessageBox::information(this, tr("Neue Datenbank"), tr("Die Datei existiert bereits und wird geöffnet."));
        reopenDatabase(path, false);
        return;
    }
    reopenDatabase(path, true);
}

void MainWindow::openDatabase()
{
    if (!Session::isAdmin()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Datenbank öffnen"),
        QFileInfo(Database::instance().path()).absolutePath(), tr("VitaParms-Datenbank (*.sqlite *.db);;Alle Dateien (*)"));
    if (!path.isEmpty())
        reopenDatabase(path, false);
}

void MainWindow::backupDatabase()
{
    if (!Session::isAdmin()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Sicherung erstellen"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("VitaParms-Sicherung_%1.sqlite").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm")))),
        tr("VitaParms-Datenbank (*.sqlite)"));
    if (path.isEmpty()) return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = Database::instance().backupTo(path, &err);
    // Schlüsseldatei (Anmeldekonten) gehört zur Sicherung
    const QString keys = KeyStore::pathFor(path);
    if (ok) {
        QFile::remove(keys);
        ok = QFile::copy(KeyStore::pathFor(Database::instance().path()), keys);
        if (!ok) err = tr("Die Schlüsseldatei konnte nicht kopiert werden.");
    }
    QApplication::restoreOverrideCursor();
    if (ok)
        QMessageBox::information(this, tr("Sicherung"),
            tr("Sicherung gespeichert (verschlüsselt):\n%1\n%2\n\nBeide Dateien gehören zusammen. Ohne die "
               ".keys-Datei lässt sich die Sicherung nur mit dem Wiederherstellungsschlüssel öffnen.")
                .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(keys)));
    else
        QMessageBox::critical(this, tr("Sicherung"), tr("Sicherung fehlgeschlagen:\n%1").arg(err));
}

void MainWindow::restoreDatabase()
{
    if (!Session::isAdmin()) return;
    const QString src = QFileDialog::getOpenFileName(this, tr("Sicherung wiederherstellen / Datenbank importieren"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("VitaParms-Datenbank (*.vpdb *.sqlite *.db);;Alle Dateien (*)"));
    if (src.isEmpty()) return;
    const QString target = Database::instance().path();
    const bool hasKeys = DbTransfer::isBundle(src) || QFile::exists(KeyStore::pathFor(src));
    if (QMessageBox::warning(this, tr("Sicherung wiederherstellen"),
            tr("Die aktuelle Datenbank\n%1\nwird durch die gewählte ersetzt.\n\n"
               "Der bisherige Stand wird vorher als Kopie (*.vor-import-…) abgelegt.%2\n\n"
               "Anschließend ist eine Anmeldung mit den Konten der gewählten Datenbank nötig.\nFortfahren?")
                .arg(QDir::toNativeSeparators(target),
                     hasKeys ? QString() : tr("\n\nZur Sicherung gibt es keine .keys-Datei – Sie benötigen "
                                             "den Wiederherstellungsschlüssel.")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    replaceDatabaseFrom(src);
}

void MainWindow::replaceDatabaseFrom(const QString &src)
{
    const QString target = Database::instance().path();
    QString safety, err;
    bool keys = false;
    if (!DbTransfer::importDatabase(src, {}, target, &safety, &keys, &err)) {
        QMessageBox::critical(this, tr("Sicherung wiederherstellen"), err);
        if (Database::instance().isOpen()) return;   // nichts verändert, weiter angemeldet
        Session::clear();
        hide();
        if (!Session::openInteractive(nullptr, target)) { qApp->quit(); return; }
        afterLogin();
        show();
        return;
    }
    Session::clear();
    hide();
    if (!Session::openInteractive(nullptr, target)) {
        qApp->quit();
        return;
    }
    afterLogin();
    show();
}

void MainWindow::showUsbSync(bool autoMerge)
{
    UsbSyncDialog d(this);
    d.setAutoMerge(autoMerge);
    d.exec();
    const QString bundle = d.bundleToImport();
    if (bundle.isEmpty()) return;
    replaceDatabaseFrom(bundle);
    QFile::remove(bundle);
}

void MainWindow::exportDatabase()
{
    if (!Session::isAdmin()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Datenbank exportieren"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("VitaParms_%1.vpdb").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm")))),
        tr("VitaParms-Übertragungsdatei (*.vpdb)"));
    if (path.isEmpty()) return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = DbTransfer::exportBundle(path, &err);
    QApplication::restoreOverrideCursor();
    if (ok)
        QMessageBox::information(this, tr("Datenbank exportieren"),
            tr("Gespeichert:\n%1\n\nDie Datei enthält die verschlüsselte Datenbank und die Anmeldekonten. Auf dem "
               "anderen Gerät (Computer oder Tablet) bei der Anmeldung „Datenbank importieren…“ wählen. "
               "Behandeln Sie die Datei wie eine Sicherung – ohne Kontopasswort lässt sie sich nicht öffnen.")
                .arg(QDir::toNativeSeparators(path)));
    else
        QMessageBox::critical(this, tr("Datenbank exportieren"), err);
}

void MainWindow::showSettings()
{
    SettingsDialog d(this);
    if (d.exec() == QDialog::Accepted) {
        if (d.restartRequested()) {
            close();
            I18n::restartApplication();
            return;
        }
        m_chart->update();
        m_idle->setMinutes(AppSettings::autoLockMinutes());
        updatePrimaryActions();
        reloadJournal();
        reloadChart();
    }
}

void MainWindow::customizeForms()
{
    if (!Session::isAdmin()) return;
    FormCustomizeDialog d(this);
    d.exec();
}

void MainWindow::showDisclaimer()
{
    QMessageBox::information(this, tr("Haftungshinweis"),
        tr("<p><b>VitaParms ist ein persönliches Tagebuch für gesundheitsbezogene Messwerte – "
           "kein Medizinprodukt.</b></p>"
           "<p>Das Programm stellt keine Diagnosen, gibt keine Therapieempfehlungen und ersetzt "
           "keine ärztliche Beratung. Berechnete Werte (z.&nbsp;B. BMI) und Grafiken dienen nur "
           "der Übersicht. Die Verwendung erfolgt auf eigene Gefahr.</p>"
           "<p>Bei Beschwerden oder auffälligen Werten wenden Sie sich bitte an Ihre Ärztin "
           "oder Ihren Arzt, im Notfall an den Notruf 112.</p>"));
}

QString MainWindow::appTitle()
{
    return DemoData::isActive() ? tr("VitaParms – Demo") : QStringLiteral("VitaParms");
}

void MainWindow::startDemo()
{
    QMessageBox box(QMessageBox::Information, tr("Demo-Datenbank"),
        tr("<p>Die Demo öffnet ein <b>zweites Programmfenster</b> mit der erfundenen Person "
           "<i>Erika Mustermann</i>: %1 Tage Messwerte bis heute (mit Tagesprofilen und einem Infekt), "
           "Medikamentenplan, Termine und Befunde in der Krankenakte.</p>"
           "<p>Die Demo hat eine eigene, verschlüsselte Datenbank und eigene Einstellungen; Sprache und "
           "Erscheinungsbild werden übernommen. Die Anmeldung erfolgt automatisch "
           "(Konto <tt>%2</tt>, Passwort <tt>%3</tt>). Bei jedem Start wird die Demo neu erzeugt – "
           "Sie können also nichts kaputt machen.</p>"
           "<p>Ihre eigenen Daten bleiben unberührt.</p>")
            .arg(DemoData::kDays).arg(DemoData::accountName(), DemoData::password()),
        QMessageBox::NoButton, this);
    auto *go = box.addButton(tr("Demo &starten"), QMessageBox::AcceptRole);
    box.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(go);
    box.exec();
    if (box.clickedButton() != go) return;
    if (!DemoData::launch())
        QMessageBox::warning(this, tr("Demo-Datenbank"), tr("Die Demo konnte nicht gestartet werden."));
    else
        statusBar()->showMessage(tr("Die Demo wird in einem eigenen Fenster geöffnet …"), 6000);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("Über VitaParms"),
        tr("<h3>VitaParms %1</h3>"
           "<p>Tagebuch für Vitalwerte mit Verlauf, Grafiken, Druck, PDF- und Excel-Export.</p>"
           "<p>Copyright © 2026 Michael Bergmann<br>"
           "Freie Software unter der <b>MIT-Lizenz</b>.</p>"
           "<p>Erstellt mit Qt %2 (LGPLv3).</p>"
           "<p><small>Kein Medizinprodukt – Verwendung auf eigene Gefahr.</small></p>")
            .arg(QStringLiteral(VITAPARMS_VERSION), QString::fromLatin1(qVersion())));
}

void MainWindow::showWebsiteSettings()
{
    if (!Session::isAdmin()) return;
    WebsiteDialog d(this);
    d.exec();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // Abgleich mit dem Tablet über das USB-Kabel anbieten (nach der ersten USB-Verbindung)
    if (!DemoData::isActive() && Session::isLoggedIn() && UsbSyncDialog::askOnExit()) {
        QMessageBox mb(QMessageBox::Question, tr("Mit dem Tablet abgleichen?"),
                       tr("Vor dem Beenden die Datenbank über das USB-Kabel mit dem Tablet abgleichen?\n\n"
                          "Das Tablet muss angeschlossen sein, VitaParms darauf geöffnet und angemeldet."),
                       QMessageBox::NoButton, this);
        QPushButton *sync = mb.addButton(tr("&Abgleichen…"), QMessageBox::AcceptRole);
        QPushButton *quit = mb.addButton(tr("&Ohne Abgleich beenden"), QMessageBox::DestructiveRole);
        QPushButton *cancel = mb.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
        mb.setDefaultButton(sync);
        mb.setEscapeButton(cancel);
        mb.exec();
        if (mb.clickedButton() == cancel) { e->ignore(); return; }
        if (mb.clickedButton() == sync) showUsbSync(true);
        Q_UNUSED(quit)
    }
    // Datenbank beim Beenden abgleichen, wenn sich etwas geändert hat
    {
        const DbSync::Config sc = DbSync::config();
        DbSync &ds = DbSync::instance();
        if (!DemoData::isActive() && sc.autoSync && DbSync::isReady(sc) && ds.status().pending) {
            QProgressDialog progress(tr("Datenbank wird abgeglichen …"), QString(), 0, 0, this);
            progress.setWindowTitle(tr("VitaParms"));
            progress.setWindowModality(Qt::ApplicationModal);
            progress.setMinimumDuration(0);
            progress.show();
            QString msg;
            if (!ds.syncBlocking(60 * 1000, &msg)) {
                progress.close();
                if (QMessageBox::warning(this, tr("Abgleich"),
                        tr("Die Datenbank konnte nicht abgeglichen werden:\n%1\n\nTrotzdem beenden?").arg(msg),
                        QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
                    e->ignore();
                    return;
                }
            }
        }
    }
    // Website beim Beenden aktualisieren, wenn seit dem letzten Hochladen etwas geändert wurde
    WebSync &ws = WebSync::instance();
    const WebSync::Config wc = WebSync::config();
    if (!DemoData::isActive() && wc.autoPublish && WebSync::isReady(wc) && (ws.hasPendingChanges() || ws.isBusy())) {
        QProgressDialog progress(tr("Website wird aktualisiert …"), QString(), 0, 0, this);
        progress.setWindowTitle(tr("VitaParms"));
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.show();
        QString msg;
        if (!ws.publishBlocking(45 * 1000, &msg)) {
            progress.close();
            if (QMessageBox::warning(this, tr("Website"),
                    tr("Die Website konnte nicht aktualisiert werden:\n%1\n\nTrotzdem beenden?").arg(msg),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
                e->ignore();
                return;
            }
        }
    }
    AppSettings::setWindowState(QStringLiteral("main/geometry"), saveGeometry());
    AppSettings::setWindowState(QStringLiteral("main/state"), saveState());
    DocumentsPage::cleanupTempFiles();
    e->accept();
}
