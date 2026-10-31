// SPDX-License-Identifier: MIT
#include "appcontroller.h"
#include "edition.h"
#include "wounds.h"

#include "appsettings.h"
#include "bmi.h"
#include "careinfo.h"
#include "medstock.h"
#include <algorithm>
#include "contacts.h"
#include "database.h"
#include "dbsync.h"
#include "dbtransfer.h"
#include "demodata.h"
#include "drugclass.h"
#include "usbsync.h"
#include "tabletreminders.h"
#include "formloader.h"
#include "i18n.h"
#include "journalmodel.h"
#include "keystore.h"
#include "medschedule.h"
#include "peakflow.h"
#include "protocols.h"
#include "inr.h"
#include "fluid.h"
#include "stool.h"
#include "reportprinter.h"
#include "session.h"
#include "sounds.h"
#include "themes.h"
#include "vitaldefs.h"
#include "websync.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPalette>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>
#include <cmath>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace {
QString iso(const QDate &d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QDate fromIso(const QVariant &v) { return QDate::fromString(v.toString(), Qt::ISODate); }
QString dmy(const QDate &d) { return d.isValid() ? QLocale().toString(d, QStringLiteral("dd.MM.yyyy")) : QString(); }

// Zusatzfelder der Messung, die die Tablet-Eingabe immer anbietet (wie im Standardformular)
struct ExtraField { const char *key; const char *label; const char *unit; int min, max; };
const ExtraField kExtraFields[] = {
    { "Atemfrequenz", QT_TRANSLATE_NOOP("AppController", "Atemfrequenz"), "/min", 3, 80 },
};

std::optional<double> optNum(const QVariant &v)
{
    if (!v.isValid() || v.isNull() || v.toString().trimmed().isEmpty()) return std::nullopt;
    bool ok = false;
    double d = v.toDouble(&ok);
    if (!ok) d = QLocale().toDouble(v.toString().trimmed(), &ok);
    if (!ok) d = v.toString().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? std::optional<double>(d) : std::nullopt;
}
std::optional<int> optInt(const QVariant &v)
{
    const auto d = optNum(v);
    return d ? std::optional<int>(qRound(*d)) : std::nullopt;
}

QString sexText(const QString &s)
{
    if (s == QLatin1String("m")) return QCoreApplication::translate("AppController", "männlich");
    if (s == QLatin1String("w")) return QCoreApplication::translate("AppController", "weiblich");
    if (s == QLatin1String("d")) return QCoreApplication::translate("AppController", "divers");
    return {};
}
} // namespace

AppController::AppController(QObject *parent) : QObject(parent)
{
    applyPalette();
    // Systemumschaltung hell/dunkel verfolgen (Qt ≥ 6.5)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (theme() == QLatin1String("system")) { applyPalette(); emit themeChanged(); }
    });
#endif
    m_reminders = new TabletReminders(this);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s == Qt::ApplicationActive && loggedIn() && !m_demo) m_reminders->scheduleSoon();
    });
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (!loggedIn()) return;
        lock();
        emit locked();
    });
    Database &db = Database::instance();
    // Medikamente mit abgelaufenem Enddatum stündlich in die Historie verschieben
    auto *expire = new QTimer(this);
    expire->setInterval(60 * 60 * 1000);
    connect(expire, &QTimer::timeout, this, [this] { if (loggedIn()) Database::instance().archiveExpiredMedications(); });
    expire->start();
    connect(&db, &Database::patientsChanged, this, &AppController::bump);
    connect(&db, &Database::vitalsChanged, this, &AppController::bump);
    connect(&db, &Database::appointmentsChanged, this, &AppController::bump);
    connect(&db, &Database::documentsChanged, this, &AppController::bump);
    connect(&db, &Database::woundsChanged, this, &AppController::bump);
    DbSync &ds = DbSync::instance();
    connect(&ds, &DbSync::started, this, [this] { emit syncBusyChanged(); });
    connect(&ds, &DbSync::finished, this, [this](bool ok, const QString &msg) {
        m_syncMessage = msg;
        emit syncBusyChanged();
        emit syncStatusChanged();
        emit syncFinished(ok, msg);
    });
    setupWebsiteSignals();
}

QString AppController::version() const { return QCoreApplication::applicationVersion(); }
QString AppController::dbPath() const { return m_demo ? DemoData::databasePath() : AppSettings::databasePath(); }
bool AppController::hasDatabase() const { return Database::fileHasContent(dbPath()) && QFile::exists(KeyStore::pathFor(dbPath())); }
bool AppController::loggedIn() const { return Session::isLoggedIn() && Database::instance().isOpen(); }
bool AppController::isAdmin() const { return Session::isAdmin(); }
QString AppController::userName() const { return Session::user(); }
bool AppController::syncBusy() const { return DbSync::instance().isBusy(); }
bool AppController::syncPending() const { return loggedIn() && DbSync::instance().status().pending; }
bool AppController::syncEnabled() const { return loggedIn() && DbSync::config().enabled; }
QString AppController::language() const { return QLocale().language() == QLocale::German ? QStringLiteral("de") : QStringLiteral("en"); }
bool AppController::languageChosen() const
{
    QSettings st;
    if (st.value(QStringLiteral("ui/languageChosen"), false).toBool()) return true;
    if (hasDatabase()) {          // bestehende Einrichtung: nicht mehr fragen
        st.setValue(QStringLiteral("ui/languageChosen"), true);
        return true;
    }
    return false;
}

void AppController::setLanguage(const QString &lang)
{
    AppSettings::setLanguage(lang == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("de"));
    QSettings().setValue(QStringLiteral("ui/languageChosen"), true);
    I18n::install(*QCoreApplication::instance());
    if (m_engine) m_engine->retranslate();
    emit languageChanged();
}

bool AppController::openGettingStarted()
{
    const bool de = language() == QLatin1String("de");
    const QString pre = Edition::light() ? QStringLiteral("VitaParms-light-") : QStringLiteral("VitaParms-");
    const QStringList names = de ? QStringList{ pre + QStringLiteral("ErsteSchritte.pdf"), pre + QStringLiteral("GettingStarted.pdf") }
                                 : QStringList{ pre + QStringLiteral("GettingStarted.pdf"), pre + QStringLiteral("ErsteSchritte.pdf") };
    const QDir app(QCoreApplication::applicationDirPath());
    QStringList dirs{ QStringLiteral("assets:/docs"),                 // Android: im Paket
                      app.filePath(QStringLiteral("docs")), app.filePath(QStringLiteral("../docs")) };
#ifdef VP_SOURCE_DOCS
    dirs << QStringLiteral(VP_SOURCE_DOCS);                          // Entwicklung: Quellordner
#endif
    for (const QString &n : names)
        for (const QString &d : std::as_const(dirs)) {
            const QString src = d + QLatin1Char('/') + n;
            if (!QFile::exists(src)) continue;
            // Android kann Dateien aus dem Paket nicht direkt an andere Apps geben → in den Datenordner kopieren
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/docs");
            QDir().mkpath(dir);
            const QString dst = dir + QLatin1Char('/') + n;
            if (QFileInfo(dst).size() != QFileInfo(src).size()) {
                QFile::remove(dst);
                if (!QFile::copy(src, dst)) continue;
                QFile::setPermissions(dst, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }
            if (openFile(dst)) return true;
            setError(tr("Die Kurzanleitung lässt sich nicht öffnen. Ist eine App zum Anzeigen von PDF-Dateien installiert?"));
            return false;
        }
    setError(tr("Die Kurzanleitung „Erste Schritte“ ist in dieser Installation nicht enthalten."));
    return false;
}

bool AppController::disclaimerAccepted() const { return AppSettings::disclaimerAccepted(); }
void AppController::acceptDisclaimer() { AppSettings::setDisclaimerAccepted(true); emit disclaimerChanged(); }

void AppController::setError(const QString &e)
{
    m_lastError = e;
    emit lastErrorChanged();
}

void AppController::bump()
{
    if (m_reminders && loggedIn() && !m_demo) m_reminders->scheduleSoon();   // Erinnerungen neu stellen (nicht in der Demo)
    ++m_revision;
    emit dataChanged();
    emit syncStatusChanged();
}

// ---------------------------------------------------------------- Erscheinungsbild

QString AppController::theme() const
{
    return QSettings().value(QStringLiteral("mobile/theme"), QStringLiteral("system")).toString();
}

void AppController::setTheme(const QString &t)
{
    QSettings().setValue(QStringLiteral("mobile/theme"), t);
    applyPalette();
    emit themeChanged();
}

bool AppController::dark() const
{
    const QString t = theme();
    if (t == QLatin1String("dark")) return true;
    if (t == QLatin1String("light")) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void AppController::applyPalette()
{
    // Die Kurven (ChartRenderer) und PDF-freie Bildschirmausgaben richten sich nach der Palette
    QPalette p;
    if (dark()) {
        p.setColor(QPalette::Window, QColor(0x0F, 0x17, 0x1D));
        p.setColor(QPalette::Base, QColor(0x16, 0x21, 0x2A));
        p.setColor(QPalette::Text, QColor(0xE6, 0xED, 0xF2));
        p.setColor(QPalette::WindowText, QColor(0xE6, 0xED, 0xF2));
    } else {
        p.setColor(QPalette::Window, QColor(0xF3, 0xF6, 0xF8));
        p.setColor(QPalette::Base, Qt::white);
        p.setColor(QPalette::Text, QColor(0x16, 0x22, 0x2B));
        p.setColor(QPalette::WindowText, QColor(0x16, 0x22, 0x2B));
    }
    QApplication::setPalette(p);
    QSettings().setValue(QStringLiteral("ui/theme"), QStringLiteral("System"));
}

// ---------------------------------------------------------------- Anmeldung

QStringList AppController::accounts() const
{
    KeyStore ks;
    QStringList l;
    if (ks.load(KeyStore::pathFor(dbPath())))
        for (const Account &a : ks.accounts()) l << a.name;
    return l;
}

bool AppController::login(const QString &name, const QString &password)
{
    QString err;
    if (!Session::login(dbPath(), name.trimmed(), password, &err)) {
        setError(err.isEmpty() ? tr("Name oder Passwort stimmt nicht.") : err);
        return false;
    }
    afterLogin();
    return true;
}

void AppController::afterLogin()
{
    DbSync::adoptInitialState();
    DbSync::instance().watchDatabase();
    userActivity();
    setError({});
    emit sessionChanged();
    bump();
    QTimer::singleShot(1500, this, &AppController::syncIfDue);
    if (m_demo) return;            // Demo: keine Erinnerungen, kein Abgleich per USB
    // Termin-Erinnerungen: beim ersten Mal um Erlaubnis bitten, dann die nächsten Erinnerungen stellen
    if (!QSettings().value(QStringLiteral("reminders/asked"), false).toBool()) {
        QSettings().setValue(QStringLiteral("reminders/asked"), true);
        m_reminders->requestPermission();
    }
    m_reminders->scheduleNow();
    ensureUsbServer(nullptr);      // Computer kann jetzt ohne Code abgleichen
}

int AppController::plannedReminders() const
{
    return int(TabletReminders::plan(QDateTime::currentDateTime()).size());
}

bool AppController::notificationsAllowed() const
{
    return m_reminders->permissionGranted();
}

void AppController::requestNotifications()
{
    m_reminders->requestPermission();
}

void AppController::lock()
{
    m_pageCache.clear();          // Seitenbilder der Krankenakte nicht im Speicher lassen
    draftClear();
    // Vor dem Sperren noch ausstehende Änderungen senden
    const DbSync::Config c = DbSync::config();
    if (loggedIn() && c.autoSync && DbSync::isReady(c) && DbSync::instance().status().pending) {
        QString msg;
        DbSync::instance().syncBlocking(60000, &msg);
    }
    Database::instance().close();
    Session::clear();
    m_idle.stop();
    // mit gültigem Code bleibt der USB-Abgleich bis zum Programmende erreichbar (z. B. Einrichtung)
    if (m_usb && !m_usb->codeEnabled()) { m_usb->stop(); m_usbConnected = false; }
    if (m_demo) {                  // Sperren beendet die Demo – danach wieder die eigene Datenbank
        m_demo = false;
        DemoData::setActive(false);
    }
    emit sessionChanged();
}

// ---------------------------------------------------------------- USB-Abgleich

// Der Computer kann sich verbinden, solange die App angemeldet ist (ohne Code, mit derselben
// Datenbank); die Seite „USB-Abgleich“ zeigt zusätzlich einen Code für die erste Einrichtung.
bool AppController::ensureUsbServer(QString *error)
{
    if (!m_usb) {
        m_usb = new UsbSyncServer(this);
        m_usb->importBundle = [this](const QString &path, QString *error) {
            const bool ok = importDatabase(QUrl::fromLocalFile(path).toString(), QString());
            if (!ok && error) *error = m_lastError;
            return ok;
        };
        connect(m_usb, &UsbSyncServer::codeChanged, this, &AppController::usbChanged);
        connect(m_usb, &UsbSyncServer::status, this, [this](const QString &t) { m_usbStatus = t; emit usbChanged(); });
        connect(m_usb, &UsbSyncServer::connectedChanged, this, [this](bool c) {
            m_usbConnected = c;
            // nach der ersten Verbindung: beim Beenden/Sperren ans Abgleichen erinnern
            if (c && !QSettings().contains(QStringLiteral("usbsync/askOnExit"))) setUsbAskOnExit(true);
            emit usbChanged();
        });
        connect(m_usb, &UsbSyncServer::activity, this, &AppController::userActivity);
        connect(m_usb, &UsbSyncServer::merged, this, [this] { bump(); });
        connect(m_usb, &UsbSyncServer::finished, this, &AppController::usbFinished);
        connect(m_usb, &UsbSyncServer::replaced, this, &AppController::usbReplaced);
    }
    return m_usb->start(error);
}

bool AppController::startUsbSync()
{
    QString err;
    if (!ensureUsbServer(&err)) {
        setError(err);
        return false;
    }
    m_usb->enableCode();
    emit usbChanged();
    return true;
}

void AppController::stopUsbSync()
{
    // Code und Verbindung bleiben bis zum Programmende gültig – nichts zu tun
    emit usbChanged();
}

bool AppController::usbAskOnExit() const
{
    return QSettings().value(QStringLiteral("usbsync/askOnExit"), true).toBool();   // Standard: fragen
}

void AppController::setUsbAskOnExit(bool on)
{
    QSettings().setValue(QStringLiteral("usbsync/askOnExit"), on);
    emit usbChanged();
}

bool AppController::usbActive() const
{
    return m_usb && m_usb->isListening() && m_usb->codeEnabled();
}

QString AppController::usbCode() const
{
    return m_usb ? m_usb->code() : QString();
}

void AppController::userActivity()
{
    const int min = AppSettings::autoLockMinutes();
    if (min > 0 && loggedIn()) m_idle.start(min * 60 * 1000);
}

QString AppController::passwordProblem(const QString &pw) const
{
    return KeyStore::passwordProblem(pw);
}

QString AppController::createDatabase(const QString &admin, const QString &password)
{
    QDir().mkpath(QFileInfo(dbPath()).absolutePath());
    QString recovery, err;
    if (!Session::createDatabase(dbPath(), admin.trimmed(), password, &recovery, &err)) {
        setError(err);
        return {};
    }
    afterLogin();
    return recovery;
}

bool AppController::setupFromServer(const QString &url, const QString &uploadKey)
{
    QDir().mkpath(QFileInfo(dbPath()).absolutePath());
    QString err;
    if (!DbSync::downloadInitial(url.trimmed(), uploadKey.trimmed(), dbPath(), &err)) {
        setError(err);
        return false;
    }
    emit sessionChanged();
    return true;
}

QString AppController::serverInfo(const QString &url, const QString &uploadKey)
{
    QString msg;
    DbSync::serverRevision(url.trimmed(), uploadKey.trimmed(), &msg);
    return msg;
}

bool AppController::isDatabaseBundle(const QString &fileUrl) const
{
    return DbTransfer::isBundle(fileUrl);
}

QString AppController::fileName(const QString &fileUrl) const
{
    const QUrl u(fileUrl);
    const QString n = u.isLocalFile() ? QFileInfo(u.toLocalFile()).fileName() : u.fileName();
    return QUrl::fromPercentEncoding(n.toUtf8()).section(QLatin1Char('/'), -1).section(QLatin1Char(':'), -1);
}

bool AppController::importDatabase(const QString &fileUrl, const QString &keysUrl)
{
    if (loggedIn()) lock();
    if (!DbTransfer::isBundle(fileUrl) && keysUrl.isEmpty()) {
        // auf dem Tablet ist die Datei daneben nicht erreichbar: Schlüsseldatei mit auswählen
        const QUrl u(fileUrl);
        if (!(u.isLocalFile() && QFile::exists(KeyStore::pathFor(u.toLocalFile())))) {
            setError(tr("Bitte auch die zugehörige Schlüsseldatei (.keys) wählen – oder am Computer "
                        "„Datei → Datenbank exportieren“ verwenden, dann ist alles in einer Datei."));
            return false;
        }
    }
    QString safety, err;
    bool hasKeys = false;
    if (!DbTransfer::importDatabase(fileUrl, keysUrl, dbPath(), &safety, &hasKeys, &err)) {
        setError(err);
        return false;
    }
    if (!hasKeys) {
        setError(tr("Die Datei enthält keine Anmeldekonten (.keys)."));
        return false;
    }
    emit sessionChanged();
    return true;
}

QVariantMap AppController::readCouplingFile(const QString &fileUrl)
{
    const QUrl u(fileUrl);
    QFile f(u.isLocalFile() ? u.toLocalFile() : fileUrl);
    QVariantMap m;
    if (!f.open(QIODevice::ReadOnly)) { setError(f.errorString()); return m; }
    const QJsonObject o = QJsonDocument::fromJson(f.read(64 * 1024)).object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("vitaparms-coupling")) {
        setError(tr("Das ist keine Kopplungsdatei von VitaParms."));
        return m;
    }
    m.insert(QStringLiteral("url"), o.value(QStringLiteral("url")).toString());
    m.insert(QStringLiteral("uploadKey"), o.value(QStringLiteral("uploadKey")).toString());
    return m;
}

// ---------------------------------------------------------------- Personen

QVariantMap AppController::personSummary(qint64 id)
{
    QVariantMap m;
    const auto p = Database::instance().patient(id);
    if (!p) return m;
    m.insert(QStringLiteral("id"), p->id);
    m.insert(QStringLiteral("name"), p->displayName());
    m.insert(QStringLiteral("lastName"), p->lastName);
    m.insert(QStringLiteral("firstName"), p->firstName);
    m.insert(QStringLiteral("birthText"), dmy(p->birthDate));
    m.insert(QStringLiteral("age"), p->ageAt(QDate::currentDate()));
    m.insert(QStringLiteral("sexText"), sexText(p->sex));
    m.insert(QStringLiteral("careLevel"), p->careLevel);
    m.insert(QStringLiteral("careText"), CareInfo::careLevelText(*p));
    m.insert(QStringLiteral("careSoon"), CareInfo::careReviewSoon(*p));
    m.insert(QStringLiteral("disabilityDegree"), p->disabilityDegree);
    m.insert(QStringLiteral("disabilityText"), CareInfo::disabilityText(*p));
    m.insert(QStringLiteral("disabilityMarks"), p->disabilityMarks);
    m.insert(QStringLiteral("disabilitySoon"), CareInfo::disabilityExpiresSoon(*p));
    m.insert(QStringLiteral("allergies"), p->extra.value(QStringLiteral("Allergien")).toString().trimmed());
    m.insert(QStringLiteral("diagnoses"), p->diagnoses.trimmed());
    m.insert(QStringLiteral("amputations"), p->amputations.any() ? Bmi::describe(p->amputations) : QString());
    QStringList emergency;
    for (const Medication &med : Database::instance().medications(p->id, false))
        if (med.category == Medication::Emergency)
            emergency << med.name + (med.strength.isEmpty() ? QString() : QLatin1Char(' ') + med.strength)
                             + (med.rhythmNote.isEmpty() ? QString() : QStringLiteral(" – ") + med.rhythmNote);
    m.insert(QStringLiteral("emergency"), emergency.join(QStringLiteral("; ")));
    return m;
}

QVariantList AppController::persons()
{
    QVariantList l;
    if (!loggedIn()) return l;
    for (const Patient &p : Database::instance().patients()) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), p.id);
        m.insert(QStringLiteral("name"), p.displayName());
        m.insert(QStringLiteral("birthText"), dmy(p.birthDate));
        m.insert(QStringLiteral("careLevel"), p.careLevel);
        m.insert(QStringLiteral("disabilityDegree"), p.disabilityDegree);
        l << m;
    }
    return l;
}

QVariantMap AppController::personDetail(qint64 id)
{
    Patient p;
    if (id) {
        if (const auto x = Database::instance().patient(id)) p = *x;
    }
    QVariantMap m;
    m.insert(QStringLiteral("id"), p.id);
    m.insert(QStringLiteral("lastName"), p.lastName);
    m.insert(QStringLiteral("firstName"), p.firstName);
    m.insert(QStringLiteral("birthDate"), iso(p.birthDate));
    m.insert(QStringLiteral("sex"), p.sex);
    m.insert(QStringLiteral("street"), p.street);
    m.insert(QStringLiteral("zip"), p.zip);
    m.insert(QStringLiteral("city"), p.city);
    m.insert(QStringLiteral("phone"), p.phone);
    m.insert(QStringLiteral("mobile"), p.mobile);
    m.insert(QStringLiteral("diagnoses"), p.diagnoses);
    m.insert(QStringLiteral("notes"), p.notes);
    m.insert(QStringLiteral("careLevel"), p.careLevel);
    m.insert(QStringLiteral("careReview"), iso(p.careReview));
    m.insert(QStringLiteral("disabilityDegree"), p.disabilityDegree);
    m.insert(QStringLiteral("disabilityMarks"), p.disabilityMarks);
    m.insert(QStringLiteral("disabilityValid"), iso(p.disabilityValidUntil));
    m.insert(QStringLiteral("armLeft"), int(p.amputations.armLeft));
    m.insert(QStringLiteral("armRight"), int(p.amputations.armRight));
    m.insert(QStringLiteral("legLeft"), int(p.amputations.legLeft));
    m.insert(QStringLiteral("legRight"), int(p.amputations.legRight));
    // Zusatzfelder: Beispiel-Felder des Standardformulars immer anbieten, dazu vorhandene eigene
    QStringList keys = { QStringLiteral("Allergien") };   // Kontakte stehen in einer eigenen Liste
    for (auto it = p.extra.begin(); it != p.extra.end(); ++it)
        if (!keys.contains(it.key()) && !CareInfo::isInternalExtraKey(it.key())) keys << it.key();
    {   // Vorsorgedokumente (Anzeige; das hinterlegte PDF lässt sich öffnen)
        QStringList adv;
        QVariantList pdfs;
        for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
            if (const QString l = CareInfo::advanceLine(p, k); !l.isEmpty()) adv << l;
            const CareInfo::AdvanceDoc a = CareInfo::advanceDoc(p, k);
            if (a.present && a.docId > 0)
                pdfs << QVariantMap{ { QStringLiteral("kind"), k }, { QStringLiteral("name"), CareInfo::advanceName(k) },
                                     { QStringLiteral("docId"), a.docId } };
        }
        m.insert(QStringLiteral("advance"), adv);
        m.insert(QStringLiteral("advancePdfs"), pdfs);
    }
    {   // Kontakte (Anzeige; Bearbeitung am Computer im Stammblatt)
        QVariantList cl;
        for (const Contacts::Contact &c : Contacts::list(p)) {
            QStringList l{ c.title() };
            if (!c.person.isEmpty()) l << c.person;
            if (!c.insuranceNo.isEmpty()) l << tr("Vers.-Nr. %1").arg(c.insuranceNo);
            if (!c.address().isEmpty()) l << c.address();
            if (!c.phone.isEmpty()) l << tr("Tel. %1").arg(c.phone);
            if (!c.mobile.isEmpty()) l << tr("mobil %1").arg(c.mobile);
            if (!c.email.isEmpty()) l << c.email;
            if (!c.website.isEmpty()) l << c.website;
            cl << QVariant(QVariantList{ c.kindName(), l.join(QStringLiteral(", ")) + (c.note.isEmpty() ? QString() : QStringLiteral("\n") + c.note) });
        }
        m.insert(QStringLiteral("contacts"), cl);
        // zum Bearbeiten: alle Felder
        QVariantList edit;
        for (const Contacts::Contact &c : Contacts::list(p))
            edit << QVariantMap{ { QStringLiteral("uid"), c.uid }, { QStringLiteral("kind"), c.kind },
                                 { QStringLiteral("kindName"), c.kindName() }, { QStringLiteral("name"), c.name },
                                 { QStringLiteral("specialty"), c.specialty }, { QStringLiteral("person"), c.person },
                                 { QStringLiteral("insuranceNo"), c.insuranceNo }, { QStringLiteral("street"), c.street },
                                 { QStringLiteral("zip"), c.zip }, { QStringLiteral("city"), c.city },
                                 { QStringLiteral("phone"), c.phone }, { QStringLiteral("mobile"), c.mobile },
                                 { QStringLiteral("email"), c.email }, { QStringLiteral("website"), c.website },
                                 { QStringLiteral("note"), c.note }, { QStringLiteral("summary"), c.summary() } };
        m.insert(QStringLiteral("contactList"), edit);
    }
    {   // Vorsorgedokumente zum Bearbeiten
        const QList<MedicalDocument> docs = p.id ? Database::instance().documents(p.id) : QList<MedicalDocument>{};
        auto docTitle = [&docs](qint64 id) {
            for (const MedicalDocument &d : docs)
                if (d.id == id) return d.title + (d.docDate.isValid() ? QStringLiteral(" (") + QLocale().toString(d.docDate, QStringLiteral("dd.MM.yyyy")) + QLatin1Char(')') : QString());
            return QString();
        };
        QVariantList adv;
        for (int k = 0; k < CareInfo::AdvanceCount; ++k) {
            const CareInfo::AdvanceDoc a = CareInfo::advanceDoc(p, k);
            adv << QVariantMap{ { QStringLiteral("kind"), k }, { QStringLiteral("name"), CareInfo::advanceName(k) },
                                { QStringLiteral("personLabel"), CareInfo::advancePersonLabel(k) },
                                { QStringLiteral("present"), a.present }, { QStringLiteral("person"), a.person },
                                { QStringLiteral("address"), a.address }, { QStringLiteral("phone"), a.phone },
                                { QStringLiteral("validUntil"), iso(a.validUntil) }, { QStringLiteral("hasPdf"), a.docId > 0 },
                                { QStringLiteral("docId"), a.docId }, { QStringLiteral("docTitle"), docTitle(a.docId) } };
        }
        m.insert(QStringLiteral("advanceDocs"), adv);
    }
    QVariantList extra;
    for (const QString &k : keys)
        extra << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("label"), FormIO::fieldLabel(k) },
                              { QStringLiteral("value"), p.extra.value(k).toVariant().toString() },
                              { QStringLiteral("multiline"), k == QLatin1String("Allergien") || k == QLatin1String("Notfallkontakt") } };
    m.insert(QStringLiteral("extra"), extra);
    m.insert(QStringLiteral("peakFlowBest"), PeakFlow::personalBest(p));
    {
        const QChar s = PeakFlow::biologicalSex(p);
        m.insert(QStringLiteral("bioSex"), s.isNull() ? QString() : QString(s));
        m.insert(QStringLiteral("bioSexDiffers"), PeakFlow::bioSexDiffers(p));
    }
    {   // INR-Zielbereich, Trinkmengen, Gewichtskontrolle (Text in der Anzeigesprache, leer = nicht gesetzt)
        const Inr::Range r = Inr::range(p);
        const QLocale loc;
        m.insert(QStringLiteral("inrLow"), r.personal ? loc.toString(r.low, 'f', 1) : QString());
        m.insert(QStringLiteral("inrHigh"), r.personal ? loc.toString(r.high, 'f', 1) : QString());
        m.insert(QStringLiteral("fluidMin"), Fluid::minIntake(p) ? QString::number(Fluid::minIntake(p)) : QString());
        m.insert(QStringLiteral("fluidMax"), Fluid::maxIntake(p) ? QString::number(Fluid::maxIntake(p)) : QString());
        m.insert(QStringLiteral("weightWatch"), Fluid::weightWatch(p) > 0 ? loc.toString(Fluid::weightWatch(p), 'f', 1) : QString());
    }
    return m;
}

// Peak Flow im Stammblatt: Sollwert und Bezug für die Warnhinweise (Vorschau beim Bearbeiten)
QString AppController::peakFlowInfo(const QVariantMap &m)
{
    Patient p;
    if (const qint64 id = m.value(QStringLiteral("id")).toLongLong())
        if (const auto x = Database::instance().patient(id)) p = *x;
    p.birthDate = fromIso(m.value(QStringLiteral("birthDate")));
    p.sex = m.value(QStringLiteral("sex")).toString();
    PeakFlow::setPersonalBest(p, m.value(QStringLiteral("peakFlowBest")).toInt());
    const QString bs = m.value(QStringLiteral("bioSex")).toString();
    PeakFlow::setBiologicalSex(p, bs.isEmpty() ? QChar() : bs.at(0));
    const std::optional<double> h = p.id ? Database::instance().latestHeight(p.id) : std::nullopt;
    const PeakFlow::Reference norm = PeakFlow::normReference(p, QDate::currentDate(), h);
    QStringList l;
    l << (norm.value ? tr("Sollwert heute: %1 l/min (%2).").arg(norm.value).arg(norm.basis) : norm.missing);
    const PeakFlow::Reference ref = PeakFlow::reference(p, QDate::currentDate(), h);
    if (ref.value)
        l << tr("Warnhinweise beziehen sich auf den %1 (%2 l/min): gelb unter %3, rot unter %4 l/min.")
                 .arg(ref.source == PeakFlow::Source::PersonalBest ? tr("persönlichen Bestwert") : tr("Sollwert"))
                 .arg(ref.value).arg(int(std::ceil(ref.value * 0.8))).arg(int(std::ceil(ref.value * 0.5)));
    else
        l << tr("Ohne Bezugswert gibt es keine Warnhinweise – bitte den persönlichen Bestwert eintragen.");
    return l.join(QLatin1Char('\n'));
}

// Peak Flow beim Erfassen: Bereich (grün/gelb/rot) mit Text und Handlungshinweis
QVariantMap AppController::peakFlowCheck(qint64 pid, int value, const QString &heightText, const QString &isoDateTime)
{
    QVariantMap r;
    const auto p = Database::instance().patient(pid);
    if (!p) return r;
    std::optional<double> h;
    bool ok = false;
    const double hv = QLocale().toDouble(heightText.trimmed(), &ok);
    if (ok && hv > 0) h = hv;
    else if (const double hc = heightText.trimmed().toDouble(&ok); ok && hc > 0) h = hc;
    if (!h) h = Database::instance().latestHeight(pid);
    QDate day = QDateTime::fromString(isoDateTime, Qt::ISODate).date();
    if (!day.isValid()) day = QDate::fromString(isoDateTime.left(10), Qt::ISODate);
    if (!day.isValid()) day = QDate::currentDate();
    const bool dark = Themes::isDark();
    if (value <= 0) {
        const PeakFlow::Reference ref = PeakFlow::reference(*p, day, h);
        r[QStringLiteral("zone")] = QString();
        r[QStringLiteral("text")] = ref.value ? PeakFlow::limitsText(ref) : ref.missing;
        return r;
    }
    const PeakFlow::Assessment a = PeakFlow::assess(*p, value, day, h);
    r[QStringLiteral("zone")] = PeakFlow::zoneKey(a.zone);
    r[QStringLiteral("zoneName")] = PeakFlow::zoneName(a.zone);
    r[QStringLiteral("color")] = a.zone == PeakFlow::Zone::None ? QString() : PeakFlow::zoneColor(a.zone, dark).name();
    r[QStringLiteral("text")] = a.zone == PeakFlow::Zone::None ? a.ref.missing : a.text;
    r[QStringLiteral("advice")] = a.advice;
    r[QStringLiteral("reference")] = a.ref.description;
    r[QStringLiteral("percent")] = a.percent;
    return r;
}

qint64 AppController::savePerson(const QVariantMap &m)
{
    Patient p;
    if (const qint64 id = m.value(QStringLiteral("id")).toLongLong()) {
        if (const auto x = Database::instance().patient(id)) p = *x;
    }
    p.lastName = m.value(QStringLiteral("lastName")).toString().trimmed();
    if (p.lastName.isEmpty()) { setError(tr("Bitte einen Namen eingeben.")); return 0; }
    p.firstName = m.value(QStringLiteral("firstName")).toString().trimmed();
    p.birthDate = fromIso(m.value(QStringLiteral("birthDate")));
    p.sex = m.value(QStringLiteral("sex")).toString();
    p.street = m.value(QStringLiteral("street")).toString().trimmed();
    p.zip = m.value(QStringLiteral("zip")).toString().trimmed();
    p.city = m.value(QStringLiteral("city")).toString().trimmed();
    p.phone = m.value(QStringLiteral("phone")).toString().trimmed();
    p.mobile = m.value(QStringLiteral("mobile")).toString().trimmed();
    p.diagnoses = m.value(QStringLiteral("diagnoses")).toString();
    p.notes = m.value(QStringLiteral("notes")).toString();
    p.careLevel = qBound(0, m.value(QStringLiteral("careLevel")).toInt(), 5);
    p.careReview = fromIso(m.value(QStringLiteral("careReview")));
    p.disabilityDegree = qBound(0, m.value(QStringLiteral("disabilityDegree")).toInt(), 100);
    p.disabilityMarks = m.value(QStringLiteral("disabilityMarks")).toStringList();
    p.disabilityValidUntil = fromIso(m.value(QStringLiteral("disabilityValid")));
    p.amputations.armLeft = ArmLevel(m.value(QStringLiteral("armLeft")).toInt());
    p.amputations.armRight = ArmLevel(m.value(QStringLiteral("armRight")).toInt());
    p.amputations.legLeft = LegLevel(m.value(QStringLiteral("legLeft")).toInt());
    p.amputations.legRight = LegLevel(m.value(QStringLiteral("legRight")).toInt());
    for (const QVariant &v : m.value(QStringLiteral("extra")).toList()) {
        const QVariantMap e = v.toMap();
        const QString k = e.value(QStringLiteral("key")).toString(), val = e.value(QStringLiteral("value")).toString().trimmed();
        if (k.isEmpty()) continue;
        if (val.isEmpty()) p.extra.remove(k);
        else p.extra.insert(k, val);
    }
    if (m.contains(QStringLiteral("peakFlowBest"))) PeakFlow::setPersonalBest(p, m.value(QStringLiteral("peakFlowBest")).toInt());
    if (m.contains(QStringLiteral("inrLow")))
        Inr::setRange(p, optNum(m.value(QStringLiteral("inrLow"))).value_or(0), optNum(m.value(QStringLiteral("inrHigh"))).value_or(0));
    if (m.contains(QStringLiteral("fluidMin")))
        Fluid::setLimits(p, optInt(m.value(QStringLiteral("fluidMin"))).value_or(0), optInt(m.value(QStringLiteral("fluidMax"))).value_or(0),
                         optNum(m.value(QStringLiteral("weightWatch"))).value_or(0));
    if (m.contains(QStringLiteral("bioSex"))) {      // nach p.sex: gespeichert wird nur eine Abweichung
        const QString bs = m.value(QStringLiteral("bioSex")).toString();
        PeakFlow::setBiologicalSex(p, bs.isEmpty() ? QChar() : bs.at(0));
    }
    if (m.contains(QStringLiteral("contactList"))) {
        QList<Contacts::Contact> list;
        for (const QVariant &v : m.value(QStringLiteral("contactList")).toList()) {
            const QVariantMap c = v.toMap();
            Contacts::Contact x;
            x.uid = c.value(QStringLiteral("uid")).toString();
            if (x.uid.isEmpty()) x.uid = Contacts::newUid();
            x.kind = qBound(0, c.value(QStringLiteral("kind")).toInt(), int(Contacts::KindCount) - 1);
            auto f = [&](const char *k) { return c.value(QLatin1String(k)).toString().trimmed(); };
            x.name = f("name"); x.specialty = f("specialty"); x.person = f("person"); x.insuranceNo = f("insuranceNo");
            x.street = f("street"); x.zip = f("zip"); x.city = f("city"); x.phone = f("phone"); x.mobile = f("mobile");
            x.email = f("email"); x.website = f("website"); x.note = f("note");
            if (!x.isEmpty()) list << x;
        }
        Contacts::store(p, list);
    }
    if (m.contains(QStringLiteral("advanceDocs"))) {
        for (const QVariant &v : m.value(QStringLiteral("advanceDocs")).toList()) {
            const QVariantMap a = v.toMap();
            const int k = a.value(QStringLiteral("kind")).toInt();
            if (k < 0 || k >= CareInfo::AdvanceCount) continue;
            CareInfo::AdvanceDoc d = CareInfo::advanceDoc(p, k);      // PDF-Verweis bleibt
            d.present = a.value(QStringLiteral("present")).toBool();
            d.person = a.value(QStringLiteral("person")).toString().trimmed();
            d.address = a.value(QStringLiteral("address")).toString().trimmed();
            d.phone = a.value(QStringLiteral("phone")).toString().trimmed();
            d.validUntil = fromIso(a.value(QStringLiteral("validUntil")));
            CareInfo::setAdvanceDoc(p, k, d);
        }
    }
    QString err;
    if (!Database::instance().savePatient(p, &err)) { setError(err); return 0; }
    return p.id;
}

QVariantList AppController::contactKinds() const
{
    QVariantList l;
    for (int k = 0; k < Contacts::KindCount; ++k) l << Contacts::kindName(k);
    return l;
}

bool AppController::deletePerson(qint64 id)
{
    QString err;
    if (!Session::isAdmin()) { setError(tr("Nur Administratoren können Personen löschen.")); return false; }
    if (!Database::instance().deletePatient(id, &err)) { setError(err); return false; }
    return true;
}

QVariantList AppController::careMarks() const
{
    QVariantList l;
    for (const CareInfo::Mark &m : CareInfo::marks())
        l << QVariantMap{ { QStringLiteral("code"), m.code }, { QStringLiteral("meaning"), m.meaning } };
    return l;
}

QVariantList AppController::armOptions() const
{
    QVariantList l;
    for (int i = 0; i <= int(ArmLevel::WholeArm); ++i) l << Bmi::armLevelText(ArmLevel(i));
    return l;
}

QVariantList AppController::legOptions() const
{
    QVariantList l;
    for (int i = 0; i <= int(LegLevel::WholeLeg); ++i) l << Bmi::legLevelText(LegLevel(i));
    return l;
}

qint64 AppController::lastPerson() const { return AppSettings::lastPatient(); }
void AppController::setLastPerson(qint64 id) { AppSettings::setLastPatient(id); }

// ---------------------------------------------------------------- Messwerte

QVariantList AppController::vitalFields() const
{
    QVariantList l;
    struct F { VitalKey k; double min, max, step; };
    const bool mmol = AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL;
    const F fields[] = {
        { VitalKey::Systolic, 40, 300, 1 }, { VitalKey::Diastolic, 20, 200, 1 }, { VitalKey::Pulse, 20, 250, 1 },
        { VitalKey::Temperature, 30, 44, 0.1 }, { VitalKey::SpO2, 50, 100, 1 },
        { VitalKey::Glucose, mmol ? 0.5 : 10.0, mmol ? 40.0 : 700.0, mmol ? 0.1 : 1.0 },
        { VitalKey::Weight, 1, 400, 0.1 }, { VitalKey::Height, 30, 250, 1 }, { VitalKey::PeakFlow, 30, 1000, 10 },
        { VitalKey::Inr, 0.5, 12, 0.1 },
    };
    for (const F &f : fields) {
        if (Edition::light() && (f.k == VitalKey::PeakFlow || f.k == VitalKey::Inr)) continue;   // nicht in VitaParms light
        l << QVariantMap{ { QStringLiteral("key"), VitalDefs::key(f.k) }, { QStringLiteral("label"), VitalDefs::label(f.k) },
                          { QStringLiteral("short"), VitalDefs::shortLabel(f.k) }, { QStringLiteral("unit"), VitalDefs::unit(f.k) },
                          { QStringLiteral("decimals"), VitalDefs::decimals(f.k) }, { QStringLiteral("color"), VitalDefs::color(f.k).name() },
                          { QStringLiteral("min"), f.min }, { QStringLiteral("max"), f.max }, { QStringLiteral("step"), f.step },
                          { QStringLiteral("extra"), false } };
    }
    if (Edition::light()) return l;      // keine Zusatzfelder (Atemfrequenz)
    for (const ExtraField &e : kExtraFields)
        l << QVariantMap{ { QStringLiteral("key"), QString::fromUtf8(e.key) },
                          { QStringLiteral("label"), QCoreApplication::translate("AppController", e.label) },
                          { QStringLiteral("short"), QCoreApplication::translate("AppController", e.label) },
                          { QStringLiteral("unit"), QString::fromUtf8(e.unit) }, { QStringLiteral("decimals"), 0 },
                          { QStringLiteral("color"), QStringLiteral("#6B7782") },
                          { QStringLiteral("min"), e.min }, { QStringLiteral("max"), e.max }, { QStringLiteral("step"), 1 },
                          { QStringLiteral("extra"), true } };
    return l;
}

QStringList AppController::contexts() const { return AppSettings::profileContexts(); }
void AppController::setContexts(const QStringList &l) { AppSettings::setProfileContexts(l); }

QVariantList AppController::journal(qint64 pid, const QString &query, int days)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    QDateTime from;
    if (days > 0) from = QDateTime(QDate::currentDate().addDays(-days), QTime(0, 0));
    QList<VitalRecord> recs = Database::instance().vitals(pid, from, {});
    std::sort(recs.begin(), recs.end(), [](const VitalRecord &a, const VitalRecord &b) { return a.measuredAt > b.measuredAt; });
    const QLocale loc;
    const QString q = query.trimmed().toLower();
    // Peak Flow bewerten: Körpergröße zum Messzeitpunkt (zuletzt davor gemessen, sonst zuletzt bekannt)
    const std::optional<Patient> pat = Database::instance().patient(pid);
    const std::optional<double> lastHeight = Database::instance().latestHeight(pid);
    QHash<qint64, std::optional<double>> heightAt;
    {
        std::optional<double> h;
        for (int i = recs.size() - 1; i >= 0; --i) {
            if (recs[i].height) h = recs[i].height;
            heightAt.insert(recs[i].id, h ? h : lastHeight);
        }
    }
    for (const VitalRecord &r : recs) {
        if (!q.isEmpty()) {
            QString hay = r.observations + QLatin1Char(' ') + r.context;
            for (auto it = r.extra.begin(); it != r.extra.end(); ++it) hay += QLatin1Char(' ') + it.value().toVariant().toString();
            if (!hay.toLower().contains(q)) continue;
        }
        QVariantList values;
        if (r.systolic || r.diastolic)
            values << QVariantMap{ { QStringLiteral("label"), tr("RR") },
                                   { QStringLiteral("text"), QStringLiteral("%1/%2").arg(r.systolic ? QString::number(*r.systolic) : QStringLiteral("–"),
                                                                                         r.diastolic ? QString::number(*r.diastolic) : QStringLiteral("–")) },
                                   { QStringLiteral("unit"), QStringLiteral("mmHg") },
                                   { QStringLiteral("color"), VitalDefs::color(VitalKey::Systolic).name() } };
        for (VitalKey k : { VitalKey::Pulse, VitalKey::Temperature, VitalKey::SpO2, VitalKey::Glucose, VitalKey::Weight,
                            VitalKey::Height, VitalKey::Bmi, VitalKey::PeakFlow, VitalKey::Inr, VitalKey::FluidIn, VitalKey::FluidOut,
                            VitalKey::Edema, VitalKey::Pain }) {
            const QString t = VitalDefs::formatted(r, k);
            if (t.isEmpty()) continue;
            QVariantMap v{ { QStringLiteral("label"), VitalDefs::shortLabel(k) }, { QStringLiteral("text"), t },
                           { QStringLiteral("unit"), VitalDefs::unit(k) }, { QStringLiteral("color"), VitalDefs::color(k).name() } };
            if (k == VitalKey::Pain) { v.insert(QStringLiteral("text"), VitalDefs::formatted(r, k, true)); v.insert(QStringLiteral("unit"), QString()); }
            if (k == VitalKey::FluidIn && !r.fluidInKind.isEmpty()) v.insert(QStringLiteral("unit"), QStringLiteral("ml · ") + r.fluidInKind);
            if (k == VitalKey::FluidOut && !r.fluidOutKind.isEmpty()) v.insert(QStringLiteral("unit"), QStringLiteral("ml · ") + r.fluidOutKind);
            if (k == VitalKey::Edema) {
                v.insert(QStringLiteral("text"), Fluid::edemaText(r));
                v.insert(QStringLiteral("unit"), QString());
                if (!Fluid::edemaAdvice(r).isEmpty()) v.insert(QStringLiteral("alert"), Inr::zoneColor(Inr::Zone::High, Themes::isDark()).name());
            }
            if (k == VitalKey::Inr && r.inr && pat) {
                const Inr::Assessment a = Inr::assess(*pat, *r.inr);
                if (a.zone != Inr::Zone::InRange && a.zone != Inr::Zone::None)
                    v.insert(QStringLiteral("alert"), Inr::zoneColor(a.zone, Themes::isDark()).name());
            }
            if (k == VitalKey::PeakFlow && r.peakFlow && pat) {
                const PeakFlow::Assessment a = PeakFlow::assess(*pat, *r.peakFlow, r.measuredAt.date(), heightAt.value(r.id));
                if (a.zone == PeakFlow::Zone::Yellow || a.zone == PeakFlow::Zone::Red) {
                    v.insert(QStringLiteral("alert"), PeakFlow::zoneColor(a.zone, Themes::isDark()).name());
                    v.insert(QStringLiteral("unit"), VitalDefs::unit(k) + QStringLiteral(" · %1 %").arg(a.percent));
                }
            }
            values << v;
        }
        if (r.stool) {
            QStringList t{ Stool::shortText(*r.stool) };
            for (const QString &x : { r.stoolDetail.amount, r.stoolDetail.color, r.stoolDetail.admixture })
                if (!x.isEmpty()) t << x;
            QVariantMap v{ { QStringLiteral("label"), VitalDefs::shortLabel(VitalKey::Stool) }, { QStringLiteral("text"), t.join(QStringLiteral(", ")) },
                           { QStringLiteral("unit"), QString() },
                           { QStringLiteral("color"), Stool::groupColor(Stool::group(*r.stool), Themes::isDark()).name() } };
            if (Stool::needsAttention(r.stoolDetail)) v.insert(QStringLiteral("alert"), QStringLiteral("#D55E00"));
            values << v;
            if (!r.stoolDetail.symptoms.isEmpty())
                values << QVariantMap{ { QStringLiteral("label"), tr("Begleitsymptome") }, { QStringLiteral("text"), r.stoolDetail.symptoms },
                                       { QStringLiteral("unit"), QString() }, { QStringLiteral("color"), QStringLiteral("#6B7782") } };
        }
        for (auto it = r.extra.begin(); it != r.extra.end(); ++it) {
            const QString t = JournalModel::extraText(it.value());
            if (t.isEmpty()) continue;
            values << QVariantMap{ { QStringLiteral("label"), FormIO::fieldLabel(it.key()) }, { QStringLiteral("text"), t },
                                   { QStringLiteral("unit"), QString() }, { QStringLiteral("color"), QStringLiteral("#6B7782") } };
        }
        l << QVariantMap{
            { QStringLiteral("id"), r.id },
            { QStringLiteral("day"), r.measuredAt.date().toString(Qt::ISODate) },
            { QStringLiteral("dayText"), loc.toString(r.measuredAt.date(), QStringLiteral("dddd, d. MMMM yyyy")) },
            { QStringLiteral("time"), loc.toString(r.measuredAt.time(), QStringLiteral("HH:mm")) },
            { QStringLiteral("context"), r.isAdmission ? tr("bei Aufnahme") : r.context },
            { QStringLiteral("values"), values },
            { QStringLiteral("observations"), r.observations.trimmed() },
        };
    }
    return l;
}

static QVariantMap vitalToMap(const VitalRecord &r)
{
    QVariantMap values;
    auto put = [&](VitalKey k) {
        if (const auto v = VitalDefs::value(r, k)) {
            const double d = *v;                  // value() liefert bereits die Anzeigeeinheit (BZ ggf. mmol/l)
            const double f = std::pow(10.0, VitalDefs::decimals(k));
            values.insert(VitalDefs::key(k), std::round(d * f) / f);
        }
    };
    for (VitalKey k : VitalDefs::all())
        if (k != VitalKey::Bmi && k != VitalKey::Stool) put(k);
    for (auto it = r.extra.begin(); it != r.extra.end(); ++it)
        values.insert(it.key(), it.value().toVariant());
    const StoolDetail &sd = r.stoolDetail;
    return QVariantMap{
        { QStringLiteral("fluidInKind"), r.fluidInKind }, { QStringLiteral("fluidOutKind"), r.fluidOutKind },
        { QStringLiteral("edemaSite"), r.edemaSite }, { QStringLiteral("edemaSide"), r.edemaSide },
        { QStringLiteral("stool"), r.stool ? QVariant(*r.stool) : QVariant() },
        { QStringLiteral("stoolDetail"), QVariantMap{ { QStringLiteral("amount"), sd.amount }, { QStringLiteral("color"), sd.color },
                                                      { QStringLiteral("admixture"), sd.admixture }, { QStringLiteral("symptoms"), sd.symptoms } } },
        { QStringLiteral("id"), r.id },
        { QStringLiteral("measuredAt"), r.measuredAt.toString(Qt::ISODate) },
        { QStringLiteral("context"), r.context },
        { QStringLiteral("admission"), r.isAdmission },
        { QStringLiteral("values"), values },
        { QStringLiteral("observations"), r.observations },
    };
}

QVariantMap AppController::vital(qint64 id)
{
    const auto r = Database::instance().vital(id);
    return r ? vitalToMap(*r) : QVariantMap();
}

QVariantMap AppController::newVital(qint64 pid)
{
    VitalRecord r;
    r.patientId = pid;
    const QTime t = QTime::currentTime();
    r.measuredAt = QDateTime(QDate::currentDate(), QTime(t.hour(), t.minute()));
    r.height = Database::instance().latestHeight(pid);   // änderbare Vorgabe (Aufnahme bzw. letzte Angabe)
    return vitalToMap(r);
}

static VitalRecord mapToVital(qint64 pid, const QVariantMap &m, const VitalRecord &base)
{
    VitalRecord r = base;
    r.patientId = pid;
    const QDateTime t = QDateTime::fromString(m.value(QStringLiteral("measuredAt")).toString(), Qt::ISODate);
    if (t.isValid()) r.measuredAt = t;
    r.context = m.value(QStringLiteral("context")).toString().trimmed();
    r.observations = m.value(QStringLiteral("observations")).toString();
    const QVariantMap v = m.value(QStringLiteral("values")).toMap();
    auto val = [&](VitalKey k) { return v.value(VitalDefs::key(k)); };
    r.systolic = optInt(val(VitalKey::Systolic));
    r.diastolic = optInt(val(VitalKey::Diastolic));
    r.pulse = optInt(val(VitalKey::Pulse));
    r.temperature = optNum(val(VitalKey::Temperature));
    r.spo2 = optInt(val(VitalKey::SpO2));
    r.peakFlow = optInt(val(VitalKey::PeakFlow));
    const auto g = optNum(val(VitalKey::Glucose));
    r.glucose = g ? std::optional<double>(VitalDefs::glucoseFromDisplay(*g)) : std::nullopt;
    r.weight = optNum(val(VitalKey::Weight));
    r.height = optNum(val(VitalKey::Height));
    if (const auto i = optNum(val(VitalKey::Inr))) r.inr = *i > 0 ? std::optional<double>(std::round(*i * 10) / 10) : std::nullopt;
    else r.inr = std::nullopt;
    r.fluidIn = optInt(val(VitalKey::FluidIn));
    r.fluidOut = optInt(val(VitalKey::FluidOut));
    if (r.fluidIn && *r.fluidIn <= 0) r.fluidIn.reset();
    if (r.fluidOut && *r.fluidOut <= 0) r.fluidOut.reset();
    r.fluidInKind = r.fluidIn ? m.value(QStringLiteral("fluidInKind")).toString().trimmed() : QString();
    r.fluidOutKind = r.fluidOut ? m.value(QStringLiteral("fluidOutKind")).toString().trimmed() : QString();
    r.pain = optInt(val(VitalKey::Pain));
    if (r.pain && (*r.pain < 0 || *r.pain > 10)) r.pain.reset();
    for (auto it = r.extra.begin(); it != r.extra.end();)     // früher eigenes Feld „Schmerz (NRS)“
        it = Database::isPainFieldKey(it.key()) ? r.extra.erase(it) : std::next(it);
    r.edema = optInt(val(VitalKey::Edema));
    if (r.edema && (*r.edema < 0 || *r.edema > Fluid::kEdemaMax)) r.edema.reset();
    r.edemaSite = r.edema && *r.edema > 0 ? m.value(QStringLiteral("edemaSite")).toString().trimmed() : QString();
    r.edemaSide = r.edema && *r.edema > 0 ? m.value(QStringLiteral("edemaSide")).toString().trimmed() : QString();
    if (m.contains(QStringLiteral("stool"))) {      // Stuhlgang: null = nicht erfasst, 0 = ohne Angabe, 1–7 Bristol
        r.stool = optInt(m.value(QStringLiteral("stool")));
        if (r.stool && (*r.stool < 0 || *r.stool > Stool::kTypes)) r.stool = 0;
        const QVariantMap d = m.value(QStringLiteral("stoolDetail")).toMap();
        r.stoolDetail = {};
        if (r.stool) {
            r.stoolDetail.amount = d.value(QStringLiteral("amount")).toString().trimmed();
            r.stoolDetail.color = d.value(QStringLiteral("color")).toString().trimmed();
            r.stoolDetail.admixture = d.value(QStringLiteral("admixture")).toString().trimmed();
            r.stoolDetail.symptoms = d.value(QStringLiteral("symptoms")).toString().trimmed();
        }
    }
    for (const ExtraField &e : kExtraFields) {
        const QString k = QString::fromUtf8(e.key);
        const auto x = optInt(v.value(k));
        if (x) r.extra.insert(k, *x);
        else r.extra.remove(k);
    }
    return r;
}

QStringList AppController::checkVital(const QVariantMap &m) const
{
    const VitalRecord r = mapToVital(0, m, {});
    QStringList w;
    if (!r.hasAnyValue()) w << tr("Bitte mindestens einen Wert eingeben.");
    if (r.systolic.has_value() != r.diastolic.has_value())
        w << tr("Beim Blutdruck ist nur ein Wert (systolisch oder diastolisch) eingetragen.");
    if (r.systolic && r.diastolic && *r.systolic <= *r.diastolic)
        w << tr("Der systolische Wert (%1) ist nicht größer als der diastolische (%2) – vertauscht?").arg(*r.systolic).arg(*r.diastolic);
    if (r.measuredAt > QDateTime::currentDateTime().addSecs(15 * 60))
        w << tr("Der Messzeitpunkt liegt in der Zukunft.");
    return w;
}

qint64 AppController::saveVital(qint64 pid, const QVariantMap &m)
{
    Database &db = Database::instance();
    VitalRecord base;
    if (const qint64 id = m.value(QStringLiteral("id")).toLongLong())
        if (const auto x = db.vital(id)) base = *x;
    VitalRecord r = mapToVital(pid, m, base);
    const auto p = db.patient(pid);
    if (!p) { setError(tr("Person nicht gefunden.")); return 0; }
    r.bmi = Bmi::compute(r.weight, r.height ? r.height : db.latestHeight(pid), p->amputations);
    QString err;
    if (!db.saveVital(r, &err)) { setError(err); return 0; }
    return r.id;
}

bool AppController::deleteVital(qint64 id)
{
    QString err;
    if (!Database::instance().deleteVital(id, &err)) { setError(err); return false; }
    return true;
}

QString AppController::bmiPreview(qint64 pid, const QVariant &weight, const QVariant &height)
{
    Database &db = Database::instance();
    const auto p = db.patient(pid);
    if (!p) return {};
    const auto w = optNum(weight);
    auto h = optNum(height);
    const bool fallback = !h;
    if (!h) h = db.latestHeight(pid);
    const auto bmi = Bmi::compute(w, h, p->amputations);
    if (!bmi) return w && !h ? tr("Für den BMI wird die Körpergröße benötigt.") : QString();
    QString t = tr("BMI %1 kg/m²").arg(QLocale().toString(*bmi, 'f', 1));
    const int age = p->ageAt(QDate::currentDate());
    if (age >= 18 || age < 0) t += QStringLiteral(" – ") + Bmi::whoCategory(*bmi);
    if (fallback) t += tr(" (Größe aus letzter Messung: %1 cm)").arg(QLocale().toString(*h, 'f', 0));
    if (p->amputations.any()) t += tr(", korrigiert für fehlende Gliedmaßen");
    return t;
}

QVariantList AppController::latest(qint64 pid)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    QList<VitalRecord> recs = Database::instance().vitals(pid);
    std::sort(recs.begin(), recs.end(), [](const VitalRecord &a, const VitalRecord &b) { return a.measuredAt < b.measuredAt; });
    const QLocale loc;
    const QDateTime from = QDateTime::currentDateTime().addDays(-30);
    auto when = [&](const VitalRecord &r) {
        const qint64 d = r.measuredAt.date().daysTo(QDate::currentDate());
        const QString day = d == 0 ? tr("heute") : d == 1 ? tr("gestern") : loc.toString(r.measuredAt.date(), QStringLiteral("dd.MM.yyyy"));
        return day + QLatin1Char(' ') + loc.toString(r.measuredAt.time(), QStringLiteral("HH:mm"))
               + (r.context.isEmpty() ? QString() : QStringLiteral(", ") + r.context);
    };
    const QList<QPair<QList<VitalKey>, QString>> tiles = {
        { { VitalKey::Systolic, VitalKey::Diastolic }, tr("Blutdruck") },
        { { VitalKey::Pulse }, VitalDefs::label(VitalKey::Pulse) },
        { { VitalKey::Glucose }, VitalDefs::label(VitalKey::Glucose) },
        { { VitalKey::Weight }, VitalDefs::label(VitalKey::Weight) },
        { { VitalKey::Temperature }, VitalDefs::label(VitalKey::Temperature) },
        { { VitalKey::SpO2 }, VitalDefs::label(VitalKey::SpO2) },
        { { VitalKey::Pain }, VitalDefs::label(VitalKey::Pain) },
        { { VitalKey::PeakFlow }, VitalDefs::label(VitalKey::PeakFlow) },
    };
    for (const auto &[keys, label] : tiles) {
        if (Edition::light() && keys.first() == VitalKey::PeakFlow) continue;
        const VitalRecord *last = nullptr;
        for (auto it = recs.crbegin(); it != recs.crend(); ++it)
            if (VitalDefs::value(*it, keys.first())) { last = &*it; break; }
        if (!last) continue;
        QString value = keys.size() == 2
            ? QStringLiteral("%1/%2").arg(VitalDefs::formatted(*last, keys[0]), VitalDefs::formatted(*last, keys[1]))
            : VitalDefs::formatted(*last, keys.first());
        // Verlauf der letzten 30 Tage (Tagesmittel) für die kleine Spur
        QVariantList series;
        for (VitalKey k : keys) {
            QMap<QDate, QPair<double, int>> days;
            for (const VitalRecord &r : recs) {
                if (r.measuredAt < from) continue;
                if (const auto v = VitalDefs::value(r, k)) { auto &d = days[r.measuredAt.date()]; d.first += *v; ++d.second; }
            }
            QVariantList pts;
            for (auto it = days.cbegin(); it != days.cend(); ++it)
                pts << QVariantMap{ { QStringLiteral("x"), double(from.date().daysTo(it.key())) / 30.0 },
                                    { QStringLiteral("y"), it.value().first / it.value().second } };
            series << QVariantMap{ { QStringLiteral("color"), VitalDefs::color(k).name() }, { QStringLiteral("points"), pts } };
        }
        QVariantList swatches;
        for (VitalKey k : keys) swatches << VitalDefs::color(k).name();
        l << QVariantMap{ { QStringLiteral("key"), VitalDefs::key(keys.first()) }, { QStringLiteral("label"), label },
                          { QStringLiteral("value"), value }, { QStringLiteral("unit"), VitalDefs::unit(keys.first()) },
                          { QStringLiteral("when"), when(*last) }, { QStringLiteral("colors"), swatches },
                          { QStringLiteral("series"), series } };
    }
    if (Edition::light()) return l;      // VitaParms light: kein INR, keine Ein-/Ausfuhr, Ödeme, Stuhlgang
    const auto pat = Database::instance().patient(pid);
    {   // INR: letzter Wert, farbig außerhalb des Zielbereichs
        const VitalRecord *last = nullptr;
        for (auto it = recs.crbegin(); it != recs.crend(); ++it) if (it->inr) { last = &*it; break; }
        if (last && pat) {
            const Inr::Assessment a = Inr::assess(*pat, *last->inr);
            QVariantList pts;
            for (const VitalRecord &r : recs)
                if (r.inr && r.measuredAt >= from)
                    pts << QVariantMap{ { QStringLiteral("x"), double(from.date().daysTo(r.measuredAt.date())) / 30.0 }, { QStringLiteral("y"), *r.inr } };
            const QString col = VitalDefs::color(VitalKey::Inr).name();
            l << QVariantMap{ { QStringLiteral("key"), QStringLiteral("inr") }, { QStringLiteral("label"), VitalDefs::label(VitalKey::Inr) },
                              { QStringLiteral("value"), loc.toString(*last->inr, 'f', 1) }, { QStringLiteral("unit"), Inr::zoneName(a.zone) },
                              { QStringLiteral("when"), when(*last) }, { QStringLiteral("colors"), QVariantList{ col } },
                              { QStringLiteral("alert"), a.zone != Inr::Zone::InRange },
                              { QStringLiteral("series"), QVariantList{ QVariantMap{ { QStringLiteral("color"), col }, { QStringLiteral("points"), pts } } } } };
        }
    }
    {   // Ein- und Ausfuhr heute; Spur = Einfuhr je Tag
        const auto days = Fluid::perDay(recs, from.date(), QDate::currentDate());
        if (!days.isEmpty()) {
            const Fluid::Day d = days.value(QDate::currentDate());
            QVariantList pin, pout;
            for (auto it = days.cbegin(); it != days.cend(); ++it) {
                const double x = double(from.date().daysTo(it.key())) / 30.0;
                pin << QVariantMap{ { QStringLiteral("x"), x }, { QStringLiteral("y"), double(it.value().in) } };
                pout << QVariantMap{ { QStringLiteral("x"), x }, { QStringLiteral("y"), double(it.value().out) } };
            }
            const QString ci = VitalDefs::color(VitalKey::FluidIn).name(), co = VitalDefs::color(VitalKey::FluidOut).name();
            l << QVariantMap{ { QStringLiteral("key"), QStringLiteral("fluid") }, { QStringLiteral("label"), tr("Ein-/Ausfuhr heute") },
                              { QStringLiteral("value"), QStringLiteral("%1 / %2").arg(loc.toString(d.in), loc.toString(d.out)) },
                              { QStringLiteral("unit"), tr("ml") },
                              { QStringLiteral("when"), tr("Bilanz %1").arg(Fluid::signedMl(d.balance())) },
                              { QStringLiteral("colors"), QVariantList{ ci, co } },
                              { QStringLiteral("alert"), pat && Fluid::maxIntake(*pat) && d.in > Fluid::maxIntake(*pat) },
                              { QStringLiteral("series"), QVariantList{ QVariantMap{ { QStringLiteral("color"), ci }, { QStringLiteral("points"), pin } },
                                                                        QVariantMap{ { QStringLiteral("color"), co }, { QStringLiteral("points"), pout } } } } };
        }
    }
    {   // Ödeme: letzte Angabe
        const VitalRecord *last = nullptr;
        for (auto it = recs.crbegin(); it != recs.crend(); ++it) if (it->edema) { last = &*it; break; }
        if (last && last->measuredAt >= from.addDays(-60)) {
            QVariantList pts;
            for (const VitalRecord &r : recs)
                if (r.edema && r.measuredAt >= from)
                    pts << QVariantMap{ { QStringLiteral("x"), double(from.date().daysTo(r.measuredAt.date())) / 30.0 }, { QStringLiteral("y"), double(*r.edema) } };
            const QString col = VitalDefs::color(VitalKey::Edema).name();
            QStringList where;
            if (!last->edemaSite.isEmpty()) where << last->edemaSite;
            if (!last->edemaSide.isEmpty()) where << last->edemaSide;
            l << QVariantMap{ { QStringLiteral("key"), QStringLiteral("edema") }, { QStringLiteral("label"), VitalDefs::label(VitalKey::Edema) },
                              { QStringLiteral("value"), Fluid::edemaShort(*last->edema) }, { QStringLiteral("unit"), where.join(QStringLiteral(", ")) },
                              { QStringLiteral("when"), when(*last) }, { QStringLiteral("colors"), QVariantList{ col } },
                              { QStringLiteral("alert"), !Fluid::edemaAdvice(*last).isEmpty() },
                              { QStringLiteral("series"), QVariantList{ QVariantMap{ { QStringLiteral("color"), col }, { QStringLiteral("points"), pts } } } } };
        }
    }
    // Stuhlgang: Tage seit dem letzten Eintrag; Spur = Anzahl je Tag (letzte 30 Tage)
    const Stool::Status st = Stool::status(recs);
    if (st.last.isValid() && st.tracked) {
        const auto per = Stool::perDay(recs, from.date(), QDate::currentDate());
        QVariantList pts;
        for (QDate d = from.date(); d <= QDate::currentDate(); d = d.addDays(1))
            pts << QVariantMap{ { QStringLiteral("x"), double(from.date().daysTo(d)) / 30.0 },
                                { QStringLiteral("y"), double(per.value(d).count) } };
        const QString col = VitalDefs::color(VitalKey::Stool).name();
        l << QVariantMap{ { QStringLiteral("key"), VitalDefs::key(VitalKey::Stool) }, { QStringLiteral("label"), VitalDefs::label(VitalKey::Stool) },
                          { QStringLiteral("value"), st.daysWithout == 0 ? tr("heute") : st.daysWithout == 1 ? tr("gestern")
                                                                              : tr("vor %1 Tagen").arg(st.daysWithout) },
                          { QStringLiteral("unit"), QString() },
                          { QStringLiteral("when"), tr("an %1 der letzten 30 Tage").arg(per.size()) },
                          { QStringLiteral("colors"), QVariantList{ col } },
                          { QStringLiteral("alert"), st.warn },
                          { QStringLiteral("series"), QVariantList{ QVariantMap{ { QStringLiteral("color"), col }, { QStringLiteral("points"), pts } } } } };
    }
    return l;
}

QVariantMap AppController::stoolStatus(qint64 pid)
{
    if (!loggedIn() || !pid) return {};
    const Stool::Status st = Stool::status(Database::instance().vitals(pid, QDateTime(QDate::currentDate().addDays(-40), QTime(0, 0)), {}));
    return { { QStringLiteral("warn"), st.warn }, { QStringLiteral("days"), st.daysWithout },
             { QStringLiteral("text"), Stool::warningText(st) }, { QStringLiteral("status"), Stool::statusText(st) } };
}

QVariantList AppController::careNotices(qint64 pid)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    const auto p = Database::instance().patient(pid);
    if (!p) return l;
    const QList<VitalRecord> recent = Database::instance().vitals(pid, QDateTime(QDate::currentDate().addDays(-40), QTime(0, 0)), {});
    auto add = [&](const QString &t, bool critical) {
        l << QVariantMap{ { QStringLiteral("text"), t }, { QStringLiteral("critical"), critical } };
    };
    const VitalRecord *lastInr = nullptr;
    for (const VitalRecord &r : recent)
        if (r.inr && (!lastInr || r.measuredAt > lastInr->measuredAt)) lastInr = &r;
    if (lastInr && lastInr->measuredAt.date().daysTo(QDate::currentDate()) <= 14) {
        const Inr::Assessment a = Inr::assess(*p, *lastInr->inr);
        if (a.zone == Inr::Zone::Low || a.zone == Inr::Zone::High || a.zone == Inr::Zone::VeryHigh)
            add(a.text + QLatin1Char(' ') + a.advice, a.zone == Inr::Zone::VeryHigh);
    }
    for (const Fluid::Notice &n : { Fluid::intakeNotice(*p, recent), Fluid::weightNotice(*p, recent) })
        if (n.warn) add(n.text, false);
    const Stool::Status st = Stool::status(recent);
    if (st.warn) add(Stool::warningText(st), false);
    // Bestandsführung: offene Bestellung (wie am Computer beim Programmstart)
    for (const QString &t : MedStock::reminderLines(MedStock::updateOpenOrder(pid)))
        l << QVariantMap{ { QStringLiteral("text"), t }, { QStringLiteral("critical"), false }, { QStringLiteral("stock"), true } };
    // Wunden: Beurteilung fällig, Infektionszeichen, Verschlechterung, Verbandwechsel fällig
    for (const Wounds::Notice &n : Wounds::patientNotices(pid))
        l << QVariantMap{ { QStringLiteral("text"), n.text }, { QStringLiteral("critical"), n.warn }, { QStringLiteral("wound"), true } };
    return l;
}

QVariantMap AppController::inrCheck(qint64 pid, const QString &valueText)
{
    Patient p;
    if (const auto x = Database::instance().patient(pid)) p = *x;
    const auto v = optNum(valueText);
    if (!v || *v <= 0) return { { QStringLiteral("text"), Inr::rangeText(Inr::range(p)) }, { QStringLiteral("zone"), QString() } };
    const Inr::Assessment a = Inr::assess(p, *v);
    return { { QStringLiteral("text"), a.text }, { QStringLiteral("advice"), a.advice }, { QStringLiteral("zone"), Inr::zoneKey(a.zone) },
             { QStringLiteral("zoneName"), Inr::zoneName(a.zone) }, { QStringLiteral("color"), Inr::zoneColor(a.zone, Themes::isDark()).name() } };
}

QVariantMap AppController::fluidChoices() const
{
    QVariantList grades;
    for (int g = 0; g <= Fluid::kEdemaMax; ++g)
        grades << QVariantMap{ { QStringLiteral("value"), g }, { QStringLiteral("name"), g ? Fluid::edemaShort(g) : Fluid::edemaShort(0) },
                               { QStringLiteral("title"), Fluid::edemaName(g) }, { QStringLiteral("description"), Fluid::edemaDescription(g) } };
    return { { QStringLiteral("inKinds"), Fluid::inKinds() }, { QStringLiteral("outKinds"), Fluid::outKinds() },
             { QStringLiteral("amounts"), Fluid::quickAmounts() }, { QStringLiteral("edemaGrades"), grades },
             { QStringLiteral("edemaSites"), Fluid::edemaSites() }, { QStringLiteral("edemaSides"), Fluid::edemaSides() } };
}

// Bilanz des Tages einschließlich der gerade bearbeiteten Messung (id, measuredAt, values …)
QVariantMap AppController::fluidDay(qint64 pid, const QVariantMap &current)
{
    QVariantMap m;
    if (!loggedIn() || !pid) return m;
    const auto p = Database::instance().patient(pid);
    VitalRecord base;
    const qint64 id = current.value(QStringLiteral("id")).toLongLong();
    if (id) if (const auto x = Database::instance().vital(id)) base = *x;
    const VitalRecord cur = mapToVital(pid, current, base);
    const QDate day = cur.measuredAt.isValid() ? cur.measuredAt.date() : QDate::currentDate();
    QList<VitalRecord> recs;
    for (const VitalRecord &r : Database::instance().vitals(pid, QDateTime(day, QTime(0, 0)), QDateTime(day.addDays(1), QTime(0, 0))))
        if (r.id != id) recs << r;
    recs << cur;
    const Fluid::Day d = Fluid::perDay(recs, day, day).value(day);
    const QLocale loc;
    QString t = (day == QDate::currentDate() ? tr("Heute") : loc.toString(day, QStringLiteral("dd.MM.")))
              + tr(": Einfuhr %1 ml, Ausfuhr %2 ml, Bilanz %3").arg(loc.toString(d.in), loc.toString(d.out), Fluid::signedMl(d.balance()));
    if (p) if (const QString lim = Fluid::limitsText(*p); !lim.isEmpty()) t += QStringLiteral(" · ") + lim;
    m.insert(QStringLiteral("text"), t);
    m.insert(QStringLiteral("over"), p && Fluid::maxIntake(*p) && d.in > Fluid::maxIntake(*p));
    return m;
}

QString AppController::edemaAdvice(const QVariantMap &current) const
{
    VitalRecord r = mapToVital(0, current, {});
    return r.edema && *r.edema >= 0 ? (Fluid::edemaDescription(*r.edema) + (Fluid::edemaAdvice(r).isEmpty() ? QString()
                                                                         : QStringLiteral("\n") + Fluid::edemaAdvice(r)))
                                    : QString();
}

// Nach dem Speichern: Hinweise wie am Computer (INR, Ödeme, Gewichtszunahme, Trinkmenge)
QStringList AppController::careWarnings(qint64 pid, qint64 vitalId)
{
    QStringList l;
    const auto p = Database::instance().patient(pid);
    const auto r = Database::instance().vital(vitalId);
    if (!p || !r) return l;
    const QList<VitalRecord> recent = Database::instance().vitals(pid, QDateTime(r->measuredAt.date().addDays(-10), QTime(0, 0)),
                                                                 QDateTime(r->measuredAt.date().addDays(1), QTime(0, 0)));
    if (r->inr) {
        const Inr::Assessment a = Inr::assess(*p, *r->inr);
        if (a.zone == Inr::Zone::Low || a.zone == Inr::Zone::High || a.zone == Inr::Zone::VeryHigh) l << a.text + QStringLiteral("\n") + a.advice;
    }
    if (r->edema && *r->edema > 0) {
        std::optional<int> prev;
        QDateTime at;
        for (const VitalRecord &x : recent)
            if (x.edema && x.id != r->id && x.measuredAt < r->measuredAt && (!at.isValid() || x.measuredAt > at)) { prev = x.edema; at = x.measuredAt; }
        if (const QString adv = Fluid::edemaAdvice(*r, prev); !adv.isEmpty()) l << tr("Ödeme: %1").arg(Fluid::edemaText(*r)) + QStringLiteral("\n") + adv;
    }
    if (r->weight)
        if (const Fluid::Notice n = Fluid::weightNotice(*p, recent, r->measuredAt.date()); n.warn) l << n.text;
    if (r->fluidIn && Fluid::maxIntake(*p) && r->measuredAt.date() == QDate::currentDate())
        if (const Fluid::Notice n = Fluid::intakeNotice(*p, recent, r->measuredAt.date()); n.warn) l << n.text;
    return l;
}

QVariantMap AppController::stoolChoices() const
{
    QVariantList types;
    for (int t = 0; t <= Stool::kTypes; ++t)
        types << QVariantMap{ { QStringLiteral("value"), t }, { QStringLiteral("name"), Stool::typeName(t) },
                              { QStringLiteral("text"), t ? Stool::description(t) : Stool::choiceText(0) },
                              { QStringLiteral("group"), t ? Stool::groupName(Stool::group(t)) : QString() },
                              { QStringLiteral("color"), Stool::groupColor(Stool::group(t), Themes::isDark()).name() } };
    return { { QStringLiteral("types"), types }, { QStringLiteral("amounts"), Stool::amountChoices() },
             { QStringLiteral("colors"), Stool::colorChoices() }, { QStringLiteral("admixtures"), Stool::admixtureChoices() },
             { QStringLiteral("symptomsHint"), Stool::symptomsHint() }, { QStringLiteral("scale"), Stool::scaleNote() },
             { QStringLiteral("attention"), Stool::attentionText() } };
}

bool AppController::stoolNeedsAttention(const QVariantMap &detail) const
{
    StoolDetail d;
    d.color = detail.value(QStringLiteral("color")).toString();
    d.admixture = detail.value(QStringLiteral("admixture")).toString();
    return Stool::needsAttention(d);
}

// ---------------------------------------------------------------- Medikamente

// --- Medikamente: Rhythmus, PZN, Kennzeichnung (wie im Dialog am Computer) ---------------
static QString vpNormalizePzn(const QString &input)
{
    QString d;
    for (const QChar c : input)
        if (c.isDigit()) d += c;
        else if (!c.isSpace() && c != QLatin1Char('-') && c.toUpper() != QLatin1Char('P') && c.toUpper() != QLatin1Char('Z')
                 && c.toUpper() != QLatin1Char('N') && c != QLatin1Char(':'))
            return {};
    if (d.size() == 7) d.prepend(QLatin1Char('0'));
    return d.size() == 8 ? d : QString();
}
static bool vpValidPzn(const QString &p)
{
    if (p.size() != 8) return false;
    int sum = 0;
    for (int i = 0; i < 7; ++i) {
        if (!p[i].isDigit()) return false;
        sum += p[i].digitValue() * (i + 1);
    }
    const int check = sum % 11;
    return check != 10 && p[7].isDigit() && p[7].digitValue() == check;
}

// Rhythmus aus den Eingaben (nur Regelmedikation; sonst täglich)
static void applyRhythm(Medication &m, const QVariantMap &v)
{
    if (!v.contains(QStringLiteral("rhythmChoice"))) return;
    const int choice = m.category == Medication::Regular ? v.value(QStringLiteral("rhythmChoice")).toInt() : int(MedSchedule::Daily);
    const int n = v.value(QStringLiteral("rhythmInterval"), 1).toInt();
    m.rhythm = {};
    switch (choice) {
    case MedSchedule::EveryNDays: m.rhythm.freq = Recurrence::Daily; m.rhythm.interval = std::max(2, n); break;
    case MedSchedule::Weekdays: {
        m.rhythm.freq = Recurrence::Weekly;
        m.rhythm.interval = std::max(1, n);
        const QVariantList wd = v.value(QStringLiteral("rhythmWeekdays")).toList();
        for (int i = 0; i < 7 && i < wd.size(); ++i)
            if (wd.at(i).toBool()) m.rhythm.weekdays |= 1 << i;
        break;
    }
    case MedSchedule::Monthly: m.rhythm.freq = Recurrence::Monthly; m.rhythm.interval = std::max(1, n); break;
    default: break;
    }
    QDate st = QDate::fromString(v.value(QStringLiteral("rhythmStart")).toString(), Qt::ISODate);
    if (!st.isValid()) st = QDate::currentDate();
    m.rhythmStart = choice == MedSchedule::Daily ? QDate() : st;
}

// Eingaben, die Rhythmus und Kennzeichnung beeinflussen (für Vorschau und Speichern)
static void applyBasics(Medication &m, const QVariantMap &v)
{
    auto str = [&v](const char *k) { return v.value(QLatin1String(k)).toString().trimmed(); };
    m.name = str("name"); m.substance = str("substance"); m.strength = str("strength"); m.form = str("form");
    m.morning = str("morning"); m.noon = str("noon"); m.evening = str("evening"); m.night = str("night");
    m.unit = str("unit"); m.reason = str("reason"); m.notes = str("notes"); m.rhythmNote = str("rhythmNote");
    m.category = qBound(0, v.value(QStringLiteral("category")).toInt(), Medication::CategoryCount - 1);
}

static QVariantList classList(const Medication &m)
{
    QVariantList l;
    for (int g = 0; g < DrugClass::GroupCount; ++g) {
        const DrugClass::Hit a = DrugClass::classifyAuto(m, DrugClass::Group(g));
        const DrugClass::Hit h = DrugClass::classify(m, DrugClass::Group(g));
        l << QVariantMap{ { QStringLiteral("name"), DrugClass::groupName(DrugClass::Group(g)) },
                          { QStringLiteral("on"), h.match }, { QStringLiteral("auto"), a.match },
                          { QStringLiteral("uncertain"), a.uncertain }, { QStringLiteral("reason"), a.reason } };
    }
    return l;
}

static QVariantMap medToMap(const Medication &m)
{
    const MedSchedule::Reminder r = MedSchedule::reminder(m);
    QVariantList slotList;
    for (const MedSchedule::SlotTime &st : MedSchedule::reminderSlots(m))
        slotList << QVariantMap{ { QStringLiteral("on"), st.on }, { QStringLiteral("time"), st.time.toString(QStringLiteral("HH:mm")) } };
    QVariantList days;
    for (int i = 0; i < 7; ++i)
        days << QVariantMap{ { QStringLiteral("name"), QLocale().dayName(i + 1, QLocale::ShortFormat) }, { QStringLiteral("on"), bool(r.days & (1 << i)) } };
    // Bestandsführung (Kurzfassung für die Liste)
    QString stockText;
    int stockState = 0;
    if (const MedStock::Forecast f = MedStock::forecast(m); f.tracked) {
        stockState = f.onOrder && f.state != MedStock::Ok ? -1 : f.state;
        stockText = AppController::tr("Bestand %1%2").arg(MedStock::qtyText(f.stock), m.unit.isEmpty() ? QString() : QLatin1Char(' ') + m.unit);
        if (f.runOut.isValid()) stockText += QStringLiteral(" · ") + AppController::tr("reicht bis %1").arg(QLocale().toString(f.runOut.addDays(-1), QStringLiteral("ddd dd.MM.")));
        stockText += QStringLiteral(" · ") + f.stateText();
    }
    return QVariantMap{
        { QStringLiteral("stockText"), stockText }, { QStringLiteral("stockState"), stockState },
        { QStringLiteral("remind"), r.on && MedSchedule::canRemind(m) },
        { QStringLiteral("remindSlots"), slotList },
        { QStringLiteral("remindSlotsStored"), m.extra.contains(QLatin1String(MedSchedule::kRemindTimesKey)) },
        { QStringLiteral("remindDays"), days },
        { QStringLiteral("remindByRhythm"), m.category == Medication::Regular && !MedSchedule::remindByWeekdays(m) },
        { QStringLiteral("reminderText"), MedSchedule::wantsAppointments(m) ? MedSchedule::reminderText(m) : QString() },
        { QStringLiteral("id"), m.id }, { QStringLiteral("name"), m.name }, { QStringLiteral("substance"), m.substance },
        { QStringLiteral("strength"), m.strength }, { QStringLiteral("form"), m.form }, { QStringLiteral("pzn"), m.pzn },
        { QStringLiteral("morning"), m.morning }, { QStringLiteral("noon"), m.noon }, { QStringLiteral("evening"), m.evening },
        { QStringLiteral("night"), m.night }, { QStringLiteral("unit"), m.unit }, { QStringLiteral("reason"), m.reason },
        { QStringLiteral("notes"), m.notes }, { QStringLiteral("category"), m.category },
        { QStringLiteral("categoryName"), MedSchedule::categoryName(m.category) },
        { QStringLiteral("scheme"), m.dosageScheme() }, { QStringLiteral("schedule"), MedSchedule::fullText(m) },
        { QStringLiteral("rhythmNote"), m.rhythmNote },
        { QStringLiteral("rhythm"), MedSchedule::isInterval(m) ? MedSchedule::describe(m) : QString() },
        { QStringLiteral("startDate"), iso(m.startDate) }, { QStringLiteral("endDate"), iso(m.endDate) },
        { QStringLiteral("archived"), m.archived },
        { QStringLiteral("stopReason"), MedSchedule::stopReason(m) },
        { QStringLiteral("status"), MedSchedule::statusText(m) },
        { QStringLiteral("rhythmChoice"), m.category == Medication::Regular ? int(MedSchedule::choiceOf(m)) : int(MedSchedule::Daily) },
        { QStringLiteral("rhythmInterval"), std::max(1, m.rhythm.interval) },
        { QStringLiteral("rhythmWeekdays"), [&m] {
              QVariantList l;
              for (int i = 0; i < 7; ++i)
                  l << QVariantMap{ { QStringLiteral("name"), QLocale().dayName(i + 1, QLocale::ShortFormat) },
                                    { QStringLiteral("on"), m.rhythm.freq == Recurrence::Weekly && (m.rhythm.weekdays & (1 << i)) } };
              return l; }() },
        { QStringLiteral("rhythmStart"), iso(MedSchedule::anchor(m).isValid() ? MedSchedule::anchor(m) : QDate::currentDate()) },
        { QStringLiteral("classes"), classList(m) },
    };
}

QVariantList AppController::medications(qint64 pid, bool includeArchived)
{
    QVariantList l;
    if (!loggedIn() || !pid) return l;
    for (const auto &[cat, list] : MedSchedule::grouped(Database::instance().medications(pid, includeArchived)))
        for (const Medication &m : list) l << medToMap(m);
    return l;
}

QVariantMap AppController::medication(qint64 pid, qint64 id)
{
    for (const Medication &m : Database::instance().medications(pid, true))
        if (m.id == id) return medToMap(m);
    Medication m;
    m.unit = tr("Stück");
    m.startDate = QDate::currentDate();
    return medToMap(m);
}

QString AppController::medCategoryHint(int category) const
{
    return MedSchedule::categoryHint(category);
}

int AppController::pznStatus(const QString &text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return 0;
    return vpValidPzn(vpNormalizePzn(t)) ? 1 : -1;
}

// Vorschau beim Bearbeiten: Rhythmus in Worten, nächste Einnahmetage, Tagesmenge, automatische Kennzeichnung
QVariantMap AppController::medicationPreview(const QVariantMap &v)
{
    Medication m;
    applyBasics(m, v);
    m.startDate = QDate::fromString(v.value(QStringLiteral("startDate")).toString(), Qt::ISODate);
    m.endDate = QDate::fromString(v.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
    applyRhythm(m, v);
    QVariantMap r;
    QStringList days;
    if (MedSchedule::isInterval(m)) {
        QDate d = QDate::currentDate();
        for (int i = 0; i < 5; ++i) {
            const QDate n = MedSchedule::nextDue(m, d, 800);
            if (!n.isValid()) break;
            days << QLocale().toString(n, QStringLiteral("ddd dd.MM."));
            d = n.addDays(1);
        }
    }
    r[QStringLiteral("rhythmText")] = MedSchedule::describe(m);
    r[QStringLiteral("nextDays")] = days.isEmpty() ? QString() : days.join(QStringLiteral(", ")) + QStringLiteral(" …");
    const DrugClass::DailyAmount a = DrugClass::dailyAmount(m);
    r[QStringLiteral("amount")] = a.unitsText.isEmpty() ? QString()
        : QStringList{ a.unitsText, a.activeText, a.perText }.filter(QRegularExpression(QStringLiteral("\\S"))).join(QStringLiteral(" · "));
    r[QStringLiteral("classes")] = classList(m);
    return r;
}

QStringList AppController::medCategories() const
{
    QStringList l;
    for (int i = 0; i < Medication::CategoryCount; ++i) l << MedSchedule::categoryName(i);
    return l;
}

bool AppController::saveMedication(qint64 pid, const QVariantMap &v)
{
    Database &db = Database::instance();
    QList<Medication> meds = db.medications(pid, true);
    const qint64 id = v.value(QStringLiteral("id")).toLongLong();
    Medication *m = nullptr;
    for (Medication &x : meds) if (x.id == id && id) m = &x;
    if (!m) { meds << Medication(); m = &meds.last(); }
    m->name = v.value(QStringLiteral("name")).toString().trimmed();
    if (m->name.isEmpty()) { setError(tr("Bitte den Namen des Medikaments eingeben.")); return false; }
    m->substance = v.value(QStringLiteral("substance")).toString().trimmed();
    m->strength = v.value(QStringLiteral("strength")).toString().trimmed();
    m->form = v.value(QStringLiteral("form")).toString().trimmed();
    m->morning = v.value(QStringLiteral("morning")).toString().trimmed();
    m->noon = v.value(QStringLiteral("noon")).toString().trimmed();
    m->evening = v.value(QStringLiteral("evening")).toString().trimmed();
    m->night = v.value(QStringLiteral("night")).toString().trimmed();
    m->unit = v.value(QStringLiteral("unit")).toString().trimmed();
    m->reason = v.value(QStringLiteral("reason")).toString().trimmed();
    m->notes = v.value(QStringLiteral("notes")).toString().trimmed();
    m->rhythmNote = v.value(QStringLiteral("rhythmNote")).toString().trimmed();
    m->category = qBound(0, v.value(QStringLiteral("category")).toInt(), Medication::CategoryCount - 1);
    if (v.contains(QStringLiteral("pzn"))) {
        const QString raw = v.value(QStringLiteral("pzn")).toString().trimmed();
        const QString pzn = vpNormalizePzn(raw);
        m->pzn = vpValidPzn(pzn) ? pzn : raw;
    }
    applyRhythm(*m, v);
    if (MedSchedule::choiceOf(*m) == MedSchedule::Weekdays && m->rhythm.weekdays == 0) {
        setError(tr("Bitte mindestens einen Wochentag für die Einnahme wählen.")); return false;
    }
    if (v.contains(QStringLiteral("classes"))) {   // Kennzeichnung: nur Abweichungen von der automatischen Zuordnung
        const QVariantList cl = v.value(QStringLiteral("classes")).toList();
        for (int g = 0; g < DrugClass::GroupCount && g < cl.size(); ++g) {
            const bool on = cl.at(g).toBool();
            const bool autoMatch = DrugClass::classifyAuto(*m, DrugClass::Group(g)).match;
            DrugClass::setOverride(*m, DrugClass::Group(g), on == autoMatch ? std::nullopt : std::optional<bool>(on));
        }
    }
    m->startDate = fromIso(v.value(QStringLiteral("startDate")));
    m->endDate = fromIso(v.value(QStringLiteral("endDate")));
    if (m->endDate.isValid() && m->startDate.isValid() && m->endDate < m->startDate) {
        setError(tr("Das Enddatum liegt vor dem Beginn.")); return false;
    }
    if (MedSchedule::stopPlanned(*m)) {   // vorgemerktes Absetzen folgt dem Enddatum
        if (!m->endDate.isValid()) MedSchedule::cancelPlannedStop(*m);
        else m->extra[QLatin1String(MedSchedule::kStopOnKey)] = m->endDate.addDays(1).toString(Qt::ISODate);
    }
    const bool archived = v.value(QStringLiteral("archived")).toBool();
    if (archived) {            // absetzen: Ende = eingetragenes Datum (höchstens heute), sonst heute
        const QDate day = m->endDate.isValid() && m->endDate <= QDate::currentDate() ? m->endDate : QDate::currentDate();
        MedSchedule::discontinue(*m, day, v.value(QStringLiteral("stopReason")).toString());
    } else if (m->archived) {  // wieder aufnehmen
        const QDate keepEnd = m->endDate;
        MedSchedule::reactivate(*m);
        if (keepEnd.isValid() && keepEnd > QDate::currentDate()) m->endDate = keepEnd;
    }
    if (v.contains(QStringLiteral("remind")) && v.contains(QStringLiteral("remindTimes"))) {   // An Einnahme erinnern
        MedSchedule::Reminder r;
        r.on = v.value(QStringLiteral("remind")).toBool();
        r.days = 0;
        const QVariantList days = v.value(QStringLiteral("remindDays")).toList();
        for (int i = 0; i < 7 && i < days.size(); ++i)
            if (days.at(i).toBool()) r.days |= 1 << i;
        for (const QVariant &t : v.value(QStringLiteral("remindTimes")).toList()) {
            const QTime x = QTime::fromString(t.toString().trimmed(), QStringLiteral("H:mm"));
            if (!x.isValid()) { setError(tr("Bitte die Uhrzeit im Format 08:00 eingeben.")); return false; }
            if (!r.times.contains(x)) r.times << x;
        }
        if (r.on && MedSchedule::canRemind(*m)) {
            if (r.times.isEmpty()) { setError(tr("Bitte mindestens eine Uhrzeit für die Erinnerung wählen.")); return false; }
            if (MedSchedule::remindByWeekdays(*m) && !r.days) { setError(tr("Bitte mindestens einen Tag für die Erinnerung wählen.")); return false; }
        }
        MedSchedule::setReminder(*m, r);
    }
    QString err;
    if (!db.saveMedications(pid, meds, {}, &err)) { setError(err); return false; }
    MedSchedule::syncAppointments(pid, meds);
    bump();
    return true;
}

bool AppController::deleteMedication(qint64 pid, qint64 id)
{
    Database &db = Database::instance();
    QList<Medication> meds = db.medications(pid, true), removed;
    for (int i = meds.size() - 1; i >= 0; --i)
        if (meds[i].id == id) removed << meds.takeAt(i);
    QString err;
    if (!db.saveMedications(pid, meds, { id }, &err)) { setError(err); return false; }
    MedSchedule::syncAppointments(pid, meds, removed);
    bump();
    return true;
}

QVariantList AppController::upcoming(qint64 pid, int days)
{
    QVariantList l;
    if (!loggedIn()) return l;
    const QLocale loc;
    const QDateTime from(QDate::currentDate(), QTime(0, 0));
    for (const Appointment &a : Database::instance().appointments(from, from.addDays(days), {}, pid, false)) {
        const qint64 d = QDate::currentDate().daysTo(a.start.date());
        const QString day = d == 0 ? tr("heute") : d == 1 ? tr("morgen") : loc.toString(a.start.date(), QStringLiteral("ddd, dd.MM."));
        l << QVariantMap{ { QStringLiteral("title"), a.title }, { QStringLiteral("location"), a.location },
                          { QStringLiteral("notes"), a.notes }, { QStringLiteral("priority"), a.priority },
                          { QStringLiteral("when"), a.allDay ? day : day + QLatin1Char(' ') + loc.toString(a.start.time(), QStringLiteral("HH:mm")) } };
    }
    return l;
}

// ---------------------------------------------------------------- Ausgaben

QString AppController::exportReport(const QString &kind, qint64 pid, const QVariantMap &options)
{
    Database &db = Database::instance();
    const auto p = db.patient(pid);
    if (!p) return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/exports");
    QDir().mkpath(dir);
    // alte Ausgaben aufräumen
    for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files))
        if (fi.lastModified().daysTo(QDateTime::currentDateTime()) > 2) QFile::remove(fi.absoluteFilePath());
    QString base = p->displayName();
    base.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("_"));
    const QString stamp = QDate::currentDate().toString(Qt::ISODate);
    QString path;
    bool ok = false;
    if (kind == QLatin1String("sheet")) {
        path = QStringLiteral("%1/Stammblatt_%2_%3.pdf").arg(dir, base, stamp);
        ok = ReportPrinter::patientSheetToPdf(path, *p, db.medications(pid, false), db.admissionVitals(pid));
    } else if (kind == QLatin1String("pharmacy")) {
        path = QStringLiteral("%1/Medikationsliste_Apotheke_%2_%3.pdf").arg(dir, base, stamp);
        ok = ReportPrinter::pharmacyListToPdf(path, *p, db.medications(pid, false));
    } else if (kind == QLatin1String("order")) {
        const auto o = MedStock::order(options.value(QStringLiteral("orderId")).toLongLong());
        if (!o) return {};
        path = QStringLiteral("%1/Bestellliste_%2_%3.pdf").arg(dir, base, o->createdAt.date().toString(Qt::ISODate));
        ok = ReportPrinter::orderListToPdf(path, *p, *o);
    } else if (kind == QLatin1String("preplog")) {
        const int days = options.value(QStringLiteral("days"), 30).toInt();
        const QDate from = days > 0 ? QDate::currentDate().addDays(-days) : QDate();
        QList<MedPreparation> log = db.preparations(pid, from, QDate());
        std::sort(log.begin(), log.end(), [](const MedPreparation &a, const MedPreparation &b) { return a.preparedAt > b.preparedAt; });
        const QString period = days > 0 ? tr("%1 – %2").arg(QLocale().toString(from, QStringLiteral("dd.MM.yyyy")),
                                                               QLocale().toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy")))
                                        : tr("alle Einträge");
        path = QStringLiteral("%1/Stellprotokoll_%2_%3.pdf").arg(dir, base, stamp);
        ok = ReportPrinter::preparationLogToPdf(path, *p, log, period);
    } else if (kind == QLatin1String("medplan")) {
        path = QStringLiteral("%1/Medikamentenplan_%2_%3.pdf").arg(dir, base, stamp);
        ok = ReportPrinter::medicationPlanToPdf(path, *p, db.medications(pid, false));
    } else if (kind == QLatin1String("journal") || kind == QLatin1String("journalXlsx")) {
        const int days = options.value(QStringLiteral("days")).toInt();
        const QDateTime from = days > 0 ? QDateTime(QDate::currentDate().addDays(-days), QTime(0, 0)) : QDateTime();
        JournalModel model;
        model.setRecords(db.vitals(pid, from, {}), db.vitalExtraKeys(pid));
        const QString period = days > 0 ? tr("letzte %1 Tage").arg(days) : tr("gesamter Verlauf");
        if (kind == QLatin1String("journal")) {
            path = QStringLiteral("%1/Verlauf_%2_%3.pdf").arg(dir, base, stamp);
            ok = ReportPrinter::journalToPdf(path, *p, model, period);
        } else {
            path = QStringLiteral("%1/Verlauf_%2_%3.xlsx").arg(dir, base, stamp);
            QString err;
            ok = ReportPrinter::journalToXlsx(path, *p, model, period, &err);
        }
    } else if (kind.startsWith(QLatin1String("stool")) || kind.startsWith(QLatin1String("glucose")) || kind.startsWith(QLatin1String("fluid"))
               || kind.startsWith(QLatin1String("bpProfile"))) {
        // Stuhlprotokoll / BZ-Tagesprofile / Ein- und Ausfuhr aus den erfassten Werten
        const bool stool = kind.startsWith(QLatin1String("stool"));
        const Protocols::Kind pk = stool ? Protocols::Kind::Stool
                                 : kind.startsWith(QLatin1String("glucose")) ? Protocols::Kind::Glucose
                                 : kind.startsWith(QLatin1String("bpProfile")) ? Protocols::Kind::BloodPressure
                                                                                : Protocols::Kind::Fluid;
        const bool xlsx = kind.endsWith(QLatin1String("Xlsx"));
        const int days = options.value(QStringLiteral("days")).toInt();
        Protocols::Options o;
        o.to = QDate::currentDate();
        if (days > 0) o.from = o.to.addDays(1 - days);
        if (options.contains(QStringLiteral("minValues"))) o.minValues = options.value(QStringLiteral("minValues")).toInt();
        const QDateTime from = o.from.isValid() ? QDateTime(o.from.addDays(-31), QTime(0, 0)) : QDateTime();
        const Protocols::Table t = Protocols::build(pk, db.vitals(pid, from, {}), o, *p);
        const QString name = pk == Protocols::Kind::Stool ? QStringLiteral("Stuhlprotokoll")
                           : pk == Protocols::Kind::Glucose ? QStringLiteral("BZ-Tagesprofile")
                           : pk == Protocols::Kind::BloodPressure ? QStringLiteral("RR-Tagesprofile") : QStringLiteral("Bilanz");
        path = QStringLiteral("%1/%2_%3_%4.%5").arg(dir, name, base, stamp, xlsx ? QStringLiteral("xlsx") : QStringLiteral("pdf"));
        QString err;
        ok = xlsx ? Protocols::toXlsx(t, *p, Protocols::periodText(o), path, &err)
                  : Protocols::toPdf(t, *p, Protocols::periodText(o), path);
    } else if (kind.startsWith(QLatin1String("wound-"))) {
        // Wunddokumentation einer Wunde: „wound-<ID>“ bzw. „wound-<ID>Xlsx“
        const bool xlsx = kind.endsWith(QLatin1String("Xlsx"));
        const auto w = Wounds::wound(kind.mid(6).remove(QStringLiteral("Xlsx")).toLongLong());
        if (!w || w->patientId != pid) return {};
        QString label = w->label;
        label.replace(QRegularExpression(QStringLiteral("[^\\w\\-]+")), QStringLiteral("_"));
        path = QStringLiteral("%1/Wunddokumentation_%2_%3_%4.%5").arg(dir, base, label, stamp, xlsx ? QStringLiteral("xlsx") : QStringLiteral("pdf"));
        QString err;
        ok = xlsx ? Wounds::toXlsx(*p, *w, path, &err) : Wounds::toPdf(*p, *w, path);
    } else if (kind == QLatin1String("chart") || kind == QLatin1String("chartXlsx")) {
        ChartSpec spec;
        spec.scale = ChartSpec::Scale(qBound(0, options.value(QStringLiteral("scale")).toInt(), 3));
        ChartSpec::rangeFor(spec.scale, fromIso(options.value(QStringLiteral("ref"))).isValid()
                                            ? fromIso(options.value(QStringLiteral("ref"))) : QDate::currentDate(),
                            spec.from, spec.to);
        spec.panelIds = options.value(QStringLiteral("panels")).toStringList();
        spec.hiddenKeys = options.value(QStringLiteral("hidden")).toStringList();
        spec.records = db.vitals(pid, spec.from.addDays(-31), spec.to);
        spec.setPatient(*p);
        if (kind == QLatin1String("chartXlsx")) {
            path = QStringLiteral("%1/Verlaufskurven_%2_%3.xlsx").arg(dir, base, stamp);
            QString err;
            ok = ReportPrinter::chartsToXlsx(path, *p, spec, &err);
        } else {
            path = QStringLiteral("%1/Verlaufskurven_%2_%3.pdf").arg(dir, base, stamp);
            ok = ReportPrinter::chartsToPdf(path, *p, spec);
        }
    }
    if (!ok) { setError(tr("Die Ausgabe konnte nicht erstellt werden.")); return {}; }
    return path;
}

bool AppController::openFile(const QString &path)
{
    // Android: über den FileProvider der App mit Leseberechtigung an eine Anzeige-App geben
    // (dort drucken, teilen, speichern). Am Desktop: Standardanwendung.
#ifdef Q_OS_ANDROID
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString mime = suffix == QLatin1String("pdf") ? QStringLiteral("application/pdf")
                       : suffix == QLatin1String("xlsx") ? QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")
                       : suffix == QLatin1String("zip") ? QStringLiteral("application/zip") : QStringLiteral("*/*");
    const QJniObject ctx(QNativeInterface::QAndroidApplication::context());
    const jboolean ok = QJniObject::callStaticMethod<jboolean>("de/mbergmann/vitaparms/Capture", "view",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z",
        ctx.object(), QJniObject::fromString(path).object<jstring>(), QJniObject::fromString(mime).object<jstring>(),
        QJniObject::fromString(tr("Öffnen mit …")).object<jstring>());
    return ok;
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

// ---------------------------------------------------------------- Abgleich

QVariantMap AppController::syncConfig()
{
    QVariantMap m;
    if (!loggedIn()) return m;
    const DbSync::Config c = DbSync::config();
    const DbSync::Status st = DbSync::instance().status();
    m.insert(QStringLiteral("enabled"), c.enabled);
    m.insert(QStringLiteral("autoSync"), c.autoSync);
    m.insert(QStringLiteral("url"), c.url);
    m.insert(QStringLiteral("uploadKey"), c.uploadKey);
    m.insert(QStringLiteral("revision"), st.revision);
    m.insert(QStringLiteral("last"), st.last.isValid() ? QLocale().toString(st.last, QLocale::ShortFormat) : QString());
    m.insert(QStringLiteral("lastResult"), st.lastResult);
    m.insert(QStringLiteral("pending"), st.pending);
    QString tls;
    m.insert(QStringLiteral("tlsOk"), DbSync::tlsAvailable(&tls));
    m.insert(QStringLiteral("tls"), tls);
    return m;
}

bool AppController::saveSyncConfig(const QVariantMap &m)
{
    DbSync::Config c = DbSync::config();
    c.enabled = m.value(QStringLiteral("enabled")).toBool();
    c.autoSync = m.value(QStringLiteral("autoSync")).toBool();
    c.url = WebSync::normalizedUrl(m.value(QStringLiteral("url")).toString());
    c.uploadKey = m.value(QStringLiteral("uploadKey")).toString().trimmed();
    QString why;
    if (c.enabled && !DbSync::isReady(c, &why)) { setError(why); return false; }
    DbSync::saveConfig(c);
    emit syncStatusChanged();
    return true;
}

void AppController::syncNow()
{
    QString why;
    if (!DbSync::isReady(DbSync::config(), &why)) {
        m_syncMessage = why;
        emit syncStatusChanged();
        emit syncFinished(false, why);
        return;
    }
    emit syncBusyChanged();
    QTimer::singleShot(0, this, [] { DbSync::instance().sync(); });
}

void AppController::syncIfDue()
{
    if (!loggedIn()) return;
    const DbSync::Config c = DbSync::config();
    if (c.autoSync && DbSync::isReady(c) && !DbSync::instance().isBusy()) syncNow();
}

// ---------------------------------------------------------------- Einstellungen

QString AppController::glucoseUnit() const
{
    return AppSettings::glucoseUnit() == AppSettings::GlucoseUnit::MmolL ? QStringLiteral("mmol/l") : QStringLiteral("mg/dl");
}

void AppController::setGlucoseUnit(const QString &u)
{
    AppSettings::setGlucoseUnit(u == QLatin1String("mmol/l") ? AppSettings::GlucoseUnit::MmolL : AppSettings::GlucoseUnit::MgDl);
    bump();
}

int AppController::autoLockMinutes() const { return AppSettings::autoLockMinutes(); }
void AppController::setAutoLockMinutes(int m) { AppSettings::setAutoLockMinutes(m); userActivity(); }

void AppController::playSound(const QString &kind)
{
    Sounds::play(kind == QLatin1String("countStop") ? Sounds::Kind::CountStop : Sounds::Kind::CountStart);
}
