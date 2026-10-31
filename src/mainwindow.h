// VitaParms – Hauptfenster
// SPDX-License-Identifier: MIT
#pragma once

#include "vitaldefs.h"
#include "datatypes.h"
#include "protocols.h"
#include <QMainWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QDateEdit;
class QLabel;
class QTabWidget;
class QTableView;
class QToolButton;
class ChartWidget;
class JournalModel;
class DocumentsPage;
class InactivityWatcher;
class AppointmentsPage;
class ReminderService;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Für Tests/Automatisierung
    void selectPatient(qint64 id);
    // Termine heute/morgen der gewählten Person anzeigen (beim Programmstart); force = auch ohne Einstellung
    void showStartupAppointments(bool force = false);
    // Bestandsführung: offene Bestellliste anmahnen (beim Start und nach dem Stellen)
    void remindStock(const QStringList &shortage = {});
    void printReminders();
    JournalModel *journalModel() const { return m_model; }
    ChartWidget *chartWidget() const { return m_chart; }
    QTabWidget *tabs() const { return m_tabs; }
    DocumentsPage *documentsPage() const { return m_docs; }
    AppointmentsPage *appointmentsPage() const { return m_appts; }
    ReminderService *reminders() const { return m_reminders; }
    std::optional<Patient> currentPatient() const;

signals:
    void aboutToLock();

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    // Benutzer
    void newPatient();
    void editPatient();
    void deletePatient();
    void choosePrimary(int slot);
    void assignPrimary(int slot);
    void onUserComboChanged(int index);
    void reloadPatients();

    // Messungen
    void newVital();
    void editVital();
    void deleteVital();

    // Ansichten
    void reloadJournal();
    void reloadChart();
    void showProtocol(Protocols::Kind kind);   // Stuhlprotokoll, BZ-Tagesprofile
    void onPeriodChanged();
    void onScaleChanged();
    void stepChart(int dir);
    void updateActions();

    // Ausgabe
    void printCurrent();
    void previewCurrent();
    void pdfCurrent();
    void excelCurrent();
    void printPatientSheet();
    void printMedicationPlan();
    void printPharmacyList();
    void medicationAssistant();
    void medicationStock();
    void showWounds(qint64 woundId = 0);
    void importLightDatabase();
    void exportDatabase();

    // Datei / Extras
    void newDatabase();
    void openDatabase();
    void backupDatabase();
    void restoreDatabase();
    void replaceDatabaseFrom(const QString &src);   // ersetzen und neu anmelden
    void showUsbSync(bool autoMerge = false);
    void startGlucoseProfile();
    void startBpProfile();
    void measureForAppointment(qint64 apptId);
    void showSettings();
    void customizeForms();
    void manageAccounts();
    void logout();
    void lockForInactivity();
    void about();
    void showDisclaimer();

private:
    void buildUi();
    void buildMenus();
    void updateHeader();
    void updatePrimaryActions();
    QString journalPeriodText() const;
    void journalRange(QDateTime &from, QDateTime &to) const;
    qint64 selectedVitalId() const;
    bool reopenDatabase(const QString &path, bool createNew = false);
    void afterLogin();
    void updateSessionInfo();

    qint64 m_patientId = 0;

    QComboBox   *m_userCombo = nullptr;
    QLabel      *m_header = nullptr;
    QTabWidget  *m_tabs = nullptr;

    // Journal
    QComboBox   *m_periodCombo = nullptr;
    QDateEdit   *m_fromEdit = nullptr;
    QDateEdit   *m_toEdit = nullptr;
    QCheckBox   *m_admissionCheck = nullptr;
    QTableView  *m_table = nullptr;
    JournalModel *m_model = nullptr;
    QLabel      *m_countLabel = nullptr;
    QHBoxLayout *m_journalButtons = nullptr;

    // Grafik
    QComboBox   *m_scaleCombo = nullptr;
    QDateEdit   *m_refDate = nullptr;
    QDateEdit   *m_chartFrom = nullptr;
    QDateEdit   *m_chartTo = nullptr;
    QWidget     *m_customRange = nullptr;
    QWidget     *m_navigation = nullptr;
    QList<QCheckBox *> m_panelChecks;
    QToolButton *m_seriesButton = nullptr;     // Kurven einzeln ein-/ausblenden
    QCheckBox   *m_separatePages = nullptr;    // Druck: je Bereich eine Seite
    void toggleSeries(VitalKey key);
    void rebuildSeriesMenu();
    ChartWidget *m_chart = nullptr;

    // Krankenakte
    DocumentsPage *m_docs = nullptr;
    AppointmentsPage *m_appts = nullptr;
    ReminderService *m_reminders = nullptr;

    // Aktionen
    QAction *m_actEditPatient = nullptr, *m_actDeletePatient = nullptr;
    QAction *m_actNewVital = nullptr, *m_actEditVital = nullptr, *m_actDeleteVital = nullptr;
    QAction *m_actGlucoseProfile = nullptr;
    QAction *m_actBpProfile = nullptr;
    QAction *m_actImportLight = nullptr;
    QAction *m_actPrimary[2] = { nullptr, nullptr };
    QAction *m_actAssign[2] = { nullptr, nullptr };
    QAction *m_actPrint = nullptr, *m_actPreview = nullptr, *m_actPdf = nullptr, *m_actExcel = nullptr;
    QAction *m_actSheet = nullptr, *m_actMedPlan = nullptr, *m_actPharmacy = nullptr;
    QAction *m_actManual = nullptr;
    QAction *m_actDemo = nullptr;
    QAction *m_actWebPublish = nullptr, *m_actWebSettings = nullptr, *m_actDbSync = nullptr;
    void showWebsiteSettings();
    void startDemo();
    static QString appTitle();
    QAction *m_actNewDb = nullptr, *m_actOpenDb = nullptr, *m_actBackup = nullptr,
            *m_actRestore = nullptr, *m_actForms = nullptr;
    QLabel  *m_sessionLabel = nullptr;
    InactivityWatcher *m_idle = nullptr;
    QList<QAction *> m_actMedSearch;
    QAction *m_actMedStock = nullptr;
    QAction *m_actWounds = nullptr;
    QAction *m_actExportDb = nullptr;
    QAction *m_actMedAssist = nullptr, *m_actMedLog = nullptr, *m_actNewAppt = nullptr;
    QList<QAction *> m_actUserPrint;   // Benutzer → Drucken → …
    QAction *m_actAddDoc = nullptr, *m_actPackAkte = nullptr, *m_actDocList = nullptr;
};
