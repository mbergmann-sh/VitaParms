// VitaParms – Tablet oder Smartphone über das USB-Kabel abgleichen (Desktop-Seite)
// SPDX-License-Identifier: MIT
#include "usbsyncdialog.h"

#include "session.h"
#include "usbsync.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

// ================================================================ adb

namespace {
const char *kAdbKey = "usbsync/adb";

QString exeName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("adb.exe");
#else
    return QStringLiteral("adb");
#endif
}

bool runAdb(const QString &adb, const QStringList &args, QString *out, QString *error, int timeoutMs = 20000)
{
    QProcess p;
    p.start(adb, args);
    if (!p.waitForStarted(5000)) {
        if (error) *error = QCoreApplication::translate("UsbSyncDialog", "adb lässt sich nicht starten: %1").arg(p.errorString());
        return false;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        if (error) *error = QCoreApplication::translate("UsbSyncDialog", "adb antwortet nicht.");
        return false;
    }
    if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput());
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}
} // namespace

QString Adb::find()
{
    const QString chosen = QSettings().value(QLatin1String(kAdbKey)).toString();
    if (!chosen.isEmpty() && QFileInfo(chosen).isExecutable()) return chosen;
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!inPath.isEmpty()) return inPath;
    QStringList roots;
    for (const char *var : { "ANDROID_SDK_ROOT", "ANDROID_HOME" })
        if (qEnvironmentVariableIsSet(var)) roots << qEnvironmentVariable(var);
    const QString home = QDir::homePath();
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsSet("LOCALAPPDATA"))
        roots << qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Android/Sdk");
    roots << QStringLiteral("C:/Android/Sdk") << QStringLiteral("C:/Android/android-sdk") << QStringLiteral("C:");
    roots << home;      // entpackte Platform-Tools im Benutzerordner
#elif defined(Q_OS_MACOS)
    roots << home + QStringLiteral("/Library/Android/sdk");
#else
    roots << home + QStringLiteral("/Android/Sdk") << QStringLiteral("/opt/android-sdk") << QStringLiteral("/usr/lib/android-sdk");
#endif
    for (const QString &r : roots) {
        const QString p = QDir::cleanPath(r + QStringLiteral("/platform-tools/") + exeName());
        if (QFileInfo(p).isExecutable()) return p;
    }
    return {};
}

void Adb::setPath(const QString &path)
{
    QSettings().setValue(QLatin1String(kAdbKey), path);
}

QList<Adb::Device> Adb::devices(const QString &adb, QString *error)
{
    QList<Device> list;
    QString out;
    if (!runAdb(adb, { QStringLiteral("devices"), QStringLiteral("-l") }, &out, error, 30000)) return list;
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const QStringList parts = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 2 || line.startsWith(QLatin1String("List of")) || line.startsWith(QLatin1Char('*'))) continue;
        Device d;
        d.serial = parts.at(0);
        d.state = parts.at(1);
        for (const QString &p : parts)
            if (p.startsWith(QLatin1String("model:"))) d.model = p.mid(6).replace(QLatin1Char('_'), QLatin1Char(' '));
        list << d;
    }
    return list;
}

int Adb::forward(const QString &adb, const QString &serial, quint16 devicePort, QString *error)
{
    QString out;
    if (!runAdb(adb, { QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("tcp:0"),
                       QStringLiteral("tcp:%1").arg(devicePort) }, &out, error))
        return 0;
    const int port = out.trimmed().toInt();
    if (port <= 0 && error) *error = QCoreApplication::translate("UsbSyncDialog", "adb hat keinen Port weitergeleitet.");
    return port;
}

void Adb::removeForward(const QString &adb, const QString &serial, int localPort)
{
    if (adb.isEmpty() || localPort <= 0) return;
    runAdb(adb, { QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("--remove"),
                  QStringLiteral("tcp:%1").arg(localPort) }, nullptr, nullptr, 5000);
}

// ================================================================ Dialog

UsbSyncDialog::UsbSyncDialog(QWidget *parent)
    : QDialog(parent), m_client(new UsbSyncClient(this))
{
    setObjectName(QStringLiteral("UsbSyncDialog"));
    setWindowTitle(tr("Tablet oder Smartphone über USB abgleichen"));
    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(tr(
        "<p>So geht es:</p><ol>"
        "<li>Tablet mit dem USB-Kabel anschließen. Auf dem Tablet muss <b>USB-Debugging</b> eingeschaltet sein; "
        "die Rückfrage „USB-Debugging zulassen?“ bestätigen.</li>"
        "<li>VitaParms auf dem Tablet öffnen und anmelden.</li>"
        "<li><b>Verbinden</b> wählen. Verwenden beide Geräte dieselbe Datenbank, ist kein Code nötig. Für die erste "
        "Einrichtung auf dem Tablet <b>Einstellungen → USB-Abgleich mit dem Computer</b> öffnen und den dort "
        "angezeigten Code eingeben.</li></ol>"), this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);

    // ---- Verbindung
    auto *conn = new QGroupBox(tr("Verbindung"), this);
    auto *form = new QFormLayout(conn);
    auto *adbRow = new QHBoxLayout;
    m_adbLabel = new QLabel(conn);
    m_adbLabel->setObjectName(QStringLiteral("usbAdbLabel"));
    m_adbLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_adbButton = new QPushButton(tr("Auswählen…"), conn);
    adbRow->addWidget(m_adbLabel, 1);
    adbRow->addWidget(m_adbButton);
    form->addRow(tr("Android-Hilfsprogramm:"), adbRow);
    m_adbHint = new QLabel(conn);
    m_adbHint->setObjectName(QStringLiteral("usbAdbHint"));
    m_adbHint->setWordWrap(true);
    m_adbHint->setTextFormat(Qt::RichText);
    m_adbHint->setOpenExternalLinks(true);
    form->addRow(QString(), m_adbHint);

    auto *devRow = new QHBoxLayout;
    m_devices = new QComboBox(conn);
    m_devices->setObjectName(QStringLiteral("usbDeviceCombo"));
    m_devices->setMinimumWidth(260);
    m_search = new QPushButton(tr("Erneut suchen"), conn);
    m_search->setObjectName(QStringLiteral("usbSearchButton"));
    devRow->addWidget(m_devices, 1);
    devRow->addWidget(m_search);
    auto *devLabel = new QLabel(tr("&Gerät:"), conn);
    devLabel->setBuddy(m_devices);
    form->addRow(devLabel, devRow);
    m_deviceHint = new QLabel(conn);
    m_deviceHint->setWordWrap(true);
    form->addRow(QString(), m_deviceHint);

    auto *codeRow = new QHBoxLayout;
    m_code = new QLineEdit(conn);
    m_code->setObjectName(QStringLiteral("usbCodeEdit"));
    m_code->setPlaceholderText(tr("nur für die erste Einrichtung"));
    m_code->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9 ]{0,9}")), m_code));
    m_code->setMinimumWidth(220);
    m_code->setMaximumWidth(260);
    m_connect = new QPushButton(tr("&Verbinden"), conn);
    m_connect->setObjectName(QStringLiteral("usbConnectButton"));
    codeRow->addWidget(m_code);
    codeRow->addWidget(m_connect);
    codeRow->addStretch(1);
    auto *codeLabel = new QLabel(tr("&Code vom Tablet:"), conn);
    codeLabel->setBuddy(m_code);
    form->addRow(codeLabel, codeRow);
    lay->addWidget(conn);

    // ---- Abgleich
    auto *sync = new QGroupBox(tr("Abgleich"), this);
    auto *sl = new QVBoxLayout(sync);
    m_state = new QLabel(sync);
    m_state->setObjectName(QStringLiteral("usbStateLabel"));
    m_state->setWordWrap(true);
    sl->addWidget(m_state);
    m_merge = new QPushButton(tr("&Abgleichen"), sync);
    m_merge->setObjectName(QStringLiteral("usbMergeButton"));
    m_merge->setToolTip(tr("Neue und geänderte Einträge beider Geräte zusammenführen – bei gleichzeitiger Änderung gilt die jüngere"));
    auto *mergeHint = new QLabel(tr("Führt neue und geänderte Einträge beider Geräte zusammen. Danach haben Computer und "
                                    "Tablet denselben, neuesten Stand. Bei gleichzeitiger Änderung derselben Angabe gilt die jüngere."), sync);
    mergeHint->setWordWrap(true);
    sl->addWidget(mergeHint);
    auto *mr = new QHBoxLayout;
    mr->addWidget(m_merge);
    mr->addStretch(1);
    sl->addLayout(mr);
    auto *more = new QLabel(tr("<b>Eine Datenbank ersetzen</b> – für die erste Einrichtung oder wenn die Geräte "
                               "unterschiedliche Datenbanken verwenden. Der bisherige Stand bleibt als Kopie erhalten."), sync);
    more->setWordWrap(true);
    sl->addWidget(more);
    auto *rr = new QHBoxLayout;
    m_send = new QPushButton(tr("Computer → &Tablet…"), sync);
    m_send->setObjectName(QStringLiteral("usbSendButton"));
    m_send->setToolTip(tr("Die Datenbank des Tablets durch die dieses Computers ersetzen"));
    m_fetch = new QPushButton(tr("Tablet → C&omputer…"), sync);
    m_fetch->setObjectName(QStringLiteral("usbFetchButton"));
    m_fetch->setToolTip(tr("Die Datenbank dieses Computers durch die des Tablets ersetzen"));
    rr->addWidget(m_send);
    rr->addWidget(m_fetch);
    rr->addStretch(1);
    sl->addLayout(rr);
    lay->addWidget(sync);

    m_result = new QLabel(this);
    m_result->setObjectName(QStringLiteral("usbResultLabel"));
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_result);

    m_askExit = new QCheckBox(tr("Beim &Beenden von VitaParms fragen, ob abgeglichen werden soll"), this);
    m_askExit->setObjectName(QStringLiteral("usbAskExitCheck"));
    m_askExit->setChecked(askOnExit());
    connect(m_askExit, &QCheckBox::toggled, this, &UsbSyncDialog::setAskOnExit);
    lay->addWidget(m_askExit);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);

    connect(m_adbButton, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Android-Hilfsprogramm adb auswählen"),
                                                       m_adb.isEmpty() ? QDir::homePath() : QFileInfo(m_adb).absolutePath(),
#ifdef Q_OS_WIN
                                                       tr("adb (adb.exe)")
#else
                                                       tr("adb (adb)")
#endif
        );
        if (f.isEmpty()) return;
        Adb::setPath(f);
        findDevices();
    });
    connect(m_search, &QPushButton::clicked, this, &UsbSyncDialog::findDevices);
    connect(m_connect, &QPushButton::clicked, this, [this] { m_connected ? disconnectDevice() : connectDevice(false); });
    connect(m_code, &QLineEdit::returnPressed, this, [this] { if (!m_connected) connectDevice(false); });
    connect(m_merge, &QPushButton::clicked, this, &UsbSyncDialog::doMerge);
    connect(m_send, &QPushButton::clicked, this, &UsbSyncDialog::doSend);
    connect(m_fetch, &QPushButton::clicked, this, &UsbSyncDialog::doFetch);
    connect(m_devices, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateState(); });
    connect(m_client, &UsbSyncClient::progress, this, [this](const QString &t) {
        m_result->setStyleSheet(QString());
        m_result->setText(t);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });

    resize(660, 10);
    fitHeight();
    updateState();
    if (qEnvironmentVariableIsEmpty("VP_USBSYNC_NOSEARCH"))
        QMetaObject::invokeMethod(this, &UsbSyncDialog::autoStart, Qt::QueuedConnection);
}

// Gerät suchen und – bei genau einem Tablet – gleich ohne Code verbinden (gleiche Datenbank)
void UsbSyncDialog::autoStart()
{
    findDevices();
    if (m_devices->count() != 1 || m_connected) return;
    connectDevice(true);
    if (m_connected && m_sameDb && m_autoMerge) doMerge();
}

bool UsbSyncDialog::askOnExit()
{
    return QSettings().value(QStringLiteral("usbsync/askOnExit"), false).toBool();
}

void UsbSyncDialog::setAskOnExit(bool on)
{
    QSettings().setValue(QStringLiteral("usbsync/askOnExit"), on);
}

UsbSyncDialog::~UsbSyncDialog()
{
    disconnectDevice();
}

void UsbSyncDialog::setResult(const QString &text, bool error)
{
    m_result->setStyleSheet(error ? QStringLiteral("color: #b3261e;") : QString());
    m_result->setText(text);
    fitHeight();
}

void UsbSyncDialog::fitHeight()
{
    // umbrechende Texte: Höhe zur aktuellen Breite passend vergrößern
    if (!layout()) return;
    layout()->activate();
    const int h = layout()->totalHeightForWidth(width());
    if (h > height()) resize(width(), h);
}

void UsbSyncDialog::findDevices()
{
    m_devices->clear();
    const QString direct = qEnvironmentVariable("VP_USBSYNC_DIRECT");
    if (!direct.isEmpty()) {             // Test ohne adb
        m_adbLabel->setText(tr("direkte Verbindung (%1)").arg(direct));
        m_adbHint->clear();
        m_devices->addItem(tr("Testgerät (%1)").arg(direct), direct);
        m_deviceHint->clear();
        m_devices->setCurrentIndex(0);
        updateState();
        return;
    }
    m_adb = Adb::find();
    if (m_adb.isEmpty()) {
        m_adbLabel->setText(tr("nicht gefunden"));
        m_adbHint->setText(tr("<b>adb nicht gefunden.</b> Die Android „SDK Platform-Tools“ kostenlos "
                              "<a href=\"https://developer.android.com/tools/releases/platform-tools\">herunterladen</a>, "
                              "entpacken und hier adb auswählen."));
        m_deviceHint->clear();
        updateState();
        return;
    }
    m_adbLabel->setText(QDir::toNativeSeparators(m_adb));
    m_adbHint->clear();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const QList<Adb::Device> list = Adb::devices(m_adb, &err);
    QApplication::restoreOverrideCursor();
    QStringList hints;
    for (const Adb::Device &d : list) {
        const QString name = d.model.isEmpty() ? d.serial : QStringLiteral("%1 (%2)").arg(d.model, d.serial);
        if (d.state == QLatin1String("device"))
            m_devices->addItem(name, d.serial);
        else if (d.state == QLatin1String("unauthorized"))
            hints << tr("%1: Bitte auf dem Gerät die Rückfrage „USB-Debugging zulassen?“ bestätigen und dann „Erneut suchen“.").arg(name);
        else
            hints << tr("%1: nicht bereit (%2). Kabel neu einstecken.").arg(name, d.state);
    }
    if (!err.isEmpty())
        hints << err;
    else if (list.isEmpty())
        hints << tr("Kein Gerät gefunden. Tablet mit dem USB-Kabel anschließen und auf dem Tablet USB-Debugging "
                    "einschalten (Handbuch, Abschnitt „Abgleich über USB“).");
    m_deviceHint->setText(hints.join(QLatin1Char('\n')));
    if (m_devices->count() > 0) m_devices->setCurrentIndex(0);   // mit Platzhaltertext wählt Qt nichts vor
    updateState();
}

void UsbSyncDialog::connectDevice(bool silent)
{
    if (m_devices->currentIndex() < 0) return;
    const QString code = m_code->text().simplified().remove(QLatin1Char(' '));
    if (!code.isEmpty() && code.size() != 6) {
        setResult(tr("Bitte den sechsstelligen Code eingeben, den das Tablet anzeigt."), true);
        m_code->setFocus();
        return;
    }
    QString host = QStringLiteral("127.0.0.1");
    QString err;
    const QString direct = qEnvironmentVariable("VP_USBSYNC_DIRECT");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (!direct.isEmpty()) {
        host = direct.section(QLatin1Char(':'), 0, 0);
        m_localPort = direct.section(QLatin1Char(':'), 1, 1).toInt();
        m_serial.clear();
    } else {
        m_serial = m_devices->currentData().toString();
        m_localPort = Adb::forward(m_adb, m_serial, UsbSync::kDevicePort, &err);
    }
    UsbSyncClient::Info info;
    const bool ok = m_localPort > 0 && m_client->connectTo(host, quint16(m_localPort), code, &info, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        if (direct.isEmpty()) Adb::removeForward(m_adb, m_serial, m_localPort);
        m_localPort = 0;
        // automatischer Versuch beim Öffnen: nur als Hinweis, nicht als Fehler
        setResult(silent ? tr("Noch nicht verbunden: %1").arg(err) : err, !silent);
        updateState();
        return;
    }
    m_connected = true;
    m_tabletLoggedIn = info.loggedIn;
    m_sameDb = info.sameDatabase;
    m_deviceName = m_devices->currentText();
    // nach der ersten erfolgreichen Verbindung: beim Beenden ans Abgleichen erinnern
    if (!QSettings().contains(QStringLiteral("usbsync/askOnExit"))) m_askExit->setChecked(true);
    setResult(QString(), false);
    updateState();
    if (m_sameDb) m_merge->setFocus();
}

void UsbSyncDialog::disconnectDevice()
{
    m_client->disconnectFromDevice();
    if (!m_serial.isEmpty()) Adb::removeForward(m_adb, m_serial, m_localPort);
    m_localPort = 0;
    m_connected = false;
    if (m_result) updateState();
}

void UsbSyncDialog::updateState()
{
    m_deviceHint->setVisible(!m_deviceHint->text().isEmpty());
    m_adbHint->setVisible(!m_adbHint->text().isEmpty());
    if (m_connected && !m_client->isConnected()) {     // vom Tablet getrennt
        m_connected = false;
        Adb::removeForward(m_adb, m_serial, m_localPort);
        m_localPort = 0;
    }
    const bool haveDevice = m_devices->count() > 0;
    m_devices->setEnabled(!m_connected && m_devices->count() > 1);
    m_search->setEnabled(!m_connected && qEnvironmentVariableIsEmpty("VP_USBSYNC_DIRECT"));
    m_adbButton->setEnabled(!m_connected && qEnvironmentVariableIsEmpty("VP_USBSYNC_DIRECT"));
    if (!haveDevice) m_devices->setPlaceholderText(tr("kein Gerät"));
    m_code->setEnabled(!m_connected && haveDevice);
    m_connect->setEnabled(haveDevice);
    m_connect->setText(m_connected ? tr("&Trennen") : tr("&Verbinden"));

    const bool admin = Session::isAdmin();
    m_merge->setEnabled(m_connected && m_sameDb);
    m_send->setEnabled(m_connected && admin);
    m_fetch->setEnabled(m_connected && admin && m_tabletLoggedIn);
    if (!admin) {
        m_send->setToolTip(tr("Nur mit einem Administratorkonto"));
        m_fetch->setToolTip(m_send->toolTip());
    }

    if (!m_connected)
        m_state->setText(tr("Nicht verbunden."));
    else if (!m_tabletLoggedIn)
        m_state->setText(tr("Verbunden mit %1. Auf dem Tablet ist niemand angemeldet – Abgleichen geht erst nach der "
                            "Anmeldung dort. „Computer → Tablet“ ist auch so möglich (z. B. für die erste Einrichtung).")
                             .arg(m_deviceName));
    else if (!m_sameDb)
        m_state->setText(tr("Verbunden mit %1. <b>Tablet und Computer verwenden unterschiedliche Datenbanken</b> – "
                            "Abgleichen ist nicht möglich. Sie können eine der beiden Datenbanken durch die andere ersetzen.")
                             .arg(m_deviceName));
    else
        m_state->setText(tr("Verbunden mit %1 – dieselbe Datenbank. Bereit zum Abgleichen.").arg(m_deviceName));
    fitHeight();
}

void UsbSyncDialog::doMerge()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setEnabled(false);
    UsbSyncClient::Result r;
    QString err;
    const bool ok = m_client->merge(&r, &err);
    setEnabled(true);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        setResult(tr("Abgleich fehlgeschlagen: %1").arg(err), true);
        updateState();
        return;
    }
    QString text = tr("Abgeglichen.\nDieser Computer: %1 neu, %2 geändert, %3 gelöscht.\nTablet: %4 neu, %5 geändert, %6 gelöscht.")
                       .arg(r.localInserted).arg(r.localUpdated).arg(r.localDeleted)
                       .arg(r.remoteInserted).arg(r.remoteUpdated).arg(r.remoteDeleted);
    if (r.keysFromDevice) text += QLatin1Char('\n') + tr("Die Anmeldekonten des Tablets waren neuer und wurden übernommen.");
    if (r.keysToDevice) text += QLatin1Char('\n') + tr("Die Anmeldekonten dieses Computers wurden auf das Tablet übertragen.");
    if (r.filesFromDevice || r.filesToDevice)
        text += QLatin1Char('\n') + tr("Dokumente: %1 vom Tablet geholt, %2 an das Tablet übertragen.").arg(r.filesFromDevice).arg(r.filesToDevice);
    setResult(text, false);
    updateState();
}

void UsbSyncDialog::doSend()
{
    if (QMessageBox::warning(this, tr("Computer → Tablet"),
            tr("Die Datenbank auf dem Tablet wird durch die dieses Computers ersetzt.\n\n"
               "Der bisherige Stand des Tablets bleibt dort als Kopie erhalten. Danach auf dem Tablet mit einem "
               "Konto dieses Computers anmelden.\n\nFortfahren?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setEnabled(false);
    QString err;
    const bool ok = m_client->sendDatabase(&err);
    setEnabled(true);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        setResult(tr("Übertragung fehlgeschlagen: %1").arg(err), true);
    } else {
        setResult(tr("Die Datenbank wurde auf das Tablet übertragen. Bitte dort mit einem Konto dieses Computers anmelden."), false);
        disconnectDevice();
    }
    updateState();
}

void UsbSyncDialog::doFetch()
{
    if (QMessageBox::warning(this, tr("Tablet → Computer"),
            tr("Die Datenbank dieses Computers wird durch die des Tablets ersetzt.\n\n"
               "Der bisherige Stand bleibt als Kopie (*.vor-import-…) erhalten. Anschließend ist eine Anmeldung "
               "mit den Konten des Tablets nötig.\n\nFortfahren?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    setEnabled(false);
    QString err, path;
    const bool ok = m_client->fetchDatabase(&path, &err);
    setEnabled(true);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        setResult(tr("Übertragung fehlgeschlagen: %1").arg(err), true);
        updateState();
        return;
    }
    // Datei aus dem temporären Ordner des Clients sichern, bevor der Dialog endet
    const QString keep = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("vitaparms-tablet-%1.vpdb").arg(QCoreApplication::applicationPid()));
    QFile::remove(keep);
    if (!QFile::copy(path, keep)) {
        setResult(tr("Die Datei konnte nicht gespeichert werden."), true);
        return;
    }
    m_bundle = keep;
    disconnectDevice();
    accept();
}
