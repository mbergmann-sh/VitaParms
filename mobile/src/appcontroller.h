// VitaParms Tablet – Brücke zwischen dem gemeinsamen Kern und der QML-Oberfläche
//
// Alle Daten gehen als QVariantMap/QVariantList an QML; Schreibzugriffe laufen über
// dieselben Kernfunktionen wie im Desktop-Programm (Database, Session, MedSchedule …),
// damit beide Versionen dieselbe Datenbank verwenden und abgleichen können.
// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QVariant>

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool hasDatabase READ hasDatabase NOTIFY sessionChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY sessionChanged)
    Q_PROPERTY(bool isAdmin READ isAdmin NOTIFY sessionChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY sessionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool syncBusy READ syncBusy NOTIFY syncBusyChanged)
    Q_PROPERTY(QString syncMessage READ syncMessage NOTIFY syncStatusChanged)
    Q_PROPERTY(bool syncPending READ syncPending NOTIFY syncStatusChanged)
    Q_PROPERTY(bool syncEnabled READ syncEnabled NOTIFY syncStatusChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool languageChosen READ languageChosen NOTIFY languageChanged)
    Q_PROPERTY(int dataRevision READ dataRevision NOTIFY dataChanged)
    Q_PROPERTY(bool disclaimerAccepted READ disclaimerAccepted NOTIFY disclaimerChanged)
    Q_PROPERTY(bool usbActive READ usbActive NOTIFY usbChanged)
    Q_PROPERTY(bool usbConnected READ usbConnected NOTIFY usbChanged)
    Q_PROPERTY(QString usbCode READ usbCode NOTIFY usbChanged)
    Q_PROPERTY(QString usbStatus READ usbStatus NOTIFY usbChanged)
    Q_PROPERTY(bool usbAskOnExit READ usbAskOnExit WRITE setUsbAskOnExit NOTIFY usbChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    QString version() const;
    bool hasDatabase() const;
    bool loggedIn() const;
    bool isAdmin() const;
    QString userName() const;
    QString lastError() const { return m_lastError; }
    bool syncBusy() const;
    QString syncMessage() const { return m_syncMessage; }
    bool syncPending() const;
    bool syncEnabled() const;
    QString theme() const;
    void setTheme(const QString &t);
    bool dark() const;
    QString language() const;
    bool languageChosen() const;
    // Sprache wechseln ("de"/"en") – lädt die Übersetzungen neu und übersetzt die Oberfläche sofort
    Q_INVOKABLE void setLanguage(const QString &lang);
    void setEngine(class QQmlEngine *engine) { m_engine = engine; }
    // Kurzanleitung „Erste Schritte“ (PDF) in der Programmsprache öffnen – auch ohne Anmeldung
    Q_INVOKABLE bool openGettingStarted();
    int dataRevision() const { return m_revision; }
    bool disclaimerAccepted() const;
    Q_INVOKABLE void acceptDisclaimer();

    // ---- Datenbank und Anmeldung
    Q_INVOKABLE QStringList accounts() const;
    Q_INVOKABLE bool login(const QString &name, const QString &password);
    Q_INVOKABLE void lock();
    Q_INVOKABLE QString createDatabase(const QString &admin, const QString &password);   // liefert Wiederherstellungsschlüssel
    Q_INVOKABLE QString passwordProblem(const QString &pw) const;
    Q_INVOKABLE bool setupFromServer(const QString &url, const QString &uploadKey);
    Q_INVOKABLE QVariantMap readCouplingFile(const QString &fileUrl);   // {url, uploadKey}
    Q_INVOKABLE QString serverInfo(const QString &url, const QString &uploadKey);
    // Datenbank aus einer Datei übernehmen (*.vpdb oder *.sqlite + .keys), ohne Website
    Q_INVOKABLE bool isDatabaseBundle(const QString &fileUrl) const;
    Q_INVOKABLE bool importDatabase(const QString &fileUrl, const QString &keysUrl);
    Q_INVOKABLE QString fileName(const QString &fileUrl) const;
    Q_INVOKABLE void userActivity();                 // für die automatische Sperre

    // ---- Personen
    Q_INVOKABLE QVariantList persons();
    Q_INVOKABLE QVariantMap personSummary(qint64 id);
    Q_INVOKABLE QVariantMap personDetail(qint64 id);   // 0 = neue Person
    Q_INVOKABLE qint64 savePerson(const QVariantMap &m);
    Q_INVOKABLE bool deletePerson(qint64 id);
    Q_INVOKABLE QVariantList careMarks() const;
    Q_INVOKABLE QVariantList contactKinds() const;
    Q_INVOKABLE QVariantList armOptions() const;
    Q_INVOKABLE QVariantList legOptions() const;
    Q_INVOKABLE qint64 lastPerson() const;
    Q_INVOKABLE void setLastPerson(qint64 id);

    // ---- Messwerte
    Q_INVOKABLE QVariantList vitalFields() const;    // Eingabefelder (inkl. Zusatzfelder Atemfrequenz, Schmerz)
    Q_INVOKABLE QStringList contexts() const;
    Q_INVOKABLE QVariantList journal(qint64 pid, const QString &query = {}, int days = 0);
    Q_INVOKABLE QVariantMap vital(qint64 id);
    Q_INVOKABLE QVariantMap newVital(qint64 pid);
    Q_INVOKABLE QStringList checkVital(const QVariantMap &m) const;   // Hinweise vor dem Speichern
    Q_INVOKABLE qint64 saveVital(qint64 pid, const QVariantMap &m);
    Q_INVOKABLE bool deleteVital(qint64 id);
    Q_INVOKABLE QString bmiPreview(qint64 pid, const QVariant &weight, const QVariant &height);
    Q_INVOKABLE QVariantList latest(qint64 pid);      // Kacheln der Übersicht
    Q_INVOKABLE QVariantMap stoolStatus(qint64 pid);  // Hinweis nach drei Tagen ohne Stuhlgang
    Q_INVOKABLE QVariantMap stoolChoices() const;     // Bristol-Typen, Menge, Farbe, Beimengungen
    Q_INVOKABLE bool stoolNeedsAttention(const QVariantMap &detail) const;   // schwarz/blutig

    // ---- Medikamente und Termine
    Q_INVOKABLE QVariantList medications(qint64 pid, bool includeArchived);
    Q_INVOKABLE QVariantMap medication(qint64 pid, qint64 id);
    Q_INVOKABLE QStringList medCategories() const;
    Q_INVOKABLE QString peakFlowInfo(const QVariantMap &person);                // Stammblatt: Sollwert/Bezug
    Q_INVOKABLE QVariantMap peakFlowCheck(qint64 pid, int value, const QString &heightText, const QString &isoDateTime);
    Q_INVOKABLE QString medCategoryHint(int category) const;
    Q_INVOKABLE int pznStatus(const QString &text) const;          // 0 = leer, 1 = gültig, -1 = ungültig
    Q_INVOKABLE QVariantMap medicationPreview(const QVariantMap &v);
    Q_INVOKABLE bool saveMedication(qint64 pid, const QVariantMap &m);
    Q_INVOKABLE bool deleteMedication(qint64 pid, qint64 id);
    Q_INVOKABLE QVariantList upcoming(qint64 pid, int days = 14);

    // ---- Terminplaner (pid 0 = alle Personen; range: upcoming, today, week, month, past)
    Q_INVOKABLE QVariantList appointmentList(qint64 pid, const QString &range, bool showDone);
    Q_INVOKABLE QVariantMap appointment(qint64 id, qint64 pid);       // id 0 = neuer Termin
    Q_INVOKABLE QVariantList apptPersons();
    Q_INVOKABLE QStringList apptOwners() const;
    Q_INVOKABLE QVariantList apptLeadTimes() const;
    Q_INVOKABLE QVariantList apptPriorities() const;
    Q_INVOKABLE QVariantList apptRepeatChoices() const;
    Q_INVOKABLE QVariantList apptMonthModes(const QString &isoStart) const;
    Q_INVOKABLE QStringList weekdayNames() const;
    Q_INVOKABLE QString apptRulePreview(const QVariantMap &rule, const QString &isoStart) const;
    Q_INVOKABLE bool apptRuleChanged(const QVariantMap &m);
    // scope bei Serien: "only", "following", "all"
    Q_INVOKABLE bool saveAppointment(const QVariantMap &m, const QString &scope = {});
    Q_INVOKABLE bool deleteAppointment(qint64 id, const QString &scope = {});
    Q_INVOKABLE bool setAppointmentDone(qint64 id, bool done);
    Q_INVOKABLE int plannedReminders() const;

    // ---- BZ-Tagesprofil und Quartalswechsel (gleiche Logik wie am Computer)
    Q_INVOKABLE QVariantList glucoseProfilePoints() const;
    Q_INVOKABLE int startGlucoseProfile(qint64 pid, const QString &isoDay, const QVariantList &points, bool sound);
    Q_INVOKABLE int stopGlucoseProfile(qint64 pid, const QString &isoDay);
    Q_INVOKABLE int glucoseProfileOpen(qint64 pid, const QString &isoDay);
    Q_INVOKABLE QVariantMap dueProfileMeasurement();   // offener Profil-Termin (jetzt fällig) oder leer
    Q_INVOKABLE QVariantMap quarterDue();               // Erinnerung jetzt zeigen? (title, message) oder leer
    Q_INVOKABLE QVariantMap quarterBanner();            // Hinweis im Terminplaner (bis bestätigt)
    Q_INVOKABLE void confirmQuarter();
    Q_INVOKABLE void snoozeQuarter();
    Q_INVOKABLE bool quarterReminderEnabled() const;
    Q_INVOKABLE void setQuarterReminderEnabled(bool on);       // gestellte Erinnerungen (Anzeige, Prüfung)
    Q_INVOKABLE bool notificationsAllowed() const;
    Q_INVOKABLE void requestNotifications();

    // ---- Ausgaben (PDF/Excel → Anzeige-App, dort drucken oder teilen)
    Q_INVOKABLE QString exportReport(const QString &kind, qint64 pid, const QVariantMap &options = {});
    Q_INVOKABLE bool openFile(const QString &path);

    // ---- Krankenakte (appcontroller_docs.cpp)
    Q_INVOKABLE QVariantList documents(qint64 pid, const QString &query = {}, const QString &category = {});
    Q_INVOKABLE QStringList documentCategories(qint64 pid);
    Q_INVOKABLE QStringList documentIssuers(qint64 pid);
    Q_INVOKABLE QVariantMap documentInfo(qint64 id);
    Q_INVOKABLE bool pdfPreviewAvailable() const;
    Q_INVOKABLE QString startCapture();                       // Kamera-App öffnen → Pfad des Fotos
    Q_INVOKABLE QString captureState(const QString &path);    // "pending", "ok", "cancel", "error"
    Q_INVOKABLE bool draftAddImage(const QString &pathOrUrl);
    Q_INVOKABLE QVariantMap draftAddFiles(const QVariantList &urls);
    Q_INVOKABLE void draftRemovePage(int i);
    Q_INVOKABLE void draftRotatePage(int i);
    Q_INVOKABLE void draftClear();
    Q_INVOKABLE QVariantMap draftInfo(qint64 pid);
    Q_INVOKABLE qint64 saveDraft(qint64 pid, const QVariantMap &meta);
    Q_INVOKABLE bool saveDocument(const QVariantMap &meta);
    Q_INVOKABLE bool deleteDocument(qint64 id);
    Q_INVOKABLE bool openDocument(qint64 id);
    Q_INVOKABLE QVariantMap shareDocuments(qint64 pid, const QVariantList &ids, const QVariantMap &opts);
    QImage documentPageImage(const QString &id, int page, int width);   // für image://docpage/<id|draft>/<Seite>

    // ---- Abgleich
    Q_INVOKABLE QVariantMap syncConfig();
    Q_INVOKABLE bool saveSyncConfig(const QVariantMap &m);
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void syncIfDue();                     // beim Start und beim Zurückkehren in die App
    Q_INVOKABLE void playSound(const QString &kind);  // "countStart", "countStop"

    // ---- Abgleich über das USB-Kabel (der Computer verbindet sich per adb)
    Q_INVOKABLE bool startUsbSync();
    Q_INVOKABLE void stopUsbSync();
    bool usbActive() const;
    bool usbConnected() const { return m_usbConnected; }
    QString usbCode() const;
    QString usbStatus() const { return m_usbStatus; }
    bool usbAskOnExit() const;
    void setUsbAskOnExit(bool on);
    bool ensureUsbServer(QString *error);

    // ---- Einstellungen
    Q_INVOKABLE QString glucoseUnit() const;          // "mg/dl" oder "mmol/l"
    Q_INVOKABLE void setGlucoseUnit(const QString &u);
    Q_INVOKABLE int autoLockMinutes() const;
    Q_INVOKABLE void setAutoLockMinutes(int m);
    Q_INVOKABLE void setContexts(const QStringList &l);

signals:
    void languageChanged();
    void sessionChanged();
    void lastErrorChanged();
    void dataChanged();
    void syncBusyChanged();
    void syncStatusChanged();
    void syncFinished(bool ok, const QString &message);
    void themeChanged();
    void disclaimerChanged();
    void locked();                                   // automatische Sperre
    void usbChanged();
    void usbFinished(bool ok, const QString &message);
    void usbReplaced();                              // Datenbank vom Computer übernommen → anmelden

private:
    class QQmlEngine *m_engine = nullptr;
    void setError(const QString &e);
    void afterLogin();
    void bump();
    void applyPalette();
    QString dbPath() const;

    QString m_lastError;
    QString m_syncMessage;
    int m_revision = 0;
    QTimer m_idle;
    class UsbSyncServer *m_usb = nullptr;
    class TabletReminders *m_reminders = nullptr;
    bool m_usbConnected = false;
    QString m_usbStatus;
    // Krankenakte: Dokument im Entstehen (Fotos oder eine PDF-Datei)
    QList<QImage> m_draftPages;
    QByteArray m_draftPdf;
    QString m_draftFileName;
    int m_draftRevision = 0;
    QHash<QString, QImage> m_pageCache;           // gerenderte Seiten (Inhalte ändern sich nie)
};
